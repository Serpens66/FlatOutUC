#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
#include "../../ChloeSteamNet/StuntGhostMotion.h"

namespace stunt_preview {
constexpr unsigned MaxJoints = 11;

using stunt_ghost::Vec3;
using stunt_ghost::Dot;
using stunt_ghost::Cross;
using stunt_ghost::Unit;
using stunt_ghost::Pose;
using stunt_ghost::ReadPose;
using stunt_ghost::Blend;
using stunt_ghost::Rotate;

struct Frame {
    uint32_t ms;
    Pose car;
    unsigned jointCount;
    std::array<Pose, MaxJoints> joints;
};

class Replay {
public:
    void Begin(uint64_t event) {
        event_ = event;
        for (auto& track : tracks_) track.clear();
    }

    bool Record(uint64_t event, unsigned target, const Frame& frame) {
        if (event != event_ || target >= tracks_.size() || frame.jointCount > MaxJoints) return false;
        auto& track = tracks_[target];
        if (track.size() >= 1200 || (!track.empty() && frame.ms <= track.back().ms)) return false;
        track.push_back(frame);
        return true;
    }

    bool Sample(unsigned target, uint32_t ms, Frame& result) const {
        if (target >= tracks_.size()) return false;
        const auto& track = tracks_[target];
        if (track.empty() || ms < track.front().ms || ms > track.back().ms) return false;
        const auto after = std::lower_bound(track.begin(), track.end(), ms,
            [](const Frame& frame, uint32_t time) { return frame.ms < time; });
        result = *after;
        if (after == track.begin() || after->ms == ms) return true;
        const Frame& before = *(after - 1);
        const float t = float(ms - before.ms) / float(after->ms - before.ms);
        result.ms = ms;
        result.car = Blend(before.car, after->car, t);
        result.jointCount = before.jointCount;
        result.joints = before.joints;
        if (before.jointCount == after->jointCount) {
            for (unsigned i = 0; i < result.jointCount; ++i) result.joints[i] = Blend(before.joints[i], after->joints[i], t);
        }
        return true;
    }

private:
    uint64_t event_ = 0;
    std::array<std::vector<Frame>, 2> tracks_;
};

// Diagnostic drawing only. It consumes the captured poses and creates no game-world objects.
inline bool DrawFrame(IDirect3DDevice9* device, const Frame& frame) {
    struct Vertex { float x, y, z, rhw; DWORD color; };
    if (!device) return false;
    D3DVIEWPORT9 viewport{};
    if (FAILED(device->GetViewport(&viewport)) || !viewport.Width || !viewport.Height) return false;
    IDirect3DStateBlock9* restore = nullptr;
    if (FAILED(device->CreateStateBlock(D3DSBT_ALL, &restore))) return false;
    if (FAILED(device->BeginScene())) {
        restore->Release();
        return false;
    }

    const float w = static_cast<float>(viewport.Width), h = static_cast<float>(viewport.Height);
    const float x0 = w * 0.33f, y0 = h * 0.08f, x1 = w * 0.96f, y1 = h * 0.77f;
    const Vec3 focus = frame.jointCount ? frame.joints[frame.jointCount > 1 ? 1 : 0].position : frame.car.position;
    const Vec3 eye = focus + Vec3{5.5f, 3.5f, 6.0f};
    const Vec3 forward = Unit(focus - eye);
    const Vec3 right = Unit(Cross(forward, {0, 1, 0}));
    const Vec3 up = Cross(right, forward);
    const float zoom = (y1 - y0) * 0.85f;
    const auto project = [&](Vec3 point, DWORD color, Vertex& result) {
        const Vec3 relative = point - eye;
        const float depth = Dot(forward, relative);
        if (depth < 0.1f) return false;
        result = {(x0 + x1) * 0.5f + zoom * Dot(right, relative) / depth,
            (y0 + y1) * 0.5f - zoom * Dot(up, relative) / depth, 0.0f, 1.0f, color};
        return true;
    };
    std::vector<Vertex> lines;
    const auto line = [&](Vec3 a, Vec3 b, DWORD color, float width) {
        Vertex start, end;
        if (!project(a, color, start) || !project(b, color, end)) return;
        const float dx = end.x - start.x, dy = end.y - start.y;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length < 0.01f) return;
        const float scale = width * w / (1280.0f * length);
        const Vertex startLeft{start.x - dy * scale, start.y + dx * scale, 0, 1, color};
        const Vertex startRight{start.x + dy * scale, start.y - dx * scale, 0, 1, color};
        const Vertex endLeft{end.x - dy * scale, end.y + dx * scale, 0, 1, color};
        const Vertex endRight{end.x + dy * scale, end.y - dx * scale, 0, 1, color};
        lines.insert(lines.end(), {startLeft, startRight, endLeft, endLeft, startRight, endRight});
    };

