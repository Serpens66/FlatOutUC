#pragma once

#include "StuntGhostMotion.h"
#include <map>

namespace stunt_ghost {
class Session {
public:
    void Begin(uint64_t event) {
        event_ = event;
        tracks_.clear();
    }

    bool Receive(uint64_t sender, const void* data, size_t size, uint64_t now) {
        if (!event_ || !sender || !data || !IsMotionSize(size)) return false;
        MotionPacket packet;
        memcpy(&packet, data, sizeof(packet));
        if (packet.event != event_ || !packet.attempt || packet.attempt > 10) return false;
        const auto found = tracks_.find(sender);
        const auto* latest = found == tracks_.end() ? nullptr : found->second.Latest();
        if (latest && packet.attempt < latest->attempt) return false;
        if (latest && packet.attempt == latest->attempt) return found->second.Receive(data, size, now);
        if (!latest && tracks_.size() >= 7) return false;
        MotionTrack track;
        track.Begin(event_, packet.attempt);
        if (!track.Receive(data, size, now)) return false;
        tracks_.insert_or_assign(sender, track);
        return true;
    }

    bool Sample(uint64_t sender, uint16_t model, uint16_t skin, uint64_t now, Pose& pose) {
        MotionPacket visual;
        return Sample(sender, now, pose, visual) && visual.modelKey == model && visual.skin == skin;
    }

    bool Sample(uint64_t sender, uint64_t now, Pose& pose, MotionPacket& visual, DriverPose* driver = nullptr) {
        const auto found = tracks_.find(sender);
        if (found == tracks_.end() || !found->second.SampleLive(now, pose, driver)) return false;
        visual = *found->second.Latest();
        return true;
    }

private:
    uint64_t event_ = 0;
    std::map<uint64_t, MotionTrack> tracks_;
};
}
