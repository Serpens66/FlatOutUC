#pragma once

#include <d3d9.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace stunt_video {
using Microsoft::WRL::ComPtr;
constexpr UINT Width = 512, Height = 288;
constexpr size_t MaxJpeg = 24 * 1024;

enum Kind : uint8_t { Available = 1, Subscribe, Unsubscribe, Picture, PictureSeen };
#pragma pack(push, 1)
struct Message {
	uint64_t event;
	uint64_t subscription;
	uint32_t sequence;
	uint8_t kind;
};
#pragma pack(pop)
static_assert(sizeof(Message) == 21);

inline bool Parse(const void* data, size_t bytes, Message& message) {
	if (bytes < sizeof(message) || bytes > sizeof(message) + MaxJpeg) return false;
	memcpy(&message, data, sizeof(message));
	if (!message.event) return false;
	switch (message.kind) {
	case Available: return bytes == sizeof(message) && !message.subscription && message.sequence <= 1;
	case Subscribe: case Unsubscribe: return bytes == sizeof(message) && message.subscription && !message.sequence;
	case Picture: return bytes > sizeof(message) && message.subscription && message.sequence;
	case PictureSeen: return bytes == sizeof(message) && message.subscription && message.sequence;
	default: return false;
	}
}

inline bool AcceptPicture(const Message& message, uint64_t event, uint64_t subscription, uint32_t previous) {
	return message.kind == Picture && message.event == event && message.subscription == subscription &&
		message.sequence > previous;
}

struct Frame {
	uint64_t event = 0, subscription = 0;
	uint32_t sequence = 0;
	UINT width = 0, height = 0;
	std::vector<uint8_t> bytes;
};

inline bool Encode(IWICImagingFactory* factory, Frame& frame) {
	for (float quality : {0.70f, 0.44f, 0.24f}) {
		ComPtr<IStream> stream;
		ComPtr<IWICBitmapEncoder> encoder;
		ComPtr<IWICBitmapFrameEncode> image;
		ComPtr<IPropertyBag2> options;
		if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)) ||
			FAILED(factory->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &encoder)) ||
			FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
			FAILED(encoder->CreateNewFrame(&image, &options))) return false;
		PROPBAG2 property{};
		property.pstrName = const_cast<wchar_t*>(L"ImageQuality");
		VARIANT value{};
		value.vt = VT_R4;
		value.fltVal = quality;
		if (FAILED(options->Write(1, &property, &value)) || FAILED(image->Initialize(options.Get())) ||
			FAILED(image->SetSize(frame.width, frame.height))) return false;
		WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
		if (FAILED(image->SetPixelFormat(&format)) || format != GUID_WICPixelFormat24bppBGR ||
			FAILED(image->WritePixels(frame.height, frame.width * 3, static_cast<UINT>(frame.bytes.size()), frame.bytes.data())) ||
			FAILED(image->Commit()) || FAILED(encoder->Commit())) return false;
		STATSTG stat{};
		if (FAILED(stream->Stat(&stat, STATFLAG_NONAME))) return false;
		if (stat.cbSize.QuadPart > MaxJpeg) continue;
		std::vector<uint8_t> jpeg(static_cast<size_t>(stat.cbSize.QuadPart));
		LARGE_INTEGER start{};
		ULONG read = 0;
		if (FAILED(stream->Seek(start, STREAM_SEEK_SET, nullptr)) ||
			FAILED(stream->Read(jpeg.data(), static_cast<ULONG>(jpeg.size()), &read)) || read != jpeg.size()) return false;
		frame.bytes = std::move(jpeg);
		return true;
	}
	return false;
}

