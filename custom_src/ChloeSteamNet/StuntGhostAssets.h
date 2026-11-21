#pragma once

#include "StuntGhostRender.h"
#include <cstring>
#include <vector>

namespace stunt_ghost {
// A standalone car body submitted to the native material sorting pass.
class CarAsset {
public:
    bool Load(unsigned folder = 1, unsigned skin = 2) {
        if (attempted_) return !parts_.empty();
        attempted_ = true;
        if ((!folder || (folder > 99 && (folder < 201 || folder > 299))) || !skin || skin > 255) return false;
        char folderPath[48], bodyPath[64];
        sprintf_s(folderPath, "data/cars/car_%u/", folder);
        sprintf_s(bodyPath, "%sbody.bgm", folderPath);
        const auto exists = reinterpret_cast<bool (__cdecl*)(const char*, char)>(0x5A5B80);
        if (!exists(bodyPath, 0)) return false;
        for (const char* suffix : {".dds", "_damaged.dds", "_damaged_normal.dds", "_normal.dds", "_specular.dds"}) {
            char texturePath[72];
            sprintf_s(texturePath, "%sskin%u%s", folderPath, skin, suffix);
            if (!exists(texturePath, 0)) return false;
        }
        skin_ = skin;
        paintedSurfaces_ = 0;
        paintedMaterials_.clear();
#ifdef FLATOUT_GHOST_RENDER_TEST
        paintTechniqueBound_ = false;
        frames_ = calls_ = 0;
#endif
        auto& materialCache = *reinterpret_cast<std::array<uintptr_t, 3>*>(0x849F34);
        auto& primaryFolder = *reinterpret_cast<std::array<char, 264>*>(0x845B78);
        auto& secondaryFolder = *reinterpret_cast<std::array<char, 264>*>(0x845C80);
        const auto originalCache = materialCache;
        const auto originalPrimary = primaryFolder;
        const auto originalSecondary = secondaryFolder;
        materialCache = {};
        strcpy_s(primaryFolder.data(), primaryFolder.size(), folderPath);
        strcpy_s(secondaryFolder.data(), secondaryFolder.size(), "data/cars/shared/");
        const auto textureCache = *reinterpret_cast<void**>(0x9298FCC);
        const auto loadTexture = reinterpret_cast<uintptr_t (__stdcall*)(void*, const char*, int, int)>(0x5A6F00);
        char textureName[40];
        sprintf_s(textureName, "skin%u.tga", skin);
        diffuse_ = loadTexture(textureCache, textureName, 2, 2);
        sprintf_s(textureName, "skin%u_damaged.tga", skin);
        damaged_ = loadTexture(textureCache, textureName, 2, 0x41);
        sprintf_s(textureName, "skin%u_specular.tga", skin);
        specular_ = loadTexture(textureCache, textureName, 2, 0);
        resource_[9] = reinterpret_cast<uintptr_t>(LoadMaterial);
        wheelResource_[9] = resource_[9];
        loading_ = this;
        const auto load = reinterpret_cast<void (__stdcall*)(uintptr_t*, const char*)>(0x5802D0);
        load(resource_.data(), bodyPath);
        char wheelPath[64];
        sprintf_s(wheelPath, "%stire.bgm", folderPath);
        if (exists(wheelPath, 0)) load(wheelResource_.data(), wheelPath);
        else if (exists("data/cars/shared/tire.bgm", 0)) load(wheelResource_.data(), "data/cars/shared/tire.bgm");
        loading_ = nullptr;
        privateMaterials_ = materialCache;
        materialCache = originalCache;
        primaryFolder = originalPrimary;
        secondaryFolder = originalSecondary;
        if (!resource_[6] || !resource_[2] || resource_[2] > 128) return false;
        for (unsigned i = 0; i < resource_[2]; ++i) {
            const auto part = resource_[6] + i * 0x120;
            const char* name = NodeName(part);
            const auto length = name ? strlen(name) : 0;
            // The live vehicle submits these to the shadow pass. Adding
            // them to the ghost's color pass brightens the whole chassis.
            if (length >= 7 && _stricmp(name + length - 7, "_shadow") == 0) continue;
            AddPart(part, *reinterpret_cast<const Matrix*>(part + 0x40));
        }
        AddWheels();
        if (!UploadTexture(diffuse_) || !UploadTexture(damaged_) || !UploadTexture(specular_)) {
            parts_.clear();
            return false;
        }
        for (const auto material : materials_) {
            UploadTexture(*reinterpret_cast<const uintptr_t*>(material));
        }
        return !parts_.empty();
    }

