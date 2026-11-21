#pragma once

#include "StuntGhostRender.h"

namespace stunt_ghost {
static void DriverRenderHook();
static __declspec(naked) void __stdcall DrawSkinnedQueue(void*) {
    __asm {
        push esi
        mov esi, [esp + 8]
        mov eax, 05B1A30h
        call eax
        pop esi
        ret 4
    }
}

class DriverAsset {
public:
    void Show(size_t slot, const DriverPose& driver, float opacity) {
        if (character_ != driver.character || skin_ != driver.skin) {
            Unload();
            character_ = driver.character;
            skin_ = driver.skin;
            ready_ = Load();
        }
        if (!ready_ || !Install()) return;

        const auto rig = resource_[5];
        for (unsigned i = 0; i < kDriverJoints; ++i) {
            const auto bone = *reinterpret_cast<const unsigned*>(rig + 0x2140 + i * 0x14);
            const auto& bind = *reinterpret_cast<const Matrix*>(rig + 0x2F0 + bone * 0x40);
            matrices_[bone] = PlacePart(bind, WorldMatrix(driver.joints[i]));
        }
        const auto material = *reinterpret_cast<const uintptr_t*>(surface_);
        reinterpret_cast<std::array<float, 4>*>(material + 0x50)->fill(opacity);
        *reinterpret_cast<uint32_t*>(material + 0x60) = 1;
        visible_ = true;
        assets_[slot] = this;
    }

    void Hide() { visible_ = false; }

    void Unload() {
        if (character_ < 0) return;
        Hide();
        for (auto& asset : assets_) if (asset == this) asset = nullptr;
        if (resource_[5]) reinterpret_cast<void (__stdcall*)(uintptr_t*)>(0x5795E0)(resource_.data());
        resource_ = {};
        surface_ = 0;
        ready_ = false;
        character_ = -1;
        skin_ = 0;
    }

private:
    friend void DriverRenderHook();
    struct Item {
        uintptr_t surface;
        const void* palette;
        uint32_t depth, flags;
    };
    static_assert(sizeof(Item) == 0x10);
    struct Bucket { Item* begin; Item* end; Item* capacity; };
    struct Palette { unsigned count; const Matrix* matrices; };
    static_assert(sizeof(Bucket) == 12 && sizeof(Palette) == 8);

    bool Load() {
        static constexpr const char* names[]{"male", "female", "fo1male", "fo1female", "ctd_male", "ctd_female"};
        if (character_ < 0 || character_ >= _countof(names) || !skin_) return false;
        const auto name = names[character_];
        char rigPath[48], modelPath[64], textureName[40], texturePath[72];
        sprintf_s(rigPath, "data/drivers/%s.ini", name);
        sprintf_s(modelPath, "data/drivers/ragdoll/%s.bgm", name);
        sprintf_s(textureName, "%s_%u.tga", name, skin_);
        sprintf_s(texturePath, "data/drivers/ragdoll/%s_%u.dds", name, skin_);
        const auto exists = reinterpret_cast<bool (__cdecl*)(const char*, char)>(0x5A5B80);
        if (!exists(rigPath, 0) || !exists(modelPath, 0) || !exists(texturePath, 0)) return false;

        const auto rig = reinterpret_cast<uintptr_t (__cdecl*)(const char*)>(0x598E20)(rigPath);
        resource_[5] = rig;
        if (!rig || *reinterpret_cast<const unsigned*>(rig + 0x24) != kDriverJoints) return false;
        unsigned seen = 0;
        for (unsigned i = 0; i < kDriverJoints; ++i) {
            const auto bone = *reinterpret_cast<const unsigned*>(rig + 0x2140 + i * 0x14);
            if (bone >= kDriverJoints || (seen & (1u << bone))) return false;
            seen |= 1u << bone;
        }
        // Load the native skinned mesh and material without constructing a
        // driver, physics bodies, or any joints in the simulation.
        auto& primaryFolder = *reinterpret_cast<std::array<char, 264>*>(0x845B78);
        auto& secondaryFolder = *reinterpret_cast<std::array<char, 264>*>(0x845C80);
        const auto primary = primaryFolder;
        const auto secondary = secondaryFolder;
        const char* model = modelPath;
        const char* texture = textureName;
        auto* resource = resource_.data();
        __asm {
            push texture
            push resource
            mov eax, model
            mov edx, 057A620h
            call edx
        }
        primaryFolder = primary;
        secondaryFolder = secondary;
        const auto mesh = resource_[0x30 / 4];
        if (!mesh) return false;
        const auto meshModel = *reinterpret_cast<const uintptr_t*>(mesh + 0xF0);
        if (!meshModel) return false;
        const auto begin = *reinterpret_cast<const uintptr_t* const*>(meshModel + 0x30);
        const auto end = *reinterpret_cast<const uintptr_t* const*>(meshModel + 0x34);
        if (!begin || end != begin + 1 || !*begin) return false;
        surface_ = *begin;
        const auto material = *reinterpret_cast<const uintptr_t*>(surface_);
        return material && UploadTexture(*reinterpret_cast<const uintptr_t*>(material));
    }

    static bool Install() {
        if (installed_) return true;
        constexpr uintptr_t address = 0x4F4BE1;
        auto* site = reinterpret_cast<uint8_t*>(address);
        int32_t relative;
        memcpy(&relative, site + 1, sizeof(relative));
        if (*site != 0xE8 || address + 5 + relative != 0x5B1A30) return false;
        DWORD old, ignored;
        if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old)) return false;
        relative = static_cast<int32_t>(reinterpret_cast<uintptr_t>(DriverRenderHook) - address - 5);
        memcpy(site + 1, &relative, sizeof(relative));
        FlushInstructionCache(GetCurrentProcess(), site, 5);
        VirtualProtect(site, 5, old, &ignored);
        installed_ = true;
        return true;
    }

    static void __stdcall Render(void* queue) {
        auto& bucket = *reinterpret_cast<Bucket*>(static_cast<uint8_t*>(queue) + 0x110);
        const auto end = bucket.end;
        if (bucket.begin && end >= bucket.begin && end <= bucket.capacity) {
            for (auto* asset : assets_) {
                if (!asset || !asset->visible_ || bucket.end == bucket.capacity) continue;
                *bucket.end++ = {asset->surface_, &asset->palette_, 0, 0};
            }
        }
        DrawSkinnedQueue(queue);
        if (bucket.end != bucket.begin) bucket.end = end;
    }

    inline static bool installed_ = false;
    inline static std::array<DriverAsset*, 7> assets_{};
    std::array<uintptr_t, 0x860 / 4> resource_{};
    alignas(16) std::array<Matrix, kDriverJoints> matrices_{};
    const Palette palette_{kDriverJoints, matrices_.data()};
    uintptr_t surface_ = 0;
    int character_ = -1;
    uint16_t skin_ = 0;
    bool ready_ = false, visible_ = false;
};

static __declspec(naked) void DriverRenderHook() {
    __asm {
        push esi
        call DriverAsset::Render
        ret
    }
}
}
