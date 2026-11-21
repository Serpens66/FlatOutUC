#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <vector>

namespace stunt_stream {
constexpr uint64_t LeaseMs = 2000;
constexpr size_t FragmentBytes = 1050;
constexpr size_t MaxPicture = 256 * 1024;
constexpr unsigned MaxViewers = 7;

struct Quality {
	unsigned width, height, fps, videoKbps, audioKbps;
};
constexpr Quality Levels[] = {
	{ 426, 240,  8,  160, 32 },
	{ 640, 360, 12,  380, 64 },
	{ 960, 540, 15,  800, 64 },
	{1280, 720, 15, 1350, 64 },
	{1280, 720, 24, 2200, 64 }
};

enum Type : uint8_t { Watch = 1, Leave, Route, Picture, Presented, Recover, Feedback, Sound };
#pragma pack(push, 1)
struct Packet {
	uint64_t event = 0, driver = 0, route = 0, viewer = 0;
	uint32_t sequence = 0, stamp = 0, size = 0;
	uint16_t fragment = 0, width = 0, height = 0;
	uint8_t type = 0, quality = 0, key = 0;
};
#pragma pack(pop)
static_assert(sizeof(Packet) == 53);

inline bool Valid(const Packet& p, size_t bytes) {
	if (!p.event || !p.driver || p.quality >= std::size(Levels) || p.key > 1) return false;
	if (p.type == Picture || p.type == Sound) {
		const size_t maximum = p.type == Picture ? MaxPicture : FragmentBytes;
		if (!p.route || !p.sequence || !p.size || p.size > maximum || p.viewer) return false;
		if (p.type == Sound && (p.fragment || p.width || p.height || p.key)) return false;
		const auto& q = Levels[p.quality];
		if (p.type == Picture && (!p.width || !p.height || (p.width & 1) || (p.height & 1) ||
			p.width > q.width || p.height > q.height)) return false;
		const size_t offset = static_cast<size_t>(p.fragment) * FragmentBytes;
		return offset < p.size && bytes == (std::min)(FragmentBytes, p.size - offset);
	}
	if (p.type == Route) return p.route && bytes % sizeof(uint64_t) == 0 && bytes <= MaxViewers * sizeof(uint64_t);
	return bytes == 0 && ((p.type == Watch || p.type == Leave) ? p.viewer != 0 :
		p.route && (p.type == Presented || p.type == Recover || p.type == Feedback));
}

struct Assembly {
	Packet packet{};
	uint64_t began = 0;
	std::vector<uint8_t> data, parts;
	size_t remaining = 0;

	bool Add(const Packet& incoming, const uint8_t* payload, size_t length, uint64_t now) {
		if (packet.sequence && incoming.sequence < packet.sequence) return false;
		if (incoming.sequence != packet.sequence) {
			packet = incoming;
			began = now;
			data.resize(packet.size);
			parts.assign((packet.size + FragmentBytes - 1) / FragmentBytes, 0);
			remaining = parts.size();
		}
		if (now - began > 250 || !remaining || incoming.size != packet.size ||
			incoming.stamp != packet.stamp || incoming.key != packet.key ||
			incoming.quality != packet.quality || incoming.width != packet.width || incoming.height != packet.height ||
			incoming.fragment >= parts.size() ||
			parts[incoming.fragment]) return false;
		parts[incoming.fragment] = 1;
		std::copy_n(payload, length, data.data() + incoming.fragment * FragmentBytes);
		return --remaining == 0;
	}
};

struct Chain {
	uint64_t generation = 0;
	std::vector<uint64_t> viewers;

	uint64_t Next(uint64_t driver, uint64_t node) const {
		if (viewers.empty()) return 0;
		if (node == driver) return viewers.front();
		const auto it = std::find(viewers.begin(), viewers.end(), node);
		return it == viewers.end() || std::next(it) == viewers.end() ? 0 : *std::next(it);
	}

	uint64_t Previous(uint64_t driver, uint64_t node) const {
		const auto it = std::find(viewers.begin(), viewers.end(), node);
		return it == viewers.end() ? 0 : it == viewers.begin() ? driver : *std::prev(it);
	}
};

class Adaptation {
public:
	unsigned level = 2;
	void Observe(bool congested, uint64_t now) {
		if (congested) {
			stableSince_ = now;
			retryAfter_ = now + 20000;
			if (level && now - changed_ >= 750) { --level; changed_ = now; }
		} else if (!stableSince_) stableSince_ = now;
		else if (level + 1 < std::size(Levels) && now >= retryAfter_ && now - stableSince_ >= RampMs) {
			++level;
			stableSince_ = changed_ = now;
		}
	}
private:
	static constexpr uint64_t RampMs = 2500;
	uint64_t stableSince_ = 0, changed_ = 0, retryAfter_ = 0;
};
}
