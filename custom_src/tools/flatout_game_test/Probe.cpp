#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

// Only staged by Invoke-FlatOutGameTest.ps1 after checking the executable hash.
// Offsets and layouts are from nya-common-fouc's DeviceD3d, GameFlow,
// Player and EventManager headers. This probe is never shipped with the mod.
static void(__thiscall* originalEndScene)(void*);
static wchar_t logPath[1024];
static HANDLE leaveRace;
static HANDLE captureFrame;

static void LogAttach(const wchar_t* status) {
    FILE* log;
    if (!_wfopen_s(&log, logPath[0] ? logPath : L"FlatOutGameTest.attach", L"a")) {
        fwprintf(log, L"probe: %ls\n", status);
        fclose(log);
    }
}

static void PostGameEvent(int type, int argument) {
    int event[9] = {type, 0, argument, 0xffff};
    void* manager = *reinterpret_cast<void**>(0x9298FB4);
    if (!manager) return;
    int address = 0x4611D0;
    __asm {
        lea eax, event
        mov edx, manager
        call address
    }
}

static bool CaptureFrame(void* self) {
    auto device = *reinterpret_cast<IDirect3DDevice9**>(static_cast<char*>(self) + 0x60);
    if (!device) return false;
    IDirect3DSurface9 *back = nullptr, *scaled = nullptr, *cpu = nullptr;
    D3DSURFACE_DESC desc = {};
    D3DLOCKED_RECT pixels = {};
    bool success = false;
    if (FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back))) return false;
    const UINT width = 640;
    if (SUCCEEDED(back->GetDesc(&desc)) && desc.Width && desc.Height &&
            (desc.Format == D3DFMT_X8R8G8B8 || desc.Format == D3DFMT_A8R8G8B8)) {
        const UINT height = width * desc.Height / desc.Width;
        if (height && height <= 640 &&
                SUCCEEDED(device->CreateRenderTarget(width, height, desc.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &scaled, nullptr)) &&
                SUCCEEDED(device->StretchRect(back, nullptr, scaled, nullptr, D3DTEXF_LINEAR)) &&
                SUCCEEDED(device->CreateOffscreenPlainSurface(width, height, desc.Format, D3DPOOL_SYSTEMMEM, &cpu, nullptr)) &&
                SUCCEEDED(device->GetRenderTargetData(scaled, cpu)) &&
                SUCCEEDED(cpu->LockRect(&pixels, nullptr, D3DLOCK_READONLY))) {
            static unsigned frameId = 0;
            wchar_t path[_countof(logPath) + 24];
            swprintf_s(path, L"%ls.%03u.bmp", logPath, ++frameId);
            FILE* output;
            if (!_wfopen_s(&output, path, L"wb")) {
                BITMAPFILEHEADER file = {};
                BITMAPINFOHEADER bitmap = {};
                file.bfType = 0x4d42;
                file.bfOffBits = sizeof(file) + sizeof(bitmap);
                file.bfSize = file.bfOffBits + width * height * 4;
                bitmap.biSize = sizeof(bitmap);
                bitmap.biWidth = width;
                bitmap.biHeight = -static_cast<LONG>(height);
                bitmap.biPlanes = 1;
                bitmap.biBitCount = 32;
                fwrite(&file, sizeof(file), 1, output);
                fwrite(&bitmap, sizeof(bitmap), 1, output);
                for (UINT y = 0; y < height; ++y)
                    fwrite(static_cast<char*>(pixels.pBits) + y * pixels.Pitch, 4, width, output);
                success = !ferror(output);
                fclose(output);
            }
            cpu->UnlockRect();
        }
    }
    if (cpu) cpu->Release();
    if (scaled) scaled->Release();
    back->Release();
    return success;
}

