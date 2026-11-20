#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <xinput.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <share.h>

// Only staged by Invoke-FlatOutGameTest.ps1 after checking the executable hash.
// Offsets and layouts are from nya-common-fouc's DeviceD3d, GameFlow,
// Player and EventManager headers. This probe is never shipped with the mod.
static void(__thiscall* originalEndScene)(void*);
static wchar_t logPath[1024];
static HANDLE leaveRace;
static HANDLE captureFrame;
static PVOID exceptionObserver;
static bool verifyMusic;
static bool forceTournamentTie;

static bool ApplyTournamentTie(uintptr_t flow) {
    if (!*reinterpret_cast<const int*>(flow + 0x530)) return false;
    auto official = reinterpret_cast<int*>(flow + 0x534);
    const auto visual = reinterpret_cast<const int*>(flow + 0x5B4);
    int first = -1, second = -1;
    for (int slot = 0; slot < 32; ++slot) {
        if (visual[slot] == 1) first = slot;
        if (visual[slot] == 2) second = slot;
    }
    if (first < 0 || second < 0 || official[first] != 1) return false;
    for (int slot = 0; slot < 32; ++slot) {
        if (official[slot] == 2) official[slot] = 1;
    }
    official[second] = 1;
    return true;
}

static DWORD WINAPI TestControllerState(DWORD index, XINPUT_STATE* input) {
    if (index != 0) return ERROR_DEVICE_NOT_CONNECTED;
    *input = {};
    static DWORD packet = 0;
    input->dwPacketNumber = ++packet;
    const auto flow = *reinterpret_cast<const uintptr_t*>(0x9298FAC);
    const auto score = *reinterpret_cast<const uintptr_t*>(0x846514);
    if (!flow || *reinterpret_cast<const int*>(flow + 0x4A4) != 2 ||
            *reinterpret_cast<const int*>(flow + 0x514) != 2 ||
            *reinterpret_cast<const int*>(flow + 0x2738) != 8 ||
            !score || *reinterpret_cast<const uintptr_t*>(score) != 0x6E3E4C) return ERROR_SUCCESS;
    static int previousAttempt = -1;
    static ULONGLONG attemptStart;
    const auto now = GetTickCount64();
    const auto attempt = *reinterpret_cast<const int*>(score + 0x34);
    if (attempt != previousAttempt) {
        previousAttempt = attempt;
        attemptStart = now;
    }
    input->Gamepad.bRightTrigger = 255;
    // Accelerate on the High Jump straight and operate its launch control.
    if (now - attemptStart >= 2500 && (now - attemptStart) % 1800 < 650)
        input->Gamepad.wButtons = XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B;
    return ERROR_SUCCESS;
}

static DWORD WINAPI TestControllerCapabilities(DWORD index, DWORD, XINPUT_CAPABILITIES* caps) {
    if (index != 0) return ERROR_DEVICE_NOT_CONNECTED;
    *caps = {};
    caps->Type = XINPUT_DEVTYPE_GAMEPAD;
    caps->SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
    caps->Gamepad = {0xF3FF, 255, 255, 32767, 32767, 32767, 32767};
    return ERROR_SUCCESS;
}

static DWORD WINAPI TestControllerVibration(DWORD index, XINPUT_VIBRATION*) {
    return index == 0 ? ERROR_SUCCESS : ERROR_DEVICE_NOT_CONNECTED;
}

static bool SetupTestController() {
    // The checked executable imports ordinals 2, 3, and 4 in this order.
    auto imports = reinterpret_cast<uintptr_t*>(0x67631C);
    DWORD protection;
    if (!VirtualProtect(imports, 3 * sizeof(*imports), PAGE_READWRITE, &protection)) return false;
    imports[0] = reinterpret_cast<uintptr_t>(TestControllerState);
    imports[1] = reinterpret_cast<uintptr_t>(TestControllerVibration);
    imports[2] = reinterpret_cast<uintptr_t>(TestControllerCapabilities);
    DWORD unused;
    VirtualProtect(imports, 3 * sizeof(*imports), protection, &unused);
    return true;
}

static bool LogSteamPlaylist(FILE* log, const char* kind, uintptr_t address, unsigned count) {
    // MusicInterface::tSong contains an MSVC small string for its relative file path.
    const auto vector = reinterpret_cast<const uintptr_t*>(address);
    constexpr uintptr_t songSize = 0x60;
    const auto begin = vector[0];
    if (!begin || vector[1] != begin + count * songSize) return false;
    const char* paths[15] = {};
    for (unsigned i = 0; i < count; ++i) {
        const auto song = begin + i * songSize;
        const auto capacity = *reinterpret_cast<const size_t*>(song + 0x18);
        const auto path = capacity < 16 ? reinterpret_cast<const char*>(song + 4)
                                      : *reinterpret_cast<const char* const*>(song + 4);
        if (!path || _strnicmp(path, "data/songs/", 11)) return false;
        paths[i] = path;
    }
    for (unsigned i = 0; i < count; ++i) fprintf(log, "music %s %s\n", kind, paths[i]);
    return true;
}

