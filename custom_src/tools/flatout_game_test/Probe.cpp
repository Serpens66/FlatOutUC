#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <xinput.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <share.h>
#include "StuntPosePreview.h"
#include "../../ChloeSteamNet/StuntVideo.h"
#include "../../ChloeSteamNet/StuntAudio.h"

// Only staged by Invoke-FlatOutGameTest.ps1 after checking the executable hash.
// Offsets and layouts are from nya-common-fouc's DeviceD3d, GameFlow,
// Player and EventManager headers. This probe is never shipped with the mod.
static void(__thiscall* originalEndScene)(void*);
static wchar_t logPath[1024];
static HANDLE leaveRace;
static HANDLE captureFrame;
static HANDLE advanceStunt;
static PVOID exceptionObserver;
static bool verifyMusic;
static bool forceTournamentTie;
static bool verifyDerbyGrid;
static bool verifyDerbyImpact;
static bool recordStuntPose;
static bool verifyStuntVideo;
static uintptr_t drawDerbyNitro;

// Give the renderer a repeatable half-full nitro bar with the flame active.
// This runs only for the test capture and restores the car fields before the next simulation tick.
static uint32_t __stdcall PrepareNitroDraw(uintptr_t player, float* original) {
    if (!player) return 0;
    const auto car = *reinterpret_cast<uintptr_t*>(player + 0x294);
    if (!car) return 0;
    auto fuel = reinterpret_cast<float*>(car + 0x5EC);
    auto button = reinterpret_cast<float*>(car + 0x1F18);
    auto gauge = reinterpret_cast<float*>(player + 0x2B0);
    original[0] = *fuel;
    original[1] = *button;
    original[2] = *gauge;
    *fuel = fuel[1] > 0.0f ? fuel[1] * 0.5f : 10.0f;
    *button = 1.0f;
    *gauge = 0.5f;
    return car;
}

static __declspec(naked) void DerbyNitroFixture() {
    __asm {
        push ebp
        mov ebp, esp
        sub esp, 16
        pushad
        lea edx, [ebp - 12]
        push edx
        push dword ptr [ebp + 8]
        call PrepareNitroDraw
        mov [ebp - 16], eax
        popad
        push dword ptr [ebp + 20]
        push dword ptr [ebp + 16]
        push dword ptr [ebp + 12]
        push dword ptr [ebp + 8]
        call dword ptr [drawDerbyNitro]
        mov edx, [ebp - 16]
        test edx, edx
        jz done
        mov ecx, [ebp - 12]
        mov [edx + 0x5EC], ecx
        mov ecx, [ebp - 8]
        mov [edx + 0x1F18], ecx
        mov edx, [ebp + 8]
        mov ecx, [ebp - 4]
        mov [edx + 0x2B0], ecx
    done:
        mov esp, ebp
        pop ebp
        ret 16
    }
}

static bool InstallDerbyHudFixture() {
    auto site = reinterpret_cast<uint8_t*>(0x4E83D9);
    if (site[0] != 0xE8) return false;
    int32_t displacement;
    memcpy(&displacement, site + 1, sizeof(displacement));
    drawDerbyNitro = uintptr_t(site + 5) + displacement;
    DWORD protection;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &protection)) return false;
    displacement = int32_t(uintptr_t(&DerbyNitroFixture) - uintptr_t(site + 5));
    memcpy(site + 1, &displacement, sizeof(displacement));
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    VirtualProtect(site, 5, protection, &protection);
    return true;
}

