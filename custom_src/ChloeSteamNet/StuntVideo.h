#pragma once

#include <d3d9.h>
#include <wrl/client.h>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include "StuntStream.h"

namespace stunt_video {
using Microsoft::WRL::ComPtr;
struct Frame {
	uint64_t event = 0, subscription = 0;
	uint32_t sequence = 0;
	UINT width = 0, height = 0;
	std::vector<uint8_t> bytes;
	uint32_t stamp = 0;
	uint8_t quality = 0;
	bool key = false;
};

}
#include "StuntH264.h"
namespace stunt_video {

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

	void Clear() {
		std::lock_guard<std::mutex> lock(mutex_);
		++generation_;
		for (auto& frame : input_) frame.reset();
		for (auto& frame : output_) frame.reset();
		ready_.notify_one();
	}

private:
	void Run() {
		if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return;
		const bool mediaFoundation = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE));
		if (mediaFoundation) {
			std::optional<H264> encoder, decoder;
			uint64_t generation = 0;
			bool decode = false;
			for (;;) {
				std::unique_lock<std::mutex> lock(mutex_);
				ready_.wait(lock, [&] { return stop_ || generation != generation_ || input_[0] || input_[1]; });
				if (stop_) break;
				if (generation != generation_) {
					generation = generation_;
					lock.unlock();
					encoder.reset();
					decoder.reset();
					continue;
				}
				if (!input_[decode]) decode = !decode;
				Frame frame = std::move(*input_[decode]);
				input_[decode].reset();
				lock.unlock();
				auto& transform = decode ? decoder : encoder;
				if (!transform) transform.emplace(!decode);
				if (transform->Transform(frame)) {
					lock.lock();
					if (generation == generation_) output_[decode] = std::move(frame);
				}
				decode = !decode;
			}
		}
		if (mediaFoundation) MFShutdown();
		CoUninitialize();
	}

	std::mutex mutex_;
	std::condition_variable ready_;
	std::optional<Frame> input_[2], output_[2];
	bool stop_ = false;
	uint64_t generation_ = 0;
	std::thread worker_;
};

inline bool Capture(IDirect3DDevice9* device, Frame& frame) {
	ComPtr<IDirect3DSurface9> back, scaled, cpu;
	D3DSURFACE_DESC desc{};
	D3DLOCKED_RECT pixels{};
	if (!device || FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back)) ||
		FAILED(back->GetDesc(&desc)) || !desc.Width || !desc.Height ||
		(desc.Format != D3DFMT_X8R8G8B8 && desc.Format != D3DFMT_A8R8G8B8)) return false;
	const auto& q = stunt_stream::Levels[frame.quality];
	if (desc.Width * q.height > desc.Height * q.width) {
		frame.width = q.width;
		frame.height = desc.Height * q.width / desc.Width;
	} else {
		frame.width = desc.Width * q.height / desc.Height;
		frame.height = q.height;
	}
	frame.width &= ~1u;
	frame.height &= ~1u;
	if (!frame.width || !frame.height) return false;
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
