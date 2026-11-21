#pragma once

#include <algorithm>
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
    uint16_t attempt, modelKey, skin;
    uint16_t version;
    std::array<float, 3> position;
    std::array<float, 4> rotation;
};
struct CarPacket {
    MotionPacket motion;
    std::array<float, 3> velocity;
};
constexpr unsigned kDriverJoints = 11;
struct JointPacket {
    std::array<float, 3> position;
    std::array<float, 4> rotation;
};
struct DriverPacket {
    MotionPacket motion;
    uint16_t character, skin;
    std::array<JointPacket, kDriverJoints> joints;
};
#pragma pack(pop)
static_assert(sizeof(MotionPacket) == 52);
static_assert(sizeof(CarPacket) == 64);
static_assert(sizeof(DriverPacket) == 364);

inline bool IsMotionSize(size_t size) {
    return size == sizeof(MotionPacket) || size == sizeof(CarPacket) || size == sizeof(DriverPacket);
}

struct DriverPose {
    int character = -1;
    uint16_t skin = 0;
    std::array<Pose, kDriverJoints> joints{};
};

inline bool ReadDriverPose(const DriverPacket& flight, DriverPose& driver) {
    if (flight.character >= 6 || !flight.skin || flight.skin > 32) return false;
    driver.character = flight.character;
    driver.skin = flight.skin;
    for (size_t i = 0; i < flight.joints.size(); ++i) {
        const auto& joint = flight.joints[i];
        auto& target = driver.joints[i];
        if (!ReadPose(target, joint.position.data(), joint.rotation.data()) ||
            std::abs(target.position.x) > 32768 || std::abs(target.position.y) > 32768 ||
            std::abs(target.position.z) > 32768) return false;
        const auto relative = target.position - driver.joints[0].position;
        if (Dot(relative, relative) > 16.0f) return false;
    }
    return true;
}

class MotionTrack {
public:
    const MotionPacket* Latest() const { return count_ ? &At(count_ - 1).packet : nullptr; }

    bool SampleLive(uint64_t now, Pose& pose, DriverPose* driver = nullptr) {
        if (!Fresh(now) || (playing_ && now < lastPlayed_)) return false;
        const double newest = Latest()->ms;
        const double oldest = At(0).packet.ms;
        constexpr uint64_t buffer = 100;
        if (!playing_) {
            if (now - firstReceived_ < buffer) return Sample(oldest, now, pose, driver);
            cursor_ = (std::max)(oldest, newest - buffer);
            playing_ = true;
        } else {
            // Packet arrivals can bunch up or pause. Advance the same
            // playback clock and adjust its rate gradually towards the buffer.
            const double elapsed = static_cast<double>(now - lastPlayed_);
            const double target = newest + (std::min)(now - lastReceived_, buffer) - buffer;
            const double rate = (std::clamp)(1.0 + (target - cursor_ - elapsed) / 250.0, 0.9, 1.1);
            const double limit = newest + (At(count_ - 1).hasVelocity ? kPredictionMs : 0);
            cursor_ = (std::clamp)(cursor_ + elapsed * rate, oldest, limit);
        }
        lastPlayed_ = now;
        if (!Sample(cursor_, now, pose, driver)) return false;
        if (At(count_ - 1).driver.character < 0) pose.position = pose.position + Correction();
        return true;
    }

    void Begin(uint64_t event, uint16_t attempt) {
        event_ = event;
        attempt_ = attempt;
        count_ = 0;
        next_ = 0;
        playing_ = false;
        correction_ = {};
    }