static bool LogDerbyGrid(FILE* log, uintptr_t flow, int level) {
    const auto begin = *reinterpret_cast<const uintptr_t**>(flow + 0x2824);
    const auto end = *reinterpret_cast<const uintptr_t**>(flow + 0x2828);
    if (!begin || end <= begin || end - begin > 32) return false;
    for (auto pos = begin; pos < end; ++pos) {
        if (!*pos || !*reinterpret_cast<const uintptr_t*>(*pos + 0x294)) return false;
    }
    fprintf(log, "derby-grid %d %.4f", level, *reinterpret_cast<const float*>(flow + 0x4FC));
    for (auto pos = begin; pos < end; ++pos) {
        const auto car = *reinterpret_cast<const uintptr_t*>(*pos + 0x294);
        const auto xyz = reinterpret_cast<const float*>(car + 0x1F0);
        fprintf(log, " %d,%.2f,%.2f,%.2f", *reinterpret_cast<const int*>(*pos + 0x2CC), xyz[0], xyz[1], xyz[2]);
    }
    fputc('\n', log);
    return true;
}

static void MeasureDerbyImpact(FILE* log, uintptr_t flow, int time) {
    static int start = -1;
    static bool reported = false;
    static float beforePlayer = 0, beforeAI = 0;
    if (reported || time < 200) return;
    const auto players = *reinterpret_cast<const uintptr_t**>(flow + 0x2824);
    const auto end = *reinterpret_cast<const uintptr_t**>(flow + 0x2828);
    if (!players || end - players != 6) return;
    const auto localCar = *reinterpret_cast<const uintptr_t*>(players[0] + 0x294);
    const auto aiCar = *reinterpret_cast<const uintptr_t*>(players[2] + 0x294);
    if (!localCar || !aiCar) return;
    const float playerDamage = *reinterpret_cast<const float*>(localCar + 0x7AB8);
    const float aiDamage = *reinterpret_cast<const float*>(aiCar + 0x7AB8);
    if (start >= 0) {
        if (time - start >= 1000) {
            fprintf(log, "derby impact %.4f %.6f %.6f\n", *reinterpret_cast<const float*>(flow + 0x4FC),
                playerDamage - beforePlayer, aiDamage - beforeAI);
            reported = true;
        }
        return;
    }

    // Place the stationary AI six metres ahead of the local car, then bring the local car straight into it.
    // This runs only on the host of this short fixture. Both cars keep their own orientations.
    auto localMatrix = reinterpret_cast<const float*>(localCar + 0x1C0);
    auto aiMatrix = reinterpret_cast<float*>(aiCar + 0x1C0);
    const float length = std::sqrt(localMatrix[8] * localMatrix[8] + localMatrix[10] * localMatrix[10]);
    if (length < 0.5f) return;
    const float x = localMatrix[8] / length, z = localMatrix[10] / length;
    aiMatrix[12] = localMatrix[12] + x * 6.0f;
    aiMatrix[13] = localMatrix[13];
    aiMatrix[14] = localMatrix[14] + z * 6.0f;
    auto velocity = reinterpret_cast<float*>(localCar + 0x290);
    velocity[0] = x * 27.0f;
    velocity[1] = 0;
    velocity[2] = z * 27.0f;
    memset(reinterpret_cast<void*>(aiCar + 0x290), 0, 3 * sizeof(float));
    start = time;
    beforePlayer = playerDamage;
    beforeAI = aiDamage;
}

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
            !score || *reinterpret_cast<const uintptr_t*>(score) != 0x6E3E4C) return ERROR_SUCCESS;
    const int state = *reinterpret_cast<const int*>(flow + 0x2738);
    const auto now = GetTickCount64();
    static ULONGLONG continueUntil = 0;
    if ((state == 8 || state == 9) && advanceStunt) {
        if (WaitForSingleObject(advanceStunt, 0) == WAIT_OBJECT_0) continueUntil = now + 200;
        if (now < continueUntil) {
            input->Gamepad.wButtons = XINPUT_GAMEPAD_A;
            return ERROR_SUCCESS;
        }
    }
    if (state != 8) return ERROR_SUCCESS;
    if (advanceStunt) return ERROR_SUCCESS;
    static int previousAttempt = -1;
    static ULONGLONG attemptStart;
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

