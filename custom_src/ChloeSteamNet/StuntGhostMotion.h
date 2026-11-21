#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace stunt_ghost {
struct Vec3 {
    float x, y, z;
    Vec3 operator+(Vec3 b) const { return {x + b.x, y + b.y, z + b.z}; }
    Vec3 operator-(Vec3 b) const { return {x - b.x, y - b.y, z - b.z}; }
    Vec3 operator*(float k) const { return {x * k, y * k, z * k}; }
};

inline float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 Cross(Vec3 a, Vec3 b) { return {a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x}; }
inline Vec3 Unit(Vec3 a) { return a * (1.0f / std::sqrt(Dot(a, a))); }

struct Pose {
    Vec3 position;
    std::array<float, 4> rotation;
};

inline bool ReadPose(Pose& pose, const float* p, const float* q) {
    for (int i = 0; i < 3; ++i) if (!std::isfinite(p[i])) return false;
    float magnitude = 0;
    for (int i = 0; i < 4; ++i) magnitude += q[i] * q[i];
    if (!std::isfinite(magnitude) || magnitude < 0.5f || magnitude > 1.5f) return false;
    pose.position = {p[0], p[1], p[2]};
    const float scale = 1.0f / std::sqrt(magnitude);
    for (int i = 0; i < 4; ++i) pose.rotation[i] = q[i] * scale;
    return true;
}

inline Pose Blend(const Pose& a, const Pose& b, float t) {
    float dot = 0;
    for (int i = 0; i < 4; ++i) dot += a.rotation[i] * b.rotation[i];
    const float sign = dot < 0 ? -1.0f : 1.0f;
    Pose result{a.position * (1.0f - t) + b.position * t, {}};
    float norm = 0;
    for (int i = 0; i < 4; ++i) {
        result.rotation[i] = a.rotation[i] * (1.0f - t) + b.rotation[i] * (sign * t);
        norm += result.rotation[i] * result.rotation[i];
    }
    const float scale = 1.0f / std::sqrt(norm);
    for (float& component : result.rotation) component *= scale;
    return result;
}

inline Vec3 Rotate(const Pose& pose, Vec3 offset) {
    const auto& q = pose.rotation;
    const Vec3 axis{q[0], q[1], q[2]};
    const Vec3 cross = Cross(axis, offset);
    return offset + (cross * q[3] + Cross(axis, cross)) * 2.0f;
}

// Steam supplies the sender identity, so the payload carries only event and
// attempt identity, the visual asset, and one pose.
#pragma pack(push, 1)
struct MotionPacket {
    uint64_t event;
    uint32_t sequence, ms;
    uint16_t attempt, carFolder, skin;
    uint16_t version;
    std::array<float, 3> position;
    std::array<float, 4> rotation;
};
#pragma pack(pop)
static_assert(sizeof(MotionPacket) == 52);

class MotionTrack {
public:
    void Begin(uint64_t event, uint16_t attempt) {
        event_ = event;
        attempt_ = attempt;
        count_ = 0;
    }

    bool Receive(const void* bytes, size_t size, uint64_t now) {
        if (!bytes || size != sizeof(MotionPacket)) return false;
        MotionPacket packet;
        memcpy(&packet, bytes, size);
        if (packet.version != 1 || !event_ || !attempt_ || packet.event != event_ || packet.attempt != attempt_ ||
            !packet.sequence || !packet.carFolder || !packet.skin) return false;
        const auto& newest = samples_[1].packet;
        if (count_ && (packet.sequence <= newest.sequence || packet.ms <= newest.ms ||
            packet.carFolder != newest.carFolder || packet.skin != newest.skin || now < lastReceived_)) return false;
        Pose pose;
        if (!ReadPose(pose, packet.position.data(), packet.rotation.data()) ||
            std::abs(pose.position.x) > 32768 || std::abs(pose.position.y) > 32768 ||
            std::abs(pose.position.z) > 32768) return false;
        samples_[0] = samples_[1];
        samples_[1] = {packet, pose};
        if (count_ < 2) ++count_;
        lastReceived_ = now;
        return true;
    }

    bool Sample(uint32_t ms, uint64_t now, Pose& pose) const {
        if (!count_ || now < lastReceived_ || now - lastReceived_ > 300) return false;
        const auto& last = samples_[1];
        if (ms >= last.packet.ms) {
            pose = last.pose;
            return true;
        }
        const auto& first = samples_[0];
        if (count_ < 2 || ms < first.packet.ms) return false;
        const float t = static_cast<float>(ms - first.packet.ms) / (last.packet.ms - first.packet.ms);
        pose = Blend(first.pose, last.pose, t);
        return true;
    }

private:
    struct SamplePoint { MotionPacket packet; Pose pose; };
    uint64_t event_ = 0, lastReceived_ = 0;
    uint16_t attempt_ = 0;
    unsigned count_ = 0;
    std::array<SamplePoint, 2> samples_{};
};

}