static LONG CALLBACK LogNativeException(EXCEPTION_POINTERS* error) {
    const auto record = error->ExceptionRecord;
    if (record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION &&
            record->ExceptionCode != EXCEPTION_INT_DIVIDE_BY_ZERO)
        return EXCEPTION_CONTINUE_SEARCH;
    HMODULE module;
    wchar_t path[MAX_PATH] = L"?";
    uintptr_t offset = reinterpret_cast<uintptr_t>(record->ExceptionAddress);
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(record->ExceptionAddress), &module)) {
        GetModuleFileNameW(module, path, _countof(path));
        offset -= reinterpret_cast<uintptr_t>(module);
    }
    if (FILE* log = _wfsopen(logPath, L"a", _SH_DENYNO)) {
        fwprintf(log, L"native exception %08lx %ls+%08Ix\n", record->ExceptionCode, path, offset);
        const auto name = wcsrchr(path, L'\\');
        if (record->ExceptionCode == EXCEPTION_INT_DIVIDE_BY_ZERO && offset == 0xB6A93 &&
                name && !_wcsicmp(name + 1, L"FlatOutUCChloeCollection_gcp.asi")) {
            const auto flow = *reinterpret_cast<const uintptr_t*>(0x9298FAC);
            if (flow) {
                fwprintf(log, L"instant-action selection: car=%d level=%d skins=%lu\n",
                    *reinterpret_cast<const int*>(flow + 0x4D8),
                    *reinterpret_cast<const int*>(flow + 0x4CC), error->ContextRecord->Ecx);
            }
        }
        fclose(log);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

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
    const int activeRules = *reinterpret_cast<int*>(flow + 0x514);
    static ULONGLONG introStarted = 0;
    static int introState = 0;
    if (mode == 2 && (state == 2 || state == 6)) {
        if (introState != state) {
            introState = state;
            introStarted = now;
        }
        if (introStarted && now - introStarted >= 600) {
            // GameFlow uses the same confirm action for the track intro and
            // the explicit start prompt before a stunt or offline derby.
            PostGameEvent(0x1389, 0x0b);
            introStarted = 0;
        }
    } else {
        introStarted = 0;
        introState = 0;
    }
    int time = 0, track = -1, car = -1, skin = -1, local = 0, ai = 0, remote = 0;
    int rules = -1, derby = 0, limit = -1, stunt = 0, playerStart = 0, attempt = 0, attempts = 0;
    if (mode == 2) {
        car = *reinterpret_cast<int*>(flow + 0x4D8);
        skin = *reinterpret_cast<int*>(flow + 0x4DC);
        rules = activeRules;
        derby = *reinterpret_cast<int*>(flow + 0x51C);
        stunt = *reinterpret_cast<int*>(flow + 0x520);
        const auto score = *reinterpret_cast<uintptr_t*>(0x846514);
        const auto host = *reinterpret_cast<uintptr_t*>(flow + 0x2820);
        if (host && (state == 7 || state == 8 || state == 9)) {
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
    if (FILE* log = _wfsopen(logPath, L"a", _SH_DENYNO)) {
        fprintf(log, "%u %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n",
            frames, mode, state, time, track, car, skin, local, ai, remote,
            rules, derby, limit, stunt, playerStart, attempt, attempts);
        if (forceTournamentTie && state == 9 && rules == 3 && track == 31 && ApplyTournamentTie(flow)) {
            static bool tieLogged = false;
            if (!tieLogged) {
                fputs("tournament fixture: tied race positions\n", log);
                tieLogged = true;
            }
        }
        if (verifyMusic) {
            static bool menuLogged = false;
            static bool raceLogged = false;
            if (mode == 1 && !menuLogged) menuLogged = LogSteamPlaylist(log, "menu", 0x84678C, 5);
            if (mode == 2 && state >= 7 && !raceLogged) raceLogged = LogSteamPlaylist(log, "race", 0x846780, 15);
        }
        fclose(log);
    }
    // Preserve a naturally finished result by requesting the normal menu transition.
    // An active race instead takes the ordinary in-race quit path.
    if (mode == 2 && (state == 8 || state == 9) && WaitForSingleObject(leaveRace, 0) == WAIT_OBJECT_0)
        PostGameEvent(state == 9 ? 1005 : 0x2339, 0);
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
        verifyMusic = GetEnvironmentVariableW(L"FOUC_TEST_MUSIC", eventName, _countof(eventName)) != 0;
        forceTournamentTie = GetEnvironmentVariableW(L"FOUC_TEST_TOURNAMENT_TIE", eventName, _countof(eventName)) != 0;
        if (GetEnvironmentVariableW(L"FOUC_TEST_STUNT_INPUT", eventName, _countof(eventName)))
            LogAttach(SetupTestController() ? L"test controller installed" : L"test controller unavailable");
        if (GetEnvironmentVariableW(L"FOUC_TEST_FRAME_EVENT", eventName, _countof(eventName)))
            captureFrame = OpenEventW(SYNCHRONIZE, FALSE, eventName);
        if (!leaveRace || !captureFrame) {
            LogAttach(L"no exit signal available");
            return TRUE;
        }
        exceptionObserver = AddVectoredExceptionHandler(1, LogNativeException);
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
        if (exceptionObserver) RemoveVectoredExceptionHandler(exceptionObserver);
        if (leaveRace) CloseHandle(leaveRace);
        if (captureFrame) CloseHandle(captureFrame);
    }
    return TRUE;
}