static bool WritePose(FILE* file, uint32_t ms, int object, const float* position, const float* rotation) {
    for (int i = 0; i < 3; ++i) if (!std::isfinite(position[i])) return false;
    for (int i = 0; i < 4; ++i) if (!std::isfinite(rotation[i])) return false;
    return fprintf(file, "%u,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n", ms, object,
        position[0], position[1], position[2], rotation[0], rotation[1], rotation[2], rotation[3]) > 0;
}

static bool CheckStuntReplay(void* self, stunt_preview::Replay& replay, uint32_t ejection, uint32_t duration) {
    using namespace stunt_preview;
    Frame approach{}, launch{}, flight{}, landing{}, switched{};
    if (!replay.Sample(0, 501, approach) || approach.jointCount ||
        !replay.Sample(0, ejection + 25, launch) || launch.jointCount != MaxJoints ||
        !replay.Sample(0, ejection + 1350, flight) || flight.jointCount != MaxJoints ||
        !replay.Sample(1, duration + 325, landing) || landing.jointCount != MaxJoints ||
        !replay.Sample(1, ejection + 1350, switched) || switched.jointCount != MaxJoints) return false;
    if (flight.joints[1].position.y < launch.joints[1].position.y + 2.0f ||
        landing.joints[1].position.y > 2.5f) return false;
    const Vec3 difference = switched.joints[1].position - flight.joints[1].position;
    if (Dot(difference, difference) < 1.0f) return false;

    // A new event on the same level must never reuse buffered poses from its predecessor.
    replay.Begin(2);
    Frame old{};
    if (replay.Record(1, 0, flight) || replay.Sample(0, flight.ms, old) ||
        !replay.Record(2, 0, flight) || !replay.Sample(0, flight.ms, old) ||
        replay.Sample(1, flight.ms, old)) return false;

    const auto device = *reinterpret_cast<IDirect3DDevice9**>(static_cast<char*>(self) + 0x60);
    const auto score = *reinterpret_cast<uintptr_t*>(0x846514);
    const auto flow = *reinterpret_cast<uintptr_t*>(0x9298FAC);
    if (!score || *reinterpret_cast<uintptr_t*>(score) != 0x6E3E4C || !flow) return false;
    const int state = *reinterpret_cast<int*>(flow + 0x2738);
    const auto scoreFields = reinterpret_cast<const int*>(score + 0x30);
    std::array<int, 5> before;
    std::copy_n(scoreFields, before.size(), before.begin());
    for (const Frame* frame : {&approach, &launch, &flight, &landing, &switched}) {
        if (!DrawFrame(device, *frame) || !CaptureFrame(self)) return false;
    }
    return *reinterpret_cast<uintptr_t*>(0x846514) == score &&
        *reinterpret_cast<int*>(flow + 0x2738) == state &&
        std::equal(before.begin(), before.end(), scoreFields);
}

static bool ResetStuntVideoDevice(const stunt_video::Frame& frame) {
    using namespace stunt_video;
    auto module = GetModuleHandleW(L"d3d9.dll");
    if (!module) return false;
    auto create = reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(
        reinterpret_cast<uintptr_t>(GetProcAddress(module, "Direct3DCreate9")));
    if (!create) return false;
    const auto window = CreateWindowExW(WS_EX_NOACTIVATE, L"STATIC", L"FlatOut video reset test",
        WS_POPUP, 0, 0, 640, 360, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!window) return false;
    bool passed = false;
    {
        ComPtr<IDirect3D9> d3d;
        d3d.Attach(create(D3D_SDK_VERSION));
        ComPtr<IDirect3DDevice9> device;
        D3DPRESENT_PARAMETERS params{};
        params.Windowed = TRUE;
        params.hDeviceWindow = window;
        params.BackBufferWidth = 640;
        params.BackBufferHeight = 360;
        params.BackBufferFormat = D3DFMT_X8R8G8B8;
        params.SwapEffect = D3DSWAPEFFECT_DISCARD;
        params.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
        if (d3d && SUCCEEDED(d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &params, &device))) {
            Frame before, after;
            before.quality = after.quality = frame.quality;
            if (Draw(device.Get(), frame) && Capture(device.Get(), before)) {
                params.BackBufferWidth = 800;
                params.BackBufferHeight = 600;
                passed = SUCCEEDED(device->Reset(&params)) && Draw(device.Get(), frame) &&
                    Capture(device.Get(), after) && before.width == 1280 && before.height == 720 &&
                    after.width == 960 && after.height == 720;
            }
        }
    }
    DestroyWindow(window);
    return passed;
}