inline bool Decode(IWICImagingFactory* factory, Frame& frame) {
	if (frame.bytes.empty() || frame.bytes.size() > MaxJpeg) return false;
	ComPtr<IWICStream> stream;
	ComPtr<IWICBitmapDecoder> decoder;
	ComPtr<IWICBitmapFrameDecode> image;
	ComPtr<IWICFormatConverter> converter;
	if (FAILED(factory->CreateStream(&stream)) ||
		FAILED(stream->InitializeFromMemory(frame.bytes.data(), static_cast<DWORD>(frame.bytes.size()))) ||
		FAILED(CoCreateInstance(CLSID_WICJpegDecoder, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&decoder))) ||
		FAILED(decoder->Initialize(stream.Get(), WICDecodeMetadataCacheOnDemand)) ||
		FAILED(decoder->GetFrame(0, &image)) || FAILED(image->GetSize(&frame.width, &frame.height)) ||
		!frame.width || !frame.height || frame.width > Width || frame.height > Height ||
		FAILED(factory->CreateFormatConverter(&converter)) ||
		FAILED(converter->Initialize(image.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
			nullptr, 0, WICBitmapPaletteTypeCustom))) return false;
	std::vector<uint8_t> pixels(frame.width * frame.height * 4);
	if (FAILED(converter->CopyPixels(nullptr, frame.width * 4, static_cast<UINT>(pixels.size()), pixels.data()))) return false;
	frame.bytes = std::move(pixels);
	return true;
}

class Codec {
public:
	Codec() : worker_([this] { Run(); }) {}
	~Codec() {
		{
			std::lock_guard<std::mutex> lock(mutex_);
			stop_ = true;
		}
		ready_.notify_one();
		worker_.join();
	}

	void Submit(bool decode, Frame frame) {
		std::lock_guard<std::mutex> lock(mutex_);
		input_[decode] = std::move(frame);
		ready_.notify_one();
	}

	std::optional<Frame> Take(bool decode) {
		std::lock_guard<std::mutex> lock(mutex_);
		return std::exchange(output_[decode], std::nullopt);
	}

private:
	void Run() {
		if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return;
		{
			ComPtr<IWICImagingFactory> factory;
			if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) {
				bool decode = false;
				for (;;) {
					std::unique_lock<std::mutex> lock(mutex_);
					ready_.wait(lock, [&] { return stop_ || input_[0] || input_[1]; });
					if (stop_) break;
					if (!input_[decode]) decode = !decode;
					Frame frame = std::move(*input_[decode]);
					input_[decode].reset();
					lock.unlock();
					if (decode ? Decode(factory.Get(), frame) : Encode(factory.Get(), frame)) {
						lock.lock();
						output_[decode] = std::move(frame);
					}
					decode = !decode;
				}
			}
		}
		CoUninitialize();
	}

	std::mutex mutex_;
	std::condition_variable ready_;
	std::optional<Frame> input_[2], output_[2];
	bool stop_ = false;
	std::thread worker_;
};

inline void Fit(UINT w, UINT h, UINT& resultWidth, UINT& resultHeight) {
	if (w * Height > h * Width) {
		resultWidth = Width;
		resultHeight = (std::max)(1u, h * Width / w);
	} else {
		resultWidth = (std::max)(1u, w * Height / h);
		resultHeight = Height;
	}
}

inline bool Capture(IDirect3DDevice9* device, Frame& frame) {
	ComPtr<IDirect3DSurface9> back, scaled, cpu;
	D3DSURFACE_DESC desc{};
	D3DLOCKED_RECT pixels{};
	if (!device || FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back)) ||
		FAILED(back->GetDesc(&desc)) || !desc.Width || !desc.Height ||
		(desc.Format != D3DFMT_X8R8G8B8 && desc.Format != D3DFMT_A8R8G8B8)) return false;
	Fit(desc.Width, desc.Height, frame.width, frame.height);
	if (FAILED(device->CreateRenderTarget(frame.width, frame.height, desc.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &scaled, nullptr)) ||
		FAILED(device->StretchRect(back.Get(), nullptr, scaled.Get(), nullptr, D3DTEXF_LINEAR)) ||
		FAILED(device->CreateOffscreenPlainSurface(frame.width, frame.height, desc.Format, D3DPOOL_SYSTEMMEM, &cpu, nullptr)) ||
		FAILED(device->GetRenderTargetData(scaled.Get(), cpu.Get()))) return false;
	frame.bytes.resize(frame.width * frame.height * 3);
	if (FAILED(cpu->LockRect(&pixels, nullptr, D3DLOCK_READONLY))) return false;
	for (UINT y = 0; y < frame.height; ++y) {
		const auto* src = static_cast<const uint8_t*>(pixels.pBits) + y * pixels.Pitch;
		auto* dest = frame.bytes.data() + y * frame.width * 3;
		for (UINT x = 0; x < frame.width; ++x) memcpy(dest + x * 3, src + x * 4, 3);
	}
	cpu->UnlockRect();
	return true;
}

