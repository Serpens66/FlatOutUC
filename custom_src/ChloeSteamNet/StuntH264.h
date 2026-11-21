#pragma once

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>
#include <codecapi.h>
#include <wmcodecdsp.h>

namespace stunt_video {
inline uint8_t Byte(int n) { return static_cast<uint8_t>((std::clamp)(n, 0, 255)); }

inline std::vector<uint8_t> ToNv12(const Frame& frame) {
	const UINT w = frame.width, h = frame.height;
	std::vector<uint8_t> out(w * h * 3 / 2);
	for (UINT y = 0; y < h; y += 2) for (UINT x = 0; x < w; x += 2) {
		int u = 0, v = 0;
		for (UINT j = 0; j < 2; ++j) for (UINT i = 0; i < 2; ++i) {
			const UINT pos = (y + j) * w + x + i;
			const int b = frame.bytes[pos * 3], g = frame.bytes[pos * 3 + 1], r = frame.bytes[pos * 3 + 2];
			out[pos] = Byte(((47 * r + 157 * g + 16 * b + 128) >> 8) + 16);
			u += -26 * r - 87 * g + 112 * b;
			v += 112 * r - 102 * g - 10 * b;
		}
		const UINT chroma = w * h + y / 2 * w + x;
		out[chroma] = Byte(((u + 512) >> 10) + 128);
		out[chroma + 1] = Byte(((v + 512) >> 10) + 128);
	}
	return out;
}

inline void FromNv12(Frame& frame, const uint8_t* bytes, UINT stride) {
	const UINT w = frame.width, h = frame.height;
	frame.bytes.resize(w * h * 4);
	for (UINT y = 0; y < h; ++y) for (UINT x = 0; x < w; ++x) {
		const int luma = 298 * (bytes[y * stride + x] - 16);
		const UINT uv = stride * h + y / 2 * stride + (x & ~1u);
		const int u = bytes[uv] - 128, v = bytes[uv + 1] - 128;
		auto* pixel = frame.bytes.data() + (y * w + x) * 4;
		pixel[0] = Byte((luma + 541 * u + 128) >> 8);
		pixel[1] = Byte((luma - 55 * u - 136 * v + 128) >> 8);
		pixel[2] = Byte((luma + 459 * v + 128) >> 8);
		pixel[3] = 255;
	}
}

class H264 {
public:
	explicit H264(bool encode) : encode_(encode) {}

	bool Transform(Frame& frame) {
		if (!frame.width || !frame.height || frame.width > 1280 || frame.height > 720 ||
			(frame.width & 1) || (frame.height & 1) || frame.quality >= std::size(stunt_stream::Levels)) return false;
		if (!mft_ || event_ != frame.event || stream_ != frame.subscription || width_ != frame.width ||
			height_ != frame.height || quality_ != frame.quality) {
			if (!encode_ && !frame.key) return false;
			mft_.Reset();
			width_ = frame.width; height_ = frame.height; quality_ = frame.quality;
			event_ = frame.event; stream_ = frame.subscription;
			if (!Open()) { mft_.Reset(); return false; }
		}

		if (encode_ && frame.bytes.size() != width_ * height_ * 3) return false;
		auto nv12 = encode_ ? ToNv12(frame) : std::vector<uint8_t>{};
		const auto& data = encode_ ? nv12 : frame.bytes;
		ComPtr<IMFSample> sample;
		if (!Sample(static_cast<DWORD>(data.size()), sample, data.data())) return false;
		const auto duration = 10000000LL / stunt_stream::Levels[quality_].fps;
		sample->SetSampleTime(static_cast<LONGLONG>(frame.sequence) * duration);
		sample->SetSampleDuration(duration);
		if (encode_ && frame.key) {
			VARIANT enabled{};
			enabled.vt = VT_UI4;
			enabled.ulVal = 1;
			SetCodec(CODECAPI_AVEncVideoForceKeyFrame, enabled);
		}
		if (FAILED(mft_->ProcessInput(0, sample.Get(), 0))) return false;
		auto output = Pull();
		if (!output) return false;
		LONGLONG position = 0;
		if (SUCCEEDED(output->GetSampleTime(&position)) && position != static_cast<LONGLONG>(frame.sequence) * duration)
			return false;
		ComPtr<IMFMediaBuffer> buffer;
		if (FAILED(output->ConvertToContiguousBuffer(&buffer))) return false;
		BYTE* bytes = nullptr;
		DWORD length = 0;
		if (FAILED(buffer->Lock(&bytes, nullptr, &length))) return false;
		bool success = false;
		if (encode_) {
			UINT32 clean = 0;
			output->GetUINT32(MFSampleExtension_CleanPoint, &clean);
			frame.key = clean != 0;
			frame.bytes.assign(bytes, bytes + length);
			success = length && length <= stunt_stream::MaxPicture;
		} else {
			const UINT aligned = (height_ + 15) & ~15u;
			const UINT storageHeight = length >= stride_ * aligned * 3 / 2 ? aligned : height_;
			if (stride_ >= width_ && length >= stride_ * storageHeight * 3 / 2) {
				if (storageHeight != height_) {
					nv12.assign(bytes, bytes + stride_ * height_);
					nv12.insert(nv12.end(), bytes + stride_ * storageHeight,
						bytes + stride_ * storageHeight + stride_ * height_ / 2);
					bytes = nv12.data();
				}
				FromNv12(frame, bytes, stride_);
				success = true;
			}
		}
		buffer->Unlock();
		return success;
	}

private:
	static bool Sample(DWORD size, ComPtr<IMFSample>& sample, const uint8_t* data = nullptr) {
		ComPtr<IMFMediaBuffer> buffer;
		if (FAILED(MFCreateSample(&sample)) || FAILED(MFCreateMemoryBuffer(size, &buffer))) return false;
		if (data) {
			BYTE* dst = nullptr;
			if (FAILED(buffer->Lock(&dst, nullptr, nullptr))) return false;
			memcpy(dst, data, size);
			buffer->Unlock();
			if (FAILED(buffer->SetCurrentLength(size))) return false;
		}
		return SUCCEEDED(sample->AddBuffer(buffer.Get()));
	}