static bool VerifyStuntTransport() {
    using namespace stunt_stream;
    constexpr uint64_t driver = 1;
    Chain chain{1, {}};
    if (chain.Next(driver, driver)) return false;
    for (uint64_t count = 1; count <= 3; ++count) {
        chain.viewers.push_back(count + 1);
        unsigned sourceCopies = 0, delivered = 0;
        for (auto next = chain.Next(driver, driver); next; next = chain.Next(driver, next)) {
            sourceCopies += chain.Previous(driver, next) == driver;
            ++delivered;
        }
        if (sourceCopies != 1 || delivered != count) return false;
    }
    chain = {2, {3, 4}};
    if (chain.Next(driver, driver) != 3 || chain.Previous(driver, 3) != driver ||
        chain.Next(driver, 3) != 4 || chain.Previous(driver, 2)) return false;
    const auto adapt = [](unsigned upload, unsigned download, unsigned expected) {
        Adaptation controller;
        // Account for the video fragments and 50 audio packets each second.
        for (uint64_t time = 500; time <= 17000; time += 500) {
            const auto& quality = Levels[controller.level];
            const unsigned wireKbps = quality.videoKbps * 106 / 100 + quality.audioKbps + 22;
            controller.Observe(wireKbps > (std::min)(upload, download), time);
        }
        if (controller.level != expected) return false;
        for (uint64_t time = 17500; time <= 65000; time += 500) controller.Observe(false, time);
        return controller.level == 4;
    };
    if (!adapt(1000, 3100, 2) || !adapt(3100, 1000, 2) ||
        !adapt(256, 3100, 0) || !adapt(3100, 256, 0)) return false;
    Packet picture{1, driver, 2};
    picture.type = stunt_stream::Picture;
    picture.sequence = 1;
    picture.width = 960;
    picture.height = 540;
    picture.quality = 2;
    picture.size = FragmentBytes + 1;
    std::array<uint8_t, FragmentBytes> bytes{};
    Assembly assembly;
    if (!Valid(picture, bytes.size()) || assembly.Add(picture, bytes.data(), bytes.size(), 100)) return false;
    picture.fragment = 1;
    if (assembly.Add(picture, bytes.data(), 1, 351)) return false; // expired first fragment
    picture.sequence = 2;
    if (assembly.Add(picture, bytes.data(), 1, 352) || assembly.Add(picture, bytes.data(), 1, 353)) return false;
    picture.fragment = 0;
    return assembly.Add(picture, bytes.data(), bytes.size(), 354) && assembly.packet.sequence == 2;
}

#ifdef STUNT_STREAM_OPUS
static bool VerifyOpusRoundTrip() {
    using namespace stunt_audio;
    Encoder encoder;
    for (const unsigned channels : {2u, 1u}) {
        std::array<float, Samples * 2> input;
        for (unsigned i = 0; i < Samples; ++i)
            input[i * 2] = input[i * 2 + 1] = std::sin(static_cast<float>(i) * 0.08f) * 0.25f;
        const auto packet = encoder.Encode(input, channels);
        int error = 0;
        auto* decoder = opus_decoder_create(Rate, channels, &error);
        if (error || !decoder) return false;
        std::array<float, Samples * 2> output{};
        const int decoded = opus_decode_float(decoder, packet.data(), static_cast<opus_int32>(packet.size()),
            output.data(), Samples, 0);
        opus_decoder_destroy(decoder);
        float peak = 0;
        for (float value : output) peak = (std::max)(peak, std::abs(value));
        if (decoded != static_cast<int>(Samples) || peak < 0.1f || packet.size() != (channels == 2 ? 160u : 80u)) return false;
    }
    return true;
}
#endif

