#pragma once

#include <atomic>
#include <mmsystem.h>

#ifdef STUNT_STREAM_OPUS
#include <opus.h>
#endif

namespace stunt_audio {
constexpr unsigned PacketMs = 20, Rate = 48000, Samples = Rate * PacketMs / 1000;

#ifdef STUNT_STREAM_OPUS
// FMOD Ex 4.10.06 uses this 92-byte x86 DSP descriptor. The game builds
// the same layout for StateVariablePhilter at 0x00419670.
struct DspState { void* instance; };
using Read = int (__stdcall*)(DspState*, float*, float*, unsigned, int, int);
struct DspDescription {
	char name[32];
	unsigned version;
	int channels;
	void *create, *release, *reset;
	Read read;
	void* setPosition;
	int parameters;
	void *description, *setParameter, *getParameter, *configure;
	int width, height;
	void* userData;
};
static_assert(sizeof(DspDescription) == 0x5c && offsetof(DspDescription, read) == 0x34);

template<class T> T Export(HMODULE module, const char* name) {
	return reinterpret_cast<T>(reinterpret_cast<uintptr_t>(GetProcAddress(module, name)));
}
using DspUserData = int (__stdcall*)(void*, void**);
inline DspUserData readUserData = nullptr;

inline std::array<void*, 3> EffectGroups() {
	const auto* channels = reinterpret_cast<void* const*>(0x737698);
	const auto* names = reinterpret_cast<const char*>(0x737718);
	const int count = *reinterpret_cast<const int*>(0x846494);
	std::array<void*, 3> groups{};
	if (count < 0 || count > 32) return groups;
	for (int index = 0; index < count; ++index) {
		if (!strncmp(names + index * 36, "PLR_ENG", 8)) groups[0] = channels[index];
		if (!strncmp(names + index * 36, "SURFACE", 8)) groups[1] = channels[index];
	}
	groups[2] = *reinterpret_cast<void**>(0x7640F4); // generic game effects group "no_dsp"
	return groups;
}

struct Tap {
	static constexpr unsigned Capacity = 8192;
	std::array<float, Capacity * 2> ring{};
	std::atomic<unsigned> written{0}, read{0};
	std::atomic<bool> enabled{false};
	void* dsp = nullptr;

	static int __stdcall Mix(DspState* state, float* input, float* output, unsigned length, int inChannels, int outChannels) {
		if (input && output && inChannels == outChannels && inChannels > 0 && input != output)
			memcpy(output, input, static_cast<size_t>(length) * inChannels * sizeof(float));
		else if (output && outChannels > 0 && inChannels != outChannels) {
			for (unsigned i = 0; i < length; ++i) for (int c = 0; c < outChannels; ++c)
				output[i * outChannels + c] = input && c < inChannels ? input[i * inChannels + c] : 0;
		}
		void* context = nullptr;
		if (!input || inChannels <= 0 || !state || !readUserData ||
			readUserData(state->instance, &context) || !context) return 0;
		auto& tap = *static_cast<Tap*>(context);
		if (!tap.enabled.load(std::memory_order_relaxed)) return 0;
		const unsigned next = tap.written.load(std::memory_order_relaxed);
		const unsigned room = Capacity - (next - tap.read.load(std::memory_order_acquire));
		const unsigned count = (std::min)(room, length);
		for (unsigned i = 0; i < count; ++i) {
			const auto dst = ((next + i) % Capacity) * 2;
			tap.ring[dst] = input[i * inChannels];
			tap.ring[dst + 1] = input[i * inChannels + (inChannels > 1)];
		}
		tap.written.store(next + count, std::memory_order_release);
		return 0;
	}

