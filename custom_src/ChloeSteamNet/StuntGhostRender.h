#pragma once

#include <windows.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include "StuntGhostMotion.h"

namespace stunt_ghost {
using Matrix = std::array<float, 16>;

inline const char* TexturePath(uintptr_t texture) {
    const auto path = texture + 0x20;
    return *reinterpret_cast<const unsigned*>(path + 0x14) < 16 ?
        reinterpret_cast<const char*>(path) : *reinterpret_cast<const char* const*>(path);
}

inline bool TextureReady(uintptr_t texture) {
    return texture && *reinterpret_cast<const uintptr_t*>(texture + 0x4C);
}

inline bool UploadTexture(uintptr_t texture) {
    if (texture && *reinterpret_cast<const unsigned*>(texture + 0x10) == 0) {
        // The archive loader registers textures. During a live stunt it
        // no longer runs the loading-screen pass that uploads them.
        const auto device = *reinterpret_cast<void**>(0x84665C);
        const auto upload = reinterpret_cast<void (__thiscall*)(void*, uintptr_t, const char*, unsigned)>(0x6278A0);
        upload(device, texture, TexturePath(texture), *reinterpret_cast<const unsigned*>(texture + 0x14));
    }
    return TextureReady(texture);
}

inline Matrix WorldMatrix(const Pose& pose) {
    Matrix result{};
    const Vec3 basis[] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (unsigned i = 0; i < 3; ++i) {
        const auto axis = Rotate(pose, basis[i]);
        result[i * 4] = axis.x;
        result[i * 4 + 1] = axis.y;
        result[i * 4 + 2] = axis.z;
    }
    result[12] = pose.position.x;
    result[13] = pose.position.y;
    result[14] = pose.position.z;
    result[15] = 1;
    return result;
}

// Compose the model or bind-pose frame with the world transform.
inline Matrix PlacePart(const Matrix& local, const Matrix& world) {
    Matrix placed = local;
    for (unsigned row = 0; row < 4; ++row) {
        for (unsigned component = 0; component < 3; ++component) {
            placed[row * 4 + component] = row == 3 ? world[12 + component] : 0.0f;
            for (unsigned axis = 0; axis < 3; ++axis)
                placed[row * 4 + component] += local[row * 4 + axis] * world[axis * 4 + component];
        }
    }
    return placed;
}

}