    bool Receive(const void* bytes, size_t size, uint64_t now) {
        if (!bytes || !IsMotionSize(size)) return false;
        MotionPacket packet;
        memcpy(&packet, bytes, sizeof(packet));
        const unsigned version = size == sizeof(CarPacket) ? 3 : size == sizeof(DriverPacket) ? 2 : 1;
        if (packet.version != version ||
            !event_ || !attempt_ || packet.event != event_ || packet.attempt != attempt_ ||
            !packet.sequence || !packet.modelKey || !packet.skin) return false;
        const auto* newest = Latest();
        if (newest && (packet.sequence <= newest->sequence || packet.ms <= newest->ms ||
            packet.modelKey != newest->modelKey || packet.skin != newest->skin || now < lastReceived_)) return false;
        Pose pose;
        DriverPose driver;
        if (size == sizeof(DriverPacket)) {
            DriverPacket flight;
            memcpy(&flight, bytes, sizeof(flight));
            if (!ReadDriverPose(flight, driver)) return false;
            // After ejection the driver can be far from its car or the car
            // transform can already be reset. Use the actual driver root.
            pose = driver.joints[0];
        } else if (!ReadPose(pose, packet.position.data(), packet.rotation.data()) ||
            std::abs(pose.position.x) > 32768 || std::abs(pose.position.y) > 32768 ||
            std::abs(pose.position.z) > 32768) return false;
        Vec3 velocity{};
        const bool hasVelocity = size == sizeof(CarPacket);
        if (hasVelocity) {
            CarPacket car;
            memcpy(&car, bytes, sizeof(car));
            velocity = {car.velocity[0], car.velocity[1], car.velocity[2]};
            const float speedSquared = Dot(velocity, velocity);
            if (!std::isfinite(speedSquared) || speedSquared > 200.0f * 200.0f) return false;
        }
        // Reconcile the visible position if a packet arrives after playback
        // has already advanced beyond the last known transform.
        const bool reconcile = hasVelocity && newest && playing_ && cursor_ > newest->ms;
        Vec3 predicted{};
        if (reconcile) predicted = Predict(At(count_ - 1), cursor_) + Correction();
        if (!count_) firstReceived_ = now;
        samples_[next_] = {packet, pose, driver, velocity, hasVelocity};
        next_ = (next_ + 1) % samples_.size();
        if (count_ < samples_.size()) ++count_;
        lastReceived_ = now;
        if (reconcile) {
            Pose revised;
            if (Sample(cursor_, now, revised)) {
                correction_ = predicted - revised.position;
                correctionStart_ = cursor_;
            }
        }
        return true;
    }

    bool Sample(double ms, uint64_t now, Pose& pose, DriverPose* driver = nullptr) const {
        if (!Fresh(now) || ms < At(0).packet.ms) return false;
        const auto& newest = At(count_ - 1);
        if (ms >= newest.packet.ms) {
            pose = newest.pose;
            pose.position = Predict(newest, ms);
            if (driver) *driver = newest.driver;
            return true;
        }
        size_t upper = 1;
        while (At(upper).packet.ms <= ms) ++upper;
        const auto& first = At(upper - 1);
        const auto& last = At(upper);
        const bool sameVisual = first.driver.character == last.driver.character && first.driver.skin == last.driver.skin;
        const float t = static_cast<float>((ms - first.packet.ms) / (last.packet.ms - first.packet.ms));
        // Keep the last car transform until the driver frame becomes current.
        pose = sameVisual ? Blend(first.pose, last.pose, t) : first.pose;
        if (driver) {
            *driver = first.driver;
            if (sameVisual && first.driver.character >= 0) {
                for (unsigned i = 0; i < kDriverJoints; ++i)
                    driver->joints[i] = Blend(first.driver.joints[i], last.driver.joints[i], t);
            }
        }
        return true;
    }

private:
    static constexpr double kPredictionMs = 80;
    struct SamplePoint { MotionPacket packet; Pose pose; DriverPose driver; Vec3 velocity; bool hasVelocity; };
    static Vec3 Predict(const SamplePoint& frame, double ms) {
        const auto elapsed = (std::min)(ms - frame.packet.ms, kPredictionMs);
        return frame.pose.position + frame.velocity * static_cast<float>(elapsed / 1000.0);
    }
    Vec3 Correction() const {
        return correction_ * static_cast<float>((std::max)(0.0, 1.0 - (cursor_ - correctionStart_) / 100.0));
    }
    const SamplePoint& At(size_t index) const {
        return samples_[(next_ + samples_.size() - count_ + index) % samples_.size()];
    }
    bool Fresh(uint64_t now) const { return count_ && now >= lastReceived_ && now - lastReceived_ <= 300; }

    uint64_t event_ = 0, firstReceived_ = 0, lastReceived_ = 0, lastPlayed_ = 0;
    uint16_t attempt_ = 0;
    size_t count_ = 0, next_ = 0;
    bool playing_ = false;
    double cursor_ = 0;
    double correctionStart_ = 0;
    Vec3 correction_{};
    std::array<SamplePoint, 12> samples_{};
};

}
