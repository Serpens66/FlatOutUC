#pragma once

#include <windows.h>
#include <algorithm>
#include <array>
#include <cstdint>

namespace stunt_ghost {
using Matrix = std::array<float, 16>;
using Vector = std::array<float, 3>;

// Move a rigid render part from the source car's world frame to the ghost's.
// Wheels and panels retain the local offsets chosen by the native renderer.
inline Matrix PlacePart(const Matrix& part, const Matrix& source, const Matrix& target) {
    Matrix placed = part;
    for (unsigned row = 0; row < 4; ++row) {
        Vector relative{};
        for (unsigned axis = 0; axis < 3; ++axis)
            relative[axis] = part[row * 4 + axis] - (row == 3 ? source[12 + axis] : 0.0f);
        Vector local{};
        for (unsigned axis = 0; axis < 3; ++axis)
            for (unsigned component = 0; component < 3; ++component)
                local[axis] += relative[component] * source[axis * 4 + component];
        for (unsigned component = 0; component < 3; ++component) {
            placed[row * 4 + component] = row == 3 ? target[12 + component] : 0.0f;
            for (unsigned axis = 0; axis < 3; ++axis)
                placed[row * 4 + component] += local[axis] * target[axis * 4 + component];
        }
    }
    return placed;
}

class NativeRenderer {
public:
    bool Attach(void* renderer) {
        if (!renderer) return false;
        auto table = *reinterpret_cast<void***>(renderer);
        auto& slot = table[0xE8 / 4];
        if (slot == reinterpret_cast<void*>(Draw) && owner_ == this) return true;
        if (owner_ || reinterpret_cast<uintptr_t>(slot) != 0x608F50 ||
            reinterpret_cast<uintptr_t>(table[0xC8 / 4]) != 0x60BFD0) return false;
        DWORD old;
        if (!VirtualProtect(&slot, sizeof(slot), PAGE_READWRITE, &old)) return false;
        setWorld_ = reinterpret_cast<SetWorld>(table[0xC8 / 4]);
        drawMesh_ = reinterpret_cast<DrawMesh>(slot);
        slot_ = &slot;
        owner_ = this;
        slot = reinterpret_cast<void*>(Draw);
        DWORD ignored;
        VirtualProtect(&slot, sizeof(slot), old, &ignored);
        return true;
    }

    void SetTarget(uintptr_t car, const Matrix& target) {
        car_ = car;
        target_ = target;
    }
    void ClearTarget() { car_ = 0; }
    void Detach() {
        ClearTarget();
        if (owner_ != this || *slot_ != reinterpret_cast<void*>(Draw)) return;
        DWORD old;
        if (!VirtualProtect(slot_, sizeof(*slot_), PAGE_READWRITE, &old)) return;
        *slot_ = reinterpret_cast<void*>(drawMesh_);
        owner_ = nullptr;
        DWORD ignored;
        VirtualProtect(slot_, sizeof(*slot_), old, &ignored);
    }
    unsigned Draws() const { return draws_; }

private:
    using SetWorld = void (__thiscall*)(void*, const float*);
    using DrawMesh = void (__thiscall*)(void*, const uintptr_t*, unsigned);

    static bool IsCurrentLocalCar(uintptr_t car) {
        const auto flow = *reinterpret_cast<const uintptr_t*>(0x9298FAC);
        if (!flow || *reinterpret_cast<const int*>(flow + 0x4A4) != 2) return false;
        const auto host = *reinterpret_cast<const uintptr_t*>(flow + 0x2820);
        if (!host) return false;
        const auto players = *reinterpret_cast<const uintptr_t* const*>(host + 0x14);
        return players && players != *reinterpret_cast<const uintptr_t* const*>(host + 0x18) &&
            players[0] && *reinterpret_cast<const uintptr_t*>(players[0] + 0x294) == car;
    }

    static void __fastcall Draw(void* renderer, void*, const uintptr_t* mesh, unsigned flags) {
        auto& ghost = *owner_;
        ghost.drawMesh_(renderer, mesh, flags);
        const auto* native = static_cast<const uint8_t*>(renderer);
        if (!ghost.car_ || !mesh || native[0x2FC] || !IsCurrentLocalCar(ghost.car_)) return;
        auto begin = *reinterpret_cast<const uintptr_t**>(ghost.car_ + 0x1C8C);
        auto end = *reinterpret_cast<const uintptr_t**>(ghost.car_ + 0x1C90);
        if (!begin || !end || end < begin || end - begin > 512 || std::find(begin, end, mesh[0]) == end) return;

        // Only submit graphics. The original car and its materials, PlayerHost
        // list and collision bodies stay intact.
        const Matrix world = *reinterpret_cast<const Matrix*>(native + 0x300);
        const Matrix source = *reinterpret_cast<const Matrix*>(ghost.car_ + 0x1C0);
        const auto placed = PlacePart(world, source, ghost.target_);
        ghost.setWorld_(renderer, placed.data());
        ghost.drawMesh_(renderer, mesh, flags);
        ghost.setWorld_(renderer, world.data());
        ++ghost.draws_;
    }

    inline static NativeRenderer* owner_ = nullptr;
    SetWorld setWorld_ = nullptr;
    DrawMesh drawMesh_ = nullptr;
    void** slot_ = nullptr;
    uintptr_t car_ = 0;
    Matrix target_{};
    unsigned draws_ = 0;
};
}