	unsigned Buffered() const { return written.load(std::memory_order_acquire) - read.load(std::memory_order_relaxed); }
};

class Capture {
public:
	bool Start(void* system, const std::array<void*, 3>& groups) {
		const auto module = GetModuleHandleW(L"fmodex.dll");
		if (!module || !system) return false;
		using Call = int (__stdcall*)(void*);
		const auto version = Export<int (__stdcall*)(void*, unsigned*)>(module, "FMOD_System_GetVersion");
		const auto format = Export<int (__stdcall*)(void*, int*, int*, int*, int*, int*, int*)>(module, "FMOD_System_GetSoftwareFormat");
		const auto create = Export<int (__stdcall*)(void*, DspDescription*, void**)>(module, "FMOD_System_CreateDSP");
		const auto attach = Export<int (__stdcall*)(void*, void*)>(module, "FMOD_ChannelGroup_AddDSP");
		const auto setData = Export<int (__stdcall*)(void*, void*)>(module, "FMOD_DSP_SetUserData");
		const auto userData = Export<DspUserData>(module, "FMOD_DSP_GetUserData");
		remove_ = Export<Call>(module, "FMOD_DSP_Remove");
		release_ = Export<Call>(module, "FMOD_DSP_Release");
		unsigned found = 0;
		int unused = 0, fmt = 0, channels = 0, resampler = 0, bits = 0;
		if (!version || version(system, &found) || found != 0x00041006 ||
			!format || format(system, &rate_, &fmt, &channels, &unused, &resampler, &bits) ||
			(rate_ != 44100 && rate_ != 48000) || !create || !attach || !setData || !userData ||
			!remove_ || !release_) return false;
		if (!readUserData) readUserData = userData;
		for (size_t i = 0; i < groups.size(); ++i) {
			auto& tap = taps_[i];
			if (!groups[i] || std::find(groups.begin(), groups.begin() + i, groups[i]) != groups.begin() + i) continue;
			if (tap.dsp) continue;
			DspDescription description{};
			strcpy_s(description.name, "Chloe stunt effects");
			description.version = 0x00010000;
			description.read = Tap::Mix;
			if (create(system, &description, &tap.dsp) || !tap.dsp) { tap.dsp = nullptr; continue; }
			if (setData(tap.dsp, &tap) || attach(groups[i], tap.dsp)) {
				release_(tap.dsp);
				tap.dsp = nullptr;
				continue;
			}
			tap.read.store(0);
			tap.written.store(0);
			tap.enabled.store(true);
		}
		return std::any_of(taps_.begin(), taps_.end(), [](const Tap& tap) { return tap.enabled.load(); });
	}

	void Stop() {
		for (auto& tap : taps_) {
			tap.enabled.store(false);
			if (tap.dsp) {
				remove_(tap.dsp);
				if (!release_(tap.dsp)) tap.dsp = nullptr;
			}
		}
	}