static void VerifyStuntVideo(void* renderer, uint32_t elapsed, uint32_t airborne, uintptr_t host) {
    using namespace stunt_video;
    static unsigned streams = 0, shown = 0;
    static bool finished = false;
    static uint64_t waitingSince = 0, lastCapture = 0, copyMicros = 0;
    static uint32_t received = 0, sent = 0, milestone = 0, copies = 0;
    static stunt_stream::Assembly assembly;
    if (!verifyStuntVideo || finished) return;
#ifdef STUNT_STREAM_OPUS
    static auto* effects = new stunt_audio::Capture;
    static bool audioStarted = false, audioAttempted = false;
    static unsigned audioBlocks = 0;
    static float effectsPeak = 0;
    if (!audioAttempted && elapsed >= 300) {
        audioAttempted = true;
        audioStarted = effects->Start(*reinterpret_cast<void**>(0x7640FC), stunt_audio::EffectGroups());
    }
    if (audioStarted) {
        std::array<float, stunt_audio::Samples * 2> pcm;
        uint32_t stamp = 0;
        if (effects->ReadBlock(pcm, stamp, GetTickCount64())) {
            ++audioBlocks;
            for (float value : pcm) effectsPeak = (std::max)(effectsPeak, std::abs(value));
        }
    }
#endif
    static Codec* codec = new Codec();
    auto device = *reinterpret_cast<IDirect3DDevice9**>(static_cast<char*>(renderer) + 0x60);
    const uint64_t now = GetTickCount64();
    const unsigned target = shown % 2 + 1;
    const uint64_t event = shown < 2 ? 40 : 41;
    auto report = [&](bool success) {
#ifdef STUNT_STREAM_OPUS
        if (audioStarted) effects->Stop();
        const bool audioCodec = VerifyOpusRoundTrip();
        success = success && audioCodec && audioStarted && audioBlocks;
#endif
        if (FILE* log = _wfsopen(logPath, L"a", _SH_DENYNO)) {
            fprintf(log, "stunt video loopback %s (%u snapshots, %u frames displayed, %u captured, mean copy %llu us)\n",
                success ? "complete" : "failed", shown, streams, copies, copies ? copyMicros / copies : 0);
#ifdef STUNT_STREAM_OPUS
            fprintf(log, "stunt audio: Opus %s, FMOD tap %s, %u blocks, peak %.6f\n",
                audioCodec ? "passed" : "failed", audioStarted ? "attached" : "unavailable", audioBlocks, effectsPeak);
#endif
            fclose(log);
        }
        finished = true;
    };
    if (milestone && now - waitingSince > 900) { report(false); return; }
    if (elapsed >= 120 && now - lastCapture >= 42) {
        const bool phase = shown == 0 ? elapsed >= 350 : shown == 1 ? airborne >= 180 :
            shown == 2 ? airborne >= 960 : airborne >= 3280;
        lastCapture = now;
        Frame captured{event, target, ++sent, 0, 0, {}};
        captured.quality = 4;
        captured.key = !received || elapsed / 1000 != (elapsed - 42) / 1000;
        LARGE_INTEGER before{}, after{}, frequency{};
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&before);
        if (!Capture(device, captured)) { report(false); return; }
        QueryPerformanceCounter(&after);
        copyMicros += static_cast<uint64_t>((after.QuadPart - before.QuadPart) * 1000000 / frequency.QuadPart);
        ++copies;
        if (phase && !milestone) {
            milestone = captured.sequence;
            waitingSince = now;
            if (FILE* log = _wfsopen(logPath, L"a", _SH_DENYNO)) {
                fprintf(log, "stunt video phase %u capture at %u ms\n", shown + 1, elapsed);
                fclose(log);
            }
        }
        codec->Submit(false, std::move(captured));
    }
    if (auto encoded = codec->Take(false)) {
        using namespace stunt_stream;
        if (encoded->event != event || encoded->subscription != target || encoded->sequence <= received) return;
        stunt_stream::Packet packet{encoded->event, 1, encoded->subscription};
        packet.type = stunt_stream::Picture;
        packet.sequence = encoded->sequence;
        packet.size = static_cast<uint32_t>(encoded->bytes.size());
        packet.width = static_cast<uint16_t>(encoded->width);
        packet.height = static_cast<uint16_t>(encoded->height);
        packet.quality = encoded->quality;
        packet.key = encoded->key;
        for (size_t offset = 0; offset < packet.size; offset += FragmentBytes, ++packet.fragment) {
            const auto length = (std::min)(FragmentBytes, packet.size - offset);
            const auto* payload = encoded->bytes.data() + offset;
            if (!Valid(packet, length)) { report(false); return; }
            if (assembly.Add(packet, payload, length, now)) {
                received = packet.sequence;
                encoded->bytes = std::move(assembly.data);
                codec->Submit(true, std::move(*encoded));
            }
        }
    }
    if (auto decoded = codec->Take(true)) {
        if (decoded->event != event || decoded->subscription != target) return;
        ++streams;
        const auto score = *reinterpret_cast<uintptr_t*>(0x846514);
        if (!score || *reinterpret_cast<uintptr_t*>(score) != 0x6E3E4C) { report(false); return; }
        const auto fields = reinterpret_cast<const int*>(score + 0x30);
        std::array<int, 5> saved;
        std::copy_n(fields, saved.size(), saved.begin());
        const int timer = *reinterpret_cast<int*>(host + 0x2099C);
        static const auto caption = MakeCaption(L"ZUGUCKEN   [V / X]");
        const bool snapshot = milestone && decoded->sequence >= milestone;
        if (!Draw(device, *decoded) || (snapshot && (!Draw(device, caption, true) || !CaptureFrame(renderer))) ||
            *reinterpret_cast<int*>(host + 0x2099C) != timer || !std::equal(saved.begin(), saved.end(), fields)) {
            report(false);
            return;
        }
        if (snapshot) {
            if (decoded->width != 1280 || decoded->height != 720) { report(false); return; }
            milestone = 0;
            if (++shown == 4) {
                const bool reset = ResetStuntVideoDevice(*decoded);
                const bool transport = VerifyStuntTransport();
                if (FILE* log = _wfsopen(logPath, L"a", _SH_DENYNO)) {
                    fprintf(log, "stunt video device reset %s\n", reset ? "passed" : "failed");
                    fprintf(log, "stunt video routing and capped link fixtures %s\n", transport ? "passed" : "failed");
                    fclose(log);
                }
                report(streams >= 20 && reset && transport);
                return;
            }
            return;
        }
    }
}