    bool Unload() {
        if (!attempted_) return true;
        Detach();
        if (attached_) return false;
        auto& slots = *reinterpret_cast<std::array<uintptr_t, 4>*>(0x846504);
        if (std::any_of(slots.begin(), slots.end(), [](uintptr_t slot) { return slot != 0; })) return false;
        ReleaseModel(wheelResource_, slots, {});
        ReleaseModel(resource_, slots, privateMaterials_);
        privateMaterials_ = {};
        parts_.clear();
        materials_.clear();
        matrices_.clear();
        items_.clear();
        paintedMaterials_.clear();
        diffuse_ = damaged_ = specular_ = 0;
#ifdef FLATOUT_GHOST_RENDER_TEST
        wheels_ = 0;
#endif
        attempted_ = false;
        return true;
    }

    bool Attach() {
        if (attached_) return true;
        const auto free = std::find(assets_.begin(), assets_.end(), nullptr);
        if (free == assets_.end()) return false;
        if (original_) {
            *free = this;
            attached_ = true;
            return true;
        }
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
        *free = this;
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
        const auto active = std::find(assets_.begin(), assets_.end(), this);
        if (active == assets_.end()) return;
        if (std::count(assets_.begin(), assets_.end(), nullptr) != 6) {
            *active = nullptr;
            attached_ = false;
            return;
        }
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
        *active = nullptr;
        attached_ = false;
    }

    void Show(uintptr_t localCar, const Matrix& target, float opacity = 0.70f) {
        car_ = localCar;
        target_ = target;
        opacity_ = opacity;
        visible_ = true;
    }
    void Hide() { visible_ = false; }
    unsigned Parts() const { return static_cast<unsigned>(parts_.size()); }
    unsigned PaintedSurfaces() const { return paintedSurfaces_; }
#ifdef FLATOUT_GHOST_RENDER_TEST
    unsigned Frames() const { return frames_; }
    unsigned Calls() const { return calls_; }
    bool PaintTechniqueBound() const { return paintTechniqueBound_; }
    unsigned Wheels() const { return wheels_; }
#endif

private:
    static constexpr uintptr_t kQueue = 0x5AF910;
    static constexpr unsigned char kPrologue[]{0x55, 0x8B, 0xEC, 0xB8, 0x18, 0x10, 0, 0};
    struct Part { uintptr_t surface; Matrix local; };
    struct Item { uintptr_t surface; const Matrix* world; uint32_t sort; uint32_t flags; };
    static_assert(sizeof(Item) == 0x10);
    struct Bucket { Item* begin; Item* end; Item* capacity; uint32_t unused; };
    using Queue = void (__stdcall*)(Bucket*, int);
    using Resource = std::array<uintptr_t, 11>;

    static const char* NodeName(uintptr_t node) {
        const auto name = reinterpret_cast<const char*>(node + 0xD0);
        return *reinterpret_cast<const unsigned*>(node + 0xE4) < 16 ? name :
            *reinterpret_cast<const char* const*>(name);
    }

    bool AddPart(uintptr_t part, const Matrix& local) {
        const auto model = *reinterpret_cast<const uintptr_t*>(part + 0xF0);
        if (!model) return false;
        const auto first = *reinterpret_cast<const uintptr_t* const*>(model + 0x30);
        const auto last = *reinterpret_cast<const uintptr_t* const*>(model + 0x34);
        if (!first || !last || last <= first || last - first > 64) return false;
        const auto before = parts_.size();
        for (auto surface = first; surface != last; ++surface) {
            if (!*surface) continue;
            parts_.push_back({*surface, local});
            const auto material = *reinterpret_cast<const uintptr_t*>(*surface);
            if (material && std::find(materials_.begin(), materials_.end(), material) == materials_.end())
                materials_.push_back(material);
        }
        return parts_.size() != before;
    }