    const DWORD gridColor = 0xFF41556A, carColor = 0xFF85C7FF, driverColor = 0xFFFFCE75;
    const float ground = frame.jointCount ? frame.car.position.y - 0.3f : focus.y - 0.3f;
    for (int i = -4; i <= 4; ++i) {
        const float d = static_cast<float>(i);
        line({focus.x + d, ground, focus.z - 4}, {focus.x + d, ground, focus.z + 4}, gridColor, 1.0f);
        line({focus.x - 4, ground, focus.z + d}, {focus.x + 4, ground, focus.z + d}, gridColor, 1.0f);
    }
    const Vec3 carOffset = frame.car.position - focus;
    if (Dot(carOffset, carOffset) <= 64.0f) {
        std::array<Vec3, 8> corners;
        for (unsigned i = 0; i < corners.size(); ++i) {
            const Vec3 offset{(i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : 0.1f, (i & 4) ? 2.0f : -2.0f};
            corners[i] = frame.car.position + Rotate(frame.car, offset);
        }
        for (unsigned i = 0; i < corners.size(); ++i) {
            for (unsigned axis : {1u, 2u, 4u}) if (!(i & axis)) line(corners[i], corners[i | axis], carColor, 2.0f);
        }
        line(frame.car.position, frame.car.position + Rotate(frame.car, {0, 0, 2.7f}), carColor, 2.0f);
    }
    constexpr unsigned joints[][2] = {{0,1}, {1,2}, {1,3}, {3,4}, {1,5}, {5,6}, {2,7}, {7,8}, {2,9}, {9,10}};
    for (const auto& pair : joints) {
        if (pair[1] < frame.jointCount) line(frame.joints[pair[0]].position, frame.joints[pair[1]].position, driverColor, 3.0f);
    }
    for (unsigned i = 0; i < frame.jointCount; ++i) {
        const Pose& joint = frame.joints[i];
        for (Vec3 axis : {Vec3{0.09f,0,0}, Vec3{0,0.09f,0}, Vec3{0,0,0.09f}}) {
            const Vec3 dx = Rotate(joint, axis);
            line(joint.position - dx, joint.position + dx, driverColor, 2.0f);
        }
    }

    const DWORD panel = 0xFF122131;
    const Vertex backdrop[] = {{x0,y0,0,1,panel}, {x1,y0,0,1,panel}, {x0,y1,0,1,panel}, {x1,y1,0,1,panel}};
    const RECT scissors{static_cast<LONG>(x0), static_cast<LONG>(y0), static_cast<LONG>(x1), static_cast<LONG>(y1)};
    device->SetVertexShader(nullptr);
    device->SetPixelShader(nullptr);
    device->SetTexture(0, nullptr);
    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    device->SetRenderState(D3DRS_ZENABLE, FALSE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
    device->SetScissorRect(&scissors);
    const auto drawn = device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, backdrop, sizeof(Vertex));
    const auto projected = device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, static_cast<UINT>(lines.size() / 3), lines.data(), sizeof(Vertex));
    const auto ended = device->EndScene();
    const auto restored = restore->Apply();
    restore->Release();
    return SUCCEEDED(drawn) && SUCCEEDED(projected) && SUCCEEDED(ended) && SUCCEEDED(restored);
}
}