static void RecordStuntPose(void* device, ULONGLONG now) {
    static ULONGLONG started = 0, last = 0, launched = 0;
    static bool complete = false, launchSent = false, moving = false;
    static unsigned carFrames = 0, driverFrames = 0;
    static int recordedAttempt = 0;
    static const bool peer = GetEnvironmentVariableW(L"FOUC_TEST_STUNT_VIDEO_PEER_HOST", nullptr, 0) != 0;
    static stunt_preview::Replay replay;
    const auto flow = *reinterpret_cast<uintptr_t*>(0x9298FAC);
    if (!flow || *reinterpret_cast<int*>(flow + 0x4A4) != 2 || *reinterpret_cast<int*>(flow + 0x514) != 2 ||
        *reinterpret_cast<int*>(flow + 0x4CC) != 47 || *reinterpret_cast<int*>(flow + 0x2738) != 8) return;
    const auto host = *reinterpret_cast<uintptr_t*>(flow + 0x2820);
    if (!host) return;
    const auto players = *reinterpret_cast<uintptr_t**>(host + 0x14);
    const auto end = *reinterpret_cast<uintptr_t**>(host + 0x18);
    if (!players || end <= players || !players[0] || *reinterpret_cast<int*>(players[0] + 0x2C8) != 1) return;
    const auto player = players[0];
    const auto car = *reinterpret_cast<uintptr_t*>(player + 0x294);
    if (!car) return;
    const auto matrix = reinterpret_cast<const float*>(car + 0x1C0);
    if (peer) {
        const auto score = *reinterpret_cast<uintptr_t*>(0x846514);
        if (!score || *reinterpret_cast<uintptr_t*>(score) != 0x6E3E4C) return;
        const auto attempt = *reinterpret_cast<int*>(score + 0x34);
        if (attempt < 1 || attempt > 2) return;
        if (recordedAttempt != attempt) {
            recordedAttempt = attempt;
            started = last = launched = 0;
            complete = launchSent = moving = false;
            carFrames = driverFrames = 0;
        }
    }
    if (started) VerifyStuntVideo(device, static_cast<uint32_t>(now - started),
        launched ? static_cast<uint32_t>(now - launched) : 0, host);
    if (now - last < 50) return;
    last = now;
    if (complete) return;
    if (!started) {
        started = now;
        replay.Begin(1);
    }
    const auto elapsed = static_cast<uint32_t>(now - started);
    const uint32_t startDelay = peer ? 5000 : 0;
    if (!moving && elapsed >= startDelay) {
        moving = true;
        // Give the local test car an initial speed along its own forward axis. No global input is used.
        auto velocity = reinterpret_cast<float*>(car + 0x290);
        velocity[0] = matrix[8] * 25.0f;
        velocity[2] = matrix[10] * 25.0f;
        CaptureFrame(device);
    }
    if (!launchSent && elapsed >= startDelay + 1100) {
        launchSent = true;
        // The same native action as releasing the launch control. The test sets the middle of the launch meter.
        *reinterpret_cast<float*>(player + 0x484) = 0.5f;
        PostGameEvent(0x1774, *reinterpret_cast<int*>(player + 0x2C4));
    }

    wchar_t path[_countof(logPath) + 15];
    swprintf_s(path, L"%ls.stunt.csv", logPath);
    FILE* file;
    if (_wfopen_s(&file, path, L"a")) return;
    if (!carFrames && recordedAttempt < 2) fputs("ms,object,x,y,z,qx,qy,qz,qw\n", file);
    stunt_preview::Frame frame{};
    frame.ms = elapsed;
    const auto rotation = reinterpret_cast<const float*>(car + 0x280);
    const bool validCar = stunt_preview::ReadPose(frame.car, matrix + 12, rotation);
    if (validCar && WritePose(file, elapsed, -1, matrix + 12, rotation)) ++carFrames;

    if (*reinterpret_cast<int*>(car + 0x3590)) {
        const auto body = *reinterpret_cast<const uint8_t**>(car + 0x3570);
        const int count = *reinterpret_cast<const int*>(car + 0x3574);
        // The game's replay serializer uses the same array and writes at most eleven joints.
        if (body && count == stunt_preview::MaxJoints) {
            bool validDriver = true;
            for (int i = 0; i < count; ++i) {
                const auto joint = body + i * 0x180;
                const auto position = reinterpret_cast<const float*>(joint + 0x30);
                const auto orientation = reinterpret_cast<const float*>(joint + 0xC0);
                if (!stunt_preview::ReadPose(frame.joints[i], position, orientation) ||
                    !WritePose(file, elapsed, i, position, orientation)) validDriver = false;
            }
            if (validDriver) {
                frame.jointCount = count;
                if (driverFrames++ == 0) {
                    launched = now;
                    CaptureFrame(device);
                }
                if (driverFrames == 15 || driverFrames == 60) CaptureFrame(device);
            }
        }
    }
    fclose(file);
    if (validCar) {
        replay.Record(1, 0, frame);
        // The second fixture replays the clip 350 ms behind the first.
        frame.ms += 350;
        replay.Record(1, 1, frame);
    }

    if (launched && now - launched >= 3300 && driverFrames >= 35) {
        CaptureFrame(device);
        const bool preview = CheckStuntReplay(device, replay, static_cast<uint32_t>(launched - started), elapsed);
        FILE* proof;
        if (!_wfopen_s(&proof, logPath, L"a")) {
            fprintf(proof, "stunt pose capture complete: %u car frames and %u driver frames\n", carFrames, driverFrames);
            fprintf(proof, "stunt pose replay %s\n", preview ? "complete" : "failed");
            fclose(proof);
        }
        complete = true;
    }
}