    void AddWheels() {
        if (!resource_[7] || resource_[3] > 128 || !wheelResource_[6] || wheelResource_[2] > 128) return;
        constexpr const char* names[] = {"tire_fl", "tire_fr", "tire_rl", "tire_rr"};
        constexpr const char* mounts[] = {"placeholder_tire_fl", "placeholder_tire_fr", "placeholder_tire_rl", "placeholder_tire_rr"};
        for (unsigned wheel = 0; wheel < 4; ++wheel) {
            uintptr_t mount = 0;
            for (unsigned i = 0; i < resource_[3]; ++i) {
                const auto node = resource_[7] + i * 0xF0;
                if (const auto* name = NodeName(node); name && _stricmp(name, mounts[wheel]) == 0) {
                    mount = node;
                    break;
                }
            }
            if (!mount) continue;
            for (unsigned i = 0; i < wheelResource_[2]; ++i) {
                const auto part = wheelResource_[6] + i * 0x120;
                const auto* name = NodeName(part);
                if (!name || _stricmp(name, names[wheel]) != 0) continue;
                Matrix local = *reinterpret_cast<const Matrix*>(part + 0x40);
                std::copy_n(reinterpret_cast<const float*>(mount + 0x70), 3, local.begin() + 12);
#ifdef FLATOUT_GHOST_RENDER_TEST
                if (AddPart(part, local)) ++wheels_;
#else
                AddPart(part, local);
#endif
                break;
            }
        }
    }

    static void ReleaseModel(Resource& model, std::array<uintptr_t, 4>& slots, std::array<uintptr_t, 3> materials) {
        auto& cache = *reinterpret_cast<std::array<uintptr_t, 3>*>(0x849F34);
        const auto originalCache = cache;
        const auto gameFree = reinterpret_cast<void (__cdecl*)(void*)>(0x64C2C0);
        // The standalone power-up BGM destructor owns the same four arrays and private materials.
        slots = {model[8], model[4], model[5], model[6]};
        cache = materials;
        reinterpret_cast<void (__cdecl*)()>(0x4895B0)();
        gameFree(reinterpret_cast<void*>(cache[0]));
        cache = originalCache;
        if (const auto nodes = model[7]) {
            const auto header = reinterpret_cast<uintptr_t*>(nodes - 0x10);
            if (*header) {
                const auto vtable = *reinterpret_cast<void***>(nodes);
                reinterpret_cast<void (__thiscall*)(uintptr_t, unsigned)>(vtable[0])(nodes, 3);
            } else {
                gameFree(header);
            }
        }
        model = {};
    }

    static void WriteJump(unsigned char* site, uintptr_t target) {
        const auto relative = static_cast<int32_t>(target - reinterpret_cast<uintptr_t>(site) - 5);
        site[0] = 0xE9;
        memcpy(site + 1, &relative, sizeof(relative));
    }

    static uintptr_t __cdecl LoadMaterial(char* raw, uintptr_t context) {
        auto* texture = *reinterpret_cast<const unsigned*>(raw + 0x34) < 16 ? raw + 0x20 : *reinterpret_cast<char**>(raw + 0x20);
        const bool paint = texture && (_stricmp(texture, "skin1.tga") == 0 || _stricmp(texture, "body.tga") == 0);
        const bool untextured = !texture || !texture[0];
        std::array<char, 24> originalString{};
        if (paint || untextured) {
            memcpy(originalString.data(), raw + 0x20, originalString.size());
            const int length = paint ? sprintf_s(raw + 0x20, 16, "skin%u.tga", loading_->skin_) :
                sprintf_s(raw + 0x20, 16, "common.tga");
            *reinterpret_cast<unsigned*>(raw + 0x30) = static_cast<unsigned>(length);
            *reinterpret_cast<unsigned*>(raw + 0x34) = 15;
        }
        const auto material = reinterpret_cast<uintptr_t (__cdecl*)(char*, uintptr_t)>(0x489300)(raw, context);
        if (paint || untextured) memcpy(raw + 0x20, originalString.data(), originalString.size());
        if (paint) {
            if (material && *reinterpret_cast<const uintptr_t*>(material)) {
                ++loading_->paintedSurfaces_;
                auto& materials = loading_->paintedMaterials_;
                if (std::find(materials.begin(), materials.end(), material) == materials.end())
                    materials.push_back(material);
            }
        }
        return material;
    }

