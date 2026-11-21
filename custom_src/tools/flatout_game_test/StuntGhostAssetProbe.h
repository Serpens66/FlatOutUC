#pragma once

#include "../../ChloeSteamNet/StuntGhostRender.h"
#include <cstring>
#include <vector>

namespace stunt_ghost {
// Prototype using the game's stand-alone BGM loader and its regular material
// sorting pass. All resources belong to a single disposable probe process.
class AssetProbe {
public:
    bool Load() {
        if (attempted_) return !parts_.empty();
        attempted_ = true;
        auto& materialCache = *reinterpret_cast<std::array<uintptr_t, 3>*>(0x849F34);
        auto& primaryFolder = *reinterpret_cast<std::array<char, 264>*>(0x845B78);
        auto& secondaryFolder = *reinterpret_cast<std::array<char, 264>*>(0x845C80);
        const auto originalCache = materialCache;
        const auto originalPrimary = primaryFolder;
        const auto originalSecondary = secondaryFolder;
        materialCache = {};
        strcpy_s(primaryFolder.data(), primaryFolder.size(), "data/cars/car_1/");
        strcpy_s(secondaryFolder.data(), secondaryFolder.size(), "data/cars/shared/");
        resource_[9] = reinterpret_cast<uintptr_t>(LoadMaterial);
        reinterpret_cast<void (__stdcall*)(uintptr_t*, const char*)>(0x5802D0)(resource_.data(), "data/cars/car_1/body.bgm");
        privateMaterials_ = materialCache;
        materialCache = originalCache;
        primaryFolder = originalPrimary;
        secondaryFolder = originalSecondary;
        if (!resource_[6] || !resource_[2] || resource_[2] > 128) return false;
        for (unsigned i = 0; i < resource_[2]; ++i) {
            const auto part = resource_[6] + i * 0x120;
            const auto model = *reinterpret_cast<const uintptr_t*>(part + 0xF0);
            if (!model) continue;
            const auto first = *reinterpret_cast<const uintptr_t* const*>(model + 0x30);
            const auto last = *reinterpret_cast<const uintptr_t* const*>(model + 0x34);
            if (!first || !last || last <= first || last - first > 64) continue;
            const Matrix& local = *reinterpret_cast<const Matrix*>(part + 0x40);
            for (auto surface = first; surface != last; ++surface)
                if (*surface) parts_.push_back({*surface, local});
        }
        return !parts_.empty();
    }