// Surfaces are frame-local so no D3DPOOL_DEFAULT reference survives a device reset.
inline bool Draw(IDirect3DDevice9* device, const Frame& frame, bool caption = false) {
	ComPtr<IDirect3DSurface9> back, cpu, picture;
	D3DSURFACE_DESC desc{};
	D3DLOCKED_RECT pixels{};
	if (!device || !frame.width || !frame.height || frame.bytes.size() != frame.width * frame.height * 4 ||
		FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back)) || FAILED(back->GetDesc(&desc)) ||
		(desc.Format != D3DFMT_X8R8G8B8 && desc.Format != D3DFMT_A8R8G8B8) ||
		FAILED(device->CreateOffscreenPlainSurface(frame.width, frame.height, desc.Format, D3DPOOL_SYSTEMMEM, &cpu, nullptr)) ||
		FAILED(device->CreateOffscreenPlainSurface(frame.width, frame.height, desc.Format, D3DPOOL_DEFAULT, &picture, nullptr)) ||
		FAILED(cpu->LockRect(&pixels, nullptr, 0))) return false;
	for (UINT y = 0; y < frame.height; ++y)
		memcpy(static_cast<uint8_t*>(pixels.pBits) + y * pixels.Pitch, frame.bytes.data() + y * frame.width * 4, frame.width * 4);
	cpu->UnlockRect();
	UINT w = desc.Width * 9 / 10, h = desc.Height * 8 / 10;
	if (w * frame.height > h * frame.width) w = h * frame.width / frame.height;
	else h = w * frame.height / frame.width;
	UINT top = (desc.Height - h) / 2;
	if (caption) {
		w = desc.Width * 3 / 5;
		h = desc.Height / 22;
		top = desc.Height * 93 / 100;
	}
	const LONG x = static_cast<LONG>((desc.Width - w) / 2), y = static_cast<LONG>(top);
	const RECT area{x, y, x + static_cast<LONG>(w), y + static_cast<LONG>(h)};
	return SUCCEEDED(device->UpdateSurface(cpu.Get(), nullptr, picture.Get(), nullptr)) &&
		SUCCEEDED(device->StretchRect(picture.Get(), nullptr, back.Get(), &area, D3DTEXF_LINEAR));
}

inline Frame MakeCaption(const std::wstring& text) {
	Frame frame;
	frame.width = 640;
	frame.height = 42;
	BITMAPINFO info{};
	info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	info.bmiHeader.biWidth = frame.width;
	info.bmiHeader.biHeight = -static_cast<LONG>(frame.height);
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = 32;
	HDC dc = CreateCompatibleDC(nullptr);
	if (!dc) return {};
	void* pixels = nullptr;
	HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
	if (bitmap) {
		const auto oldBitmap = SelectObject(dc, bitmap);
		const auto font = CreateFontW(-26, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
			OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
		const auto oldFont = SelectObject(dc, font);
		RECT bounds{0, 0, static_cast<LONG>(frame.width), static_cast<LONG>(frame.height)};
		FillRect(dc, &bounds, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
		SetBkMode(dc, TRANSPARENT);
		SetTextColor(dc, RGB(255, 226, 108));
		DrawTextW(dc, text.c_str(), -1, &bounds, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
		GdiFlush();
		const auto* bytes = static_cast<const uint8_t*>(pixels);
		frame.bytes.assign(bytes, bytes + frame.width * frame.height * 4);
		SelectObject(dc, oldFont);
		DeleteObject(font);
		SelectObject(dc, oldBitmap);
		DeleteObject(bitmap);
	}
	DeleteDC(dc);
	return frame;
}
}