    static void __stdcall Render(Bucket* queue, int pass) {
        const auto visible = std::find_if(assets_.begin(), assets_.end(), [](CarAsset* asset) {
            return asset && asset->visible_;
        });
        if (!queue || pass != 8 || visible == assets_.end()) {
            original_(queue, pass);
            return;
        }
        auto& probe = **visible;
#ifdef FLATOUT_GHOST_RENDER_TEST
        ++probe.calls_;
#endif
        auto& bucket = queue[pass];
        const auto saved = bucket;
        if (!saved.begin || !saved.end || saved.end <= saved.begin || saved.end - saved.begin > 768) {
            original_(queue, pass);
            return;
        }
        const auto begin = *reinterpret_cast<const uintptr_t* const*>(probe.car_ + 0x1C8C);
        const auto end = *reinterpret_cast<const uintptr_t* const*>(probe.car_ + 0x1C90);
        if (!begin || !end || end <= begin || end - begin > 512) {
            original_(queue, pass);
            return;
        }
        const auto localCarItem = std::find_if(saved.begin, saved.end, [&](const Item& item) {
            return item.surface && std::find(begin, end, *reinterpret_cast<const uintptr_t*>(item.surface)) != end;
        });
        if (localCarItem == saved.end) {
            original_(queue, pass);
            return;
        }
        const auto paint = std::find_if(begin, end, [](uintptr_t material) {
            return material && *reinterpret_cast<const unsigned*>(material + 0x94) == 5;
        });
        probe.items_.assign(saved.begin, saved.end);
        for (auto* asset : assets_) {
            if (!asset || !asset->visible_ || asset->car_ != probe.car_ || asset->parts_.size() + probe.items_.size() > 768)
                continue;
            asset->Append(probe.items_, *localCarItem, paint != end ? *paint : 0);
        }
        bucket.begin = probe.items_.data();
        bucket.end = bucket.capacity = bucket.begin + probe.items_.size();
        original_(queue, pass);
        const bool consumed = bucket.end == bucket.begin;
        bucket = saved;
        if (consumed) bucket.end = bucket.begin;
    }

    void Append(std::vector<Item>& items, const Item& reference, uintptr_t paint) {
        if (paint && diffuse_ && damaged_ && specular_) {
            for (const auto material : paintedMaterials_) {
                // The car paint shader samples all three skin-dependent slots.
                // Keep the model's secondary texture and bind its own skin maps.
                auto* textures = reinterpret_cast<uintptr_t*>(material);
                textures[0] = diffuse_;
                textures[2] = damaged_;
                textures[3] = specular_;
                memcpy(reinterpret_cast<void*>(material + 0x10), reinterpret_cast<const void*>(paint + 0x10), 0x64);
                memcpy(reinterpret_cast<void*>(material + 0x94), reinterpret_cast<const void*>(paint + 0x94), 8);
#ifdef FLATOUT_GHOST_RENDER_TEST
                paintTechniqueBound_ = true;
#endif
            }
        }
        // The native screen-door shader preserves the original RGB values
        // instead of adding light to the scene as the contact effect does.
        for (const auto material : materials_) {
            reinterpret_cast<std::array<float, 4>*>(material + 0x50)->fill(opacity_);
            *reinterpret_cast<uint32_t*>(material + 0x60) = 1;
        }
        matrices_.resize(parts_.size());
        for (unsigned i = 0; i < parts_.size(); ++i) {
            const auto& part = parts_[i];
            matrices_[i] = PlacePart(part.local, target_);
            items.push_back({part.surface, &matrices_[i], reference.sort, reference.flags});
        }
#ifdef FLATOUT_GHOST_RENDER_TEST
        ++frames_;
#endif
    }

    inline static std::array<CarAsset*, 7> assets_{};
    inline static Queue original_ = nullptr;
    inline static CarAsset* loading_ = nullptr;
    bool attempted_ = false, attached_ = false, visible_ = false;
#ifdef FLATOUT_GHOST_RENDER_TEST
    bool paintTechniqueBound_ = false;
    unsigned frames_ = 0, calls_ = 0;
    unsigned wheels_ = 0;
#endif
    float opacity_ = 0.70f;
    uintptr_t car_ = 0;
    unsigned skin_ = 1, paintedSurfaces_ = 0;
    uintptr_t diffuse_ = 0, damaged_ = 0, specular_ = 0;
    Resource resource_{}, wheelResource_{};
    std::array<uintptr_t, 3> privateMaterials_{};
    Matrix target_{};
    std::vector<Part> parts_;
    std::vector<uintptr_t> materials_;
    std::vector<uintptr_t> paintedMaterials_;
    std::vector<Matrix> matrices_;
    std::vector<Item> items_;
};
}