    bool Attach() {
        if (attached_) return true;
        if (owner_) return false;
        auto* site = reinterpret_cast<unsigned char*>(kQueue);
        if (memcmp(site, kPrologue, sizeof(kPrologue)) != 0) return false;
        auto* trampoline = static_cast<unsigned char*>(VirtualAlloc(nullptr, sizeof(kPrologue) + 5,
            MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        if (!trampoline) return false;
        memcpy(trampoline, kPrologue, sizeof(kPrologue));
        WriteJump(trampoline + sizeof(kPrologue), kQueue + sizeof(kPrologue));
        DWORD protection;
        if (!VirtualProtect(trampoline, sizeof(kPrologue) + 5, PAGE_EXECUTE_READ, &protection) ||
            !VirtualProtect(site, sizeof(kPrologue), PAGE_EXECUTE_READWRITE, &protection)) {
            VirtualFree(trampoline, 0, MEM_RELEASE);
            return false;
        }
        FlushInstructionCache(GetCurrentProcess(), trampoline, sizeof(kPrologue) + 5);
        original_ = reinterpret_cast<Queue>(trampoline);
        owner_ = this;
        WriteJump(site, reinterpret_cast<uintptr_t>(Render));
        FlushInstructionCache(GetCurrentProcess(), site, sizeof(kPrologue));
        DWORD ignored;
        VirtualProtect(site, sizeof(kPrologue), protection, &ignored);
        attached_ = true;
        return true;
    }

    void Detach() {
        Hide();
        if (!attached_) return;
        auto* site = reinterpret_cast<unsigned char*>(kQueue);
        int32_t relative;
        memcpy(&relative, site + 1, sizeof(relative));
        if (*site != 0xE9 || kQueue + 5 + relative != reinterpret_cast<uintptr_t>(Render)) return;
        DWORD protection;
        if (!VirtualProtect(site, sizeof(kPrologue), PAGE_EXECUTE_READWRITE, &protection)) return;
        memcpy(site, kPrologue, sizeof(kPrologue));
        FlushInstructionCache(GetCurrentProcess(), site, sizeof(kPrologue));
        DWORD ignored;
        VirtualProtect(site, sizeof(kPrologue), protection, &ignored);
        VirtualFree(reinterpret_cast<void*>(original_), 0, MEM_RELEASE);
        original_ = nullptr;
        owner_ = nullptr;
        attached_ = false;
    }

    void Show(uintptr_t localCar, const Matrix& target) {
        car_ = localCar;
        target_ = target;
        visible_ = true;
    }
    void Hide() { visible_ = false; }
    unsigned Parts() const { return static_cast<unsigned>(parts_.size()); }
    unsigned Frames() const { return frames_; }
    unsigned Calls() const { return calls_; }
    bool PaintTechniqueBound() const { return paintTechniqueBound_; }
    static unsigned PaintedSurfaces() { return paintedSurfaces_; }

private:
    static constexpr uintptr_t kQueue = 0x5AF910;
    static constexpr unsigned char kPrologue[]{0x55, 0x8B, 0xEC, 0xB8, 0x18, 0x10, 0, 0};
    struct Part { uintptr_t surface; Matrix local; };
    struct Item { uintptr_t surface; const Matrix* world; uint32_t sort; uint32_t flags; };
    static_assert(sizeof(Item) == 0x10);
    struct Bucket { Item* begin; Item* end; Item* capacity; uint32_t unused; };
    using Queue = void (__stdcall*)(Bucket*, int);

    static void WriteJump(unsigned char* site, uintptr_t target) {
        const auto relative = static_cast<int32_t>(target - reinterpret_cast<uintptr_t>(site) - 5);
        site[0] = 0xE9;
        memcpy(site + 1, &relative, sizeof(relative));
    }

    static uintptr_t __cdecl LoadMaterial(char* raw, uintptr_t context) {
        auto* texture = *reinterpret_cast<const unsigned*>(raw + 0x34) < 16 ? raw + 0x20 : *reinterpret_cast<char**>(raw + 0x20);
        const bool paint = texture && _stricmp(texture, "skin1.tga") == 0;
        const bool untextured = !texture || !texture[0];
        std::array<char, 24> originalString{};
        if (untextured) {
            memcpy(originalString.data(), raw + 0x20, originalString.size());
            strcpy_s(raw + 0x20, 16, "common.tga");
            *reinterpret_cast<unsigned*>(raw + 0x30) = 10;
            *reinterpret_cast<unsigned*>(raw + 0x34) = 15;
        }
        if (paint) texture[4] = '2';
        const auto material = reinterpret_cast<uintptr_t (__cdecl*)(char*, uintptr_t)>(0x489300)(raw, context);
        if (untextured) memcpy(raw + 0x20, originalString.data(), originalString.size());
        if (paint) {
            texture[4] = '1';
            if (material && *reinterpret_cast<const uintptr_t*>(material)) {
                ++paintedSurfaces_;
                paintedMaterial_ = material;
            }
        }
        return material;
    }

    static void __stdcall Render(Bucket* queue, int pass) {
        auto& probe = *owner_;
        const auto original = probe.original_;
        if (probe.visible_) ++probe.calls_;
        if (!queue || pass != 8 || !probe.visible_) {
            original(queue, pass);
            return;
        }
        auto& bucket = queue[pass];
        const auto saved = bucket;
        if (!saved.begin || !saved.end || saved.end <= saved.begin ||
            static_cast<size_t>(saved.end - saved.begin) + probe.parts_.size() > 768) {
            original(queue, pass);
            return;
        }
        const auto begin = *reinterpret_cast<const uintptr_t* const*>(probe.car_ + 0x1C8C);
        const auto end = *reinterpret_cast<const uintptr_t* const*>(probe.car_ + 0x1C90);
        if (!begin || !end || end <= begin || end - begin > 512) {
            original(queue, pass);
            return;
        }
        const auto localCarItem = std::find_if(saved.begin, saved.end, [&](const Item& item) {
            return item.surface && std::find(begin, end, *reinterpret_cast<const uintptr_t*>(item.surface)) != end;
        });
        if (localCarItem == saved.end) {
            original(queue, pass);
            return;
        }
        const auto paint = std::find_if(begin, end, [](uintptr_t material) {
            return material && *reinterpret_cast<const unsigned*>(material + 0x94) == 5;
        });
        if (paintedMaterial_ && paint != end) {
            // The generic asset loader binds the chosen diffuse map. Match the
            // vehicle renderer's paint technique while preserving that map.
            memcpy(reinterpret_cast<void*>(paintedMaterial_ + 4), reinterpret_cast<const void*>(*paint + 4), 0x70);
            memcpy(reinterpret_cast<void*>(paintedMaterial_ + 0x94), reinterpret_cast<const void*>(*paint + 0x94), 8);
            probe.paintTechniqueBound_ = true;
        }
        const Matrix identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        probe.matrices_.resize(probe.parts_.size());
        probe.items_.assign(saved.begin, saved.end);
        for (unsigned i = 0; i < probe.parts_.size(); ++i) {
            const auto& part = probe.parts_[i];
            probe.matrices_[i] = PlacePart(part.local, identity, probe.target_);
            probe.items_.push_back({part.surface, &probe.matrices_[i], localCarItem->sort, localCarItem->flags});
        }
        bucket.begin = probe.items_.data();
        bucket.end = bucket.capacity = bucket.begin + probe.items_.size();
        original(queue, pass);
        const bool consumed = bucket.end == bucket.begin;
        bucket = saved;
        if (consumed) bucket.end = bucket.begin;
        ++probe.frames_;
    }

    inline static AssetProbe* owner_ = nullptr;
    inline static unsigned paintedSurfaces_ = 0;
    inline static uintptr_t paintedMaterial_ = 0;
    bool attempted_ = false, attached_ = false, visible_ = false;
    bool paintTechniqueBound_ = false;
    unsigned frames_ = 0, calls_ = 0;
    uintptr_t car_ = 0;
    Queue original_ = nullptr;
    std::array<uintptr_t, 11> resource_{};
    std::array<uintptr_t, 3> privateMaterials_{};
    Matrix target_{};
    std::vector<Part> parts_;
    std::vector<Matrix> matrices_;
    std::vector<Item> items_;
};
}