	bool ReadBlock(std::array<float, Samples * 2>& result, uint32_t& stamp, uint64_t now) {
		const unsigned count = rate_ * PacketMs / 1000;
		unsigned maximum = 0;
		for (const auto& tap : taps_) if (tap.enabled.load()) maximum = (std::max)(maximum, tap.Buffered());
		if (!count || maximum < count) return false;
		result.fill(0);
		for (auto& tap : taps_) {
			if (!tap.enabled.load()) continue;
			const unsigned available = tap.Buffered();
			if (available < count) continue;
			const unsigned start = tap.read.load() + (available > count * 4 ? available - count * 2 : 0);
			for (unsigned i = 0; i < Samples; ++i) {
				const unsigned index = i * count / Samples;
				const float mix = static_cast<float>(i * count % Samples) / Samples;
				const unsigned a = ((start + index) % Tap::Capacity) * 2;
				const unsigned b = ((start + (std::min)(index + 1, count - 1)) % Tap::Capacity) * 2;
				for (unsigned c = 0; c < 2; ++c) result[2 * i + c] += tap.ring[a + c] * (1 - mix) + tap.ring[b + c] * mix;
			}
			tap.read.store(start + count, std::memory_order_release);
		}
		stamp = static_cast<uint32_t>(now - (std::min)(maximum, count * 4) * 1000 / rate_);
		return true;
	}

private:
	std::array<Tap, 3> taps_{};
	int rate_ = 0;
	int (__stdcall* remove_)(void*) = nullptr;
	int (__stdcall* release_)(void*) = nullptr;
};

class Encoder {
public:
	~Encoder() { Stop(); }
	void Stop() { if (encoder_) opus_encoder_destroy(std::exchange(encoder_, nullptr)); }
	std::vector<uint8_t> Encode(std::array<float, Samples * 2>& pcm, unsigned channels) {
		if (!encoder_ || channels != channels_) {
			Stop();
			int error = 0;
			encoder_ = opus_encoder_create(Rate, channels, OPUS_APPLICATION_RESTRICTED_LOWDELAY, &error);
			channels_ = channels;
			if (error || !encoder_) return {};
			opus_encoder_ctl(encoder_, OPUS_SET_BITRATE(channels == 2 ? 64000 : 32000));
			opus_encoder_ctl(encoder_, OPUS_SET_VBR(0));
		}
		if (channels == 1) for (unsigned i = 0; i < Samples; ++i) pcm[i] = (pcm[2 * i] + pcm[2 * i + 1]) * 0.5f;
		std::vector<uint8_t> data(320);
		const int size = opus_encode_float(encoder_, pcm.data(), Samples, data.data(), static_cast<opus_int32>(data.size()));
		if (size <= 0) return {};
		data.resize(size);
		return data;
	}
private:
	OpusEncoder* encoder_ = nullptr;
	unsigned channels_ = 0;
};

class Output {
public:
	~Output() { Stop(); }
	void Stop() {
		if (device_) {
			waveOutReset(device_);
			for (auto& slot : slots_) if (slot.header.dwFlags & WHDR_PREPARED)
				waveOutUnprepareHeader(device_, &slot.header, sizeof(WAVEHDR));
			waveOutClose(std::exchange(device_, nullptr));
			slots_ = {};
		}
		if (decoder_) opus_decoder_destroy(std::exchange(decoder_, nullptr));
	}
	bool Play(const uint8_t* bytes, size_t size, unsigned channels) {
		if (opus_packet_get_nb_samples(bytes, static_cast<opus_int32>(size), Rate) != Samples) return false;
		if (!decoder_ || channels != channels_) {
			Stop();
			int error = 0;
			decoder_ = opus_decoder_create(Rate, channels, &error);
			if (error || !decoder_) return false;
			channels_ = channels;
			const WAVEFORMATEX format{WAVE_FORMAT_PCM, static_cast<WORD>(channels), Rate, Rate * channels * 2,
				static_cast<WORD>(channels * 2), 16, 0};
			if (waveOutOpen(&device_, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
				Stop();
				return false;
			}
		}
		for (auto& slot : slots_) {
			if ((slot.header.dwFlags & WHDR_PREPARED) && !(slot.header.dwFlags & WHDR_DONE)) continue;
			if (slot.header.dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(device_, &slot.header, sizeof(WAVEHDR));
			if (opus_decode(decoder_, bytes, static_cast<opus_int32>(size), slot.samples.data(), Samples, 0) != Samples)
				return false;
			slot.header = {};
			slot.header.lpData = reinterpret_cast<char*>(slot.samples.data());
			slot.header.dwBufferLength = Samples * channels * sizeof(opus_int16);
			return waveOutPrepareHeader(device_, &slot.header, sizeof(WAVEHDR)) == MMSYSERR_NOERROR &&
				waveOutWrite(device_, &slot.header, sizeof(WAVEHDR)) == MMSYSERR_NOERROR;
		}
		return false;
	}

private:
	struct Slot { WAVEHDR header{}; std::array<opus_int16, Samples * 2> samples{}; };
	std::array<Slot, 3> slots_{};
	HWAVEOUT device_ = nullptr;
	OpusDecoder* decoder_ = nullptr;
	unsigned channels_ = 0;
};
#endif
}