static void __fastcall EndScene(void* self, void*) {
    originalEndScene(self);
    static unsigned frames = 0;
    static ULONGLONG lastSample = 0;
    ++frames;
    const auto now = GetTickCount64();
    if (now - lastSample < 200) return;
    lastSample = now;
    if (WaitForSingleObject(captureFrame, 0) == WAIT_OBJECT_0 && !CaptureFrame(self))
        LogAttach(L"frame readback failed");

    const auto flow = *reinterpret_cast<uintptr_t*>(0x9298FAC);
    if (!flow) return;
    const int mode = *reinterpret_cast<int*>(flow + 0x4A4);
    const int state = *reinterpret_cast<int*>(flow + 0x2738);
    static ULONGLONG introStarted = 0;
    static bool introConfirmed = false;
    if (mode == 2 && state == 2) {
        if (!introStarted) introStarted = now;
        if (!introConfirmed && now - introStarted >= 600) {
            // GameFlow handles this confirm event by closing the track intro.
            PostGameEvent(0x1389, 0x0b);
            introConfirmed = true;
        }
    } else {
        introStarted = 0;
        introConfirmed = false;
    }
    int time = 0, track = -1, car = -1, skin = -1, local = 0, ai = 0, remote = 0;
    int rules = -1, derby = 0, limit = -1, stunt = 0, playerStart = 0, attempt = 0, attempts = 0;
    if (mode == 2) {
        car = *reinterpret_cast<int*>(flow + 0x4D8);
        skin = *reinterpret_cast<int*>(flow + 0x4DC);
        rules = *reinterpret_cast<int*>(flow + 0x514);
        derby = *reinterpret_cast<int*>(flow + 0x51C);
        stunt = *reinterpret_cast<int*>(flow + 0x520);
        const auto score = *reinterpret_cast<uintptr_t*>(0x846514);
        const auto host = *reinterpret_cast<uintptr_t*>(flow + 0x2820);
        if (host && (state == 7 || state == 8)) {
            time = *reinterpret_cast<int*>(host + 0x2099C);
            track = *reinterpret_cast<int*>(host + 0x2098C);
            const auto begin = *reinterpret_cast<uintptr_t*>(host + 0x14);
            const auto end = *reinterpret_cast<uintptr_t*>(host + 0x18);
            if (score && *reinterpret_cast<uintptr_t*>(score) == 0x6E2BEC)
                limit = *reinterpret_cast<int*>(score + 0x38);
            if (score && *reinterpret_cast<uintptr_t*>(score) == 0x6E3E4C) {
                attempt = *reinterpret_cast<int*>(score + 0x34);
                attempts = *reinterpret_cast<int*>(score + 0x3C);
            }
            if (begin && end >= begin && end - begin <= 32 * sizeof(void*)) {
                for (auto pos = begin; pos < end; pos += sizeof(void*)) {
                    const auto player = *reinterpret_cast<uintptr_t*>(pos);
                    if (!player) continue;
                    const auto type = *reinterpret_cast<int*>(player + 0x2C8);
                    local += (type == 1);
                    if (type == 1) playerStart = *reinterpret_cast<int*>(player + 0x2CC);
                    remote += (type == 2);
                    ai += (type == 4);
                }
            }
        }
    }
    FILE* log;
    if (!_wfopen_s(&log, logPath, L"a")) {
        fprintf(log, "%u %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n",
            frames, mode, state, time, track, car, skin, local, ai, remote,
            rules, derby, limit, stunt, playerStart, attempt, attempts);
        fclose(log);
    }
    // This is the normal in-race quit request in Fouc.exe.
    if (mode == 2 && state == 8 && WaitForSingleObject(leaveRace, 0) == WAIT_OBJECT_0)
        PostGameEvent(0x2339, 0);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        wchar_t eventName[128];
        if (!GetEnvironmentVariableW(L"FOUC_TEST_PROBE_LOG", logPath, _countof(logPath)) ||
            !GetEnvironmentVariableW(L"FOUC_TEST_LEAVE_EVENT", eventName, _countof(eventName)) ||
            reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) != 0x400000) {
            LogAttach(L"missing launch environment or unexpected image base");
            return TRUE;
        }
        leaveRace = OpenEventW(SYNCHRONIZE, FALSE, eventName);
        if (GetEnvironmentVariableW(L"FOUC_TEST_FRAME_EVENT", eventName, _countof(eventName)))
            captureFrame = OpenEventW(SYNCHRONIZE, FALSE, eventName);
        if (!leaveRace || !captureFrame) {
            LogAttach(L"no exit signal available");
            return TRUE;
        }
        auto slot = reinterpret_cast<decltype(originalEndScene)*>(0x677448);
        originalEndScene = *slot;
        HMODULE owner = nullptr;
        if (reinterpret_cast<uintptr_t>(originalEndScene) != 0x608900) {
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(originalEndScene), &owner)) {
                LogAttach(L"invalid EndScene pointer");
                return TRUE;
            }
            wchar_t path[MAX_PATH];
            if (!GetModuleFileNameW(owner, path, _countof(path))) return TRUE;
            const auto name = wcsrchr(path, L'\\');
            if (!name || (_wcsicmp(name + 1, L"ChloeSteamNet.asi") &&
                    _wcsicmp(name + 1, L"FlatOutUCChloeCollection_gcp.asi") &&
                    _wcsicmp(name + 1, L"FlatOutUCTimeTrialGhosts_gcp.asi") &&
                    _wcsicmp(name + 1, L"FlatOutUCUltrawideSupport_gcp.asi"))) {
                LogAttach(path);
                return TRUE;
            }
        }
        DWORD old;
        if (VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old)) {
            *slot = reinterpret_cast<decltype(originalEndScene)>(EndScene);
            VirtualProtect(slot, sizeof(*slot), old, &old);
            LogAttach(L"hook installed");
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        if (leaveRace) CloseHandle(leaveRace);
        if (captureFrame) CloseHandle(captureFrame);
    }
    return TRUE;
}