	void SetCodec(const GUID& property, VARIANT value) {
		ComPtr<ICodecAPI> codec;
		if (FAILED(mft_.As(&codec))) return;
		codec->SetValue(&property, &value);
	}

	bool Open() {
		if (FAILED(CoCreateInstance(encode_ ? CLSID_CMSH264EncoderMFT : CLSID_CMSH264DecoderMFT, nullptr,
			CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&mft_)))) return false;
		ComPtr<IMFAttributes> attributes;
		if (SUCCEEDED(mft_->GetAttributes(&attributes))) attributes->SetUINT32(MF_LOW_LATENCY, TRUE);
		VARIANT lowLatency{};
		lowLatency.vt = VT_BOOL;
		lowLatency.boolVal = VARIANT_TRUE;
		SetCodec(CODECAPI_AVLowLatencyMode, lowLatency);
		const auto& q = stunt_stream::Levels[quality_];
		ComPtr<IMFMediaType> raw, compressed;
		if (FAILED(MFCreateMediaType(&raw)) || FAILED(MFCreateMediaType(&compressed))) return false;
		for (auto* t : {raw.Get(), compressed.Get()}) {
			t->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
			t->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
			MFSetAttributeSize(t, MF_MT_FRAME_SIZE, width_, height_);
			MFSetAttributeRatio(t, MF_MT_FRAME_RATE, q.fps, 1);
			MFSetAttributeRatio(t, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
		}
		raw->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
		compressed->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
		compressed->SetUINT32(MF_MT_AVG_BITRATE, q.videoKbps * 1000);
		compressed->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_Base);
		if (encode_) {
			if (FAILED(mft_->SetOutputType(0, compressed.Get(), 0)) || FAILED(mft_->SetInputType(0, raw.Get(), 0))) return false;
		} else if (FAILED(mft_->SetInputType(0, compressed.Get(), 0)) || !DecoderOutput()) return false;
		mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
		mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
		return true;
	}

	bool DecoderOutput() {
		for (DWORD i = 0; ; ++i) {
			ComPtr<IMFMediaType> type;
			if (FAILED(mft_->GetOutputAvailableType(0, i, &type))) return false;
			GUID format{};
			if (FAILED(type->GetGUID(MF_MT_SUBTYPE, &format)) || format != MFVideoFormat_NV12) continue;
			UINT w = 0, h = 0;
			if (FAILED(MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &w, &h)) ||
				w < width_ || w > ((width_ + 15) & ~15u) ||
				h < height_ || h > ((height_ + 15) & ~15u)) continue;
			stride_ = w;
			type->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride_);
			return SUCCEEDED(mft_->SetOutputType(0, type.Get(), 0));
		}
	}

	ComPtr<IMFSample> Pull() {
		for (unsigned attempt = 0; attempt < 2; ++attempt) {
			MFT_OUTPUT_STREAM_INFO info{};
			if (FAILED(mft_->GetOutputStreamInfo(0, &info))) return {};
			ComPtr<IMFSample> sample;
			if (!(info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) &&
				!Sample((std::max)(info.cbSize, static_cast<DWORD>(width_ * height_ * 2)), sample)) return {};
			MFT_OUTPUT_DATA_BUFFER data{};
			data.pSample = sample.Get();
			DWORD status = 0;
			const HRESULT hr = mft_->ProcessOutput(0, 1, &data, &status);
			if (data.pEvents) data.pEvents->Release();
			if (!sample) sample.Attach(data.pSample);
			if (hr == MF_E_TRANSFORM_STREAM_CHANGE && !encode_ && DecoderOutput()) continue;
			if (FAILED(hr)) return {};
			return sample;
		}
		return {};
	}

	bool encode_;
	ComPtr<IMFTransform> mft_;
	uint64_t event_ = 0, stream_ = 0;
	UINT width_ = 0, height_ = 0, stride_ = 0;
	unsigned quality_ = 0;
};
}