static void __fastcall EndScene(void* self, void*) {
    originalEndScene(self);
    static unsigned frames = 0;
    static ULONGLONG lastSample = 0;
    ++frames;
    const auto now = GetTickCount64();
    if (recordStuntPose) RecordStuntPose(self, now);
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
    static const bool videoPeer = GetEnvironmentVariableW(L"FOUC_TEST_STUNT_VIDEO_PEER_HOST", nullptr, 0) != 0;
    static const bool videoHost = GetEnvironmentVariableW(L"FOUC_TEST_STUNT_VIDEO_HOST_PROMPT", nullptr, 0) != 0;
    if (mode == 2 && (state == 2 || state == 6)) {
        if (introState != state) {
            introState = state;
            introStarted = now;
        }
        const auto score = *reinterpret_cast<uintptr_t*>(0x846514);
        const bool firstStuntPrompt = state == 6 && activeRules == 2 && score &&
            *reinterpret_cast<uintptr_t*>(score) == 0x6E3E4C && *reinterpret_cast<int*>(score + 0x34) == 1;
        const auto confirmDelay = firstStuntPrompt && videoHost ? 2500u : 600u;
        const bool confirm = firstStuntPrompt && videoPeer ?
            WaitForSingleObject(advanceStunt, 0) == WAIT_OBJECT_0 : now - introStarted >= confirmDelay;
        if (introStarted && confirm) {
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
        if (host && (state == 6 || state == 7 || state == 8 || state == 9)) {
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
        static bool gridLogged = false;
        if (verifyDerbyGrid && !gridLogged && mode == 2 && (state == 7 || state == 8) && rules == 3) {
            gridLogged = LogDerbyGrid(log, flow, track);
        }
        if (verifyDerbyImpact && mode == 2 && state == 8 && rules == 3 && track == 31) MeasureDerbyImpact(log, flow, time);
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
        verifyDerbyGrid = GetEnvironmentVariableW(L"FOUC_TEST_DERBY_GRID", eventName, _countof(eventName)) != 0;
        verifyDerbyImpact = GetEnvironmentVariableW(L"FOUC_TEST_DERBY_IMPACT", eventName, _countof(eventName)) != 0 &&
            !wcsstr(GetCommandLineW(), L"-flatouttestclient");
        recordStuntPose = GetEnvironmentVariableW(L"FOUC_TEST_STUNT_POSE", eventName, _countof(eventName)) != 0;
        verifyStuntVideo = GetEnvironmentVariableW(L"FOUC_TEST_STUNT_VIDEO", eventName, _countof(eventName)) != 0;
        if (GetEnvironmentVariableW(L"FOUC_TEST_STUNT_ADVANCE_EVENT", eventName, _countof(eventName)))
            advanceStunt = OpenEventW(SYNCHRONIZE, FALSE, eventName);
        if (GetEnvironmentVariableW(L"FOUC_TEST_DERBY_HUD", eventName, _countof(eventName)))
            LogAttach(InstallDerbyHudFixture() ? L"Derby HUD fixture installed" : L"Derby HUD fixture unavailable");
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
