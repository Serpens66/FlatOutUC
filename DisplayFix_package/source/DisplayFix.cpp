// DisplayFix.asi - fixes the startup resolution of FlatOut: Ultimate Carnage on PCs where the first
// Windows display device is not the active monitor (typical for laptops with two GPUs).
//
// Bug in Fouc.exe (function 0x456D20, "default display settings"):
//   EnumDisplayDevicesA(NULL, <D3D adapter ordinal>, ...) is used to find the monitor of the D3D adapter, then
//   EnumDisplaySettingsA(<that device>, ENUM_CURRENT_SETTINGS, &dm) reads its resolution. The game assumes that the
//   D3D adapter ordinal equals the Windows display device index and ignores a failure. If device 0 is inactive
//   (e.g. \\.\DISPLAY1 of the iGPU while the dGPU drives the panel), the game uses uninitialised memory as
//   resolution (measured: 0x0 @ 7471184 Hz) and the window ends up with a height of 0.
//   The function runs whenever Savegame/device.cfg does not match the adapter. With the Chloe Collection that is
//   every start, because its setup-skip never lets the setup dialog store the adapter identifier.
//
// Fix: the single call to EnumDisplaySettingsA in that function is redirected. If the device the game asked for
// has no current mode, the primary display is used, which is always D3D adapter 0 (D3DADAPTER_DEFAULT).
// If that fails too, 1024x768 @ 60 Hz is used instead of garbage. On PCs without the problem nothing changes.
//
// Works with any ASI loader, with or without other mods. Fouc.exe is identical in the GFWL (Steam branch gfwl)
// and the current Steam version (4242504 bytes); the patch checks the original bytes and does nothing otherwise.
// Build: custom_src/DisplayFix/build.ps1 (MSVC, x86).

#include <windows.h>

static const DWORD kCallSite = 0x456DCC;                                  // call dword ptr [EnumDisplaySettingsA]
static const BYTE kOriginal[6] = { 0xFF, 0x15, 0xE4, 0x62, 0x67, 0x00 };  // IAT slot 0x6762E4

static BOOL HasMode(LPCSTR device, DWORD mode, DEVMODEA* dm) {
    ZeroMemory(dm, sizeof(DEVMODEA));
    dm->dmSize = sizeof(DEVMODEA);
    return EnumDisplaySettingsA(device, mode, dm) && dm->dmPelsWidth > 0 && dm->dmPelsHeight > 0;
}

static BOOL WINAPI FixedEnumDisplaySettingsA(LPCSTR device, DWORD mode, DEVMODEA* dm) {
    if (HasMode(device, mode, dm)) return TRUE;
    if (HasMode(nullptr, mode, dm)) return TRUE;   // primary display = D3D adapter 0
    dm->dmPelsWidth = 1024;
    dm->dmPelsHeight = 768;
    dm->dmBitsPerPel = 32;
    dm->dmDisplayFrequency = 60;
    return TRUE;
}

static void ApplyPatch() {
    BYTE* site = (BYTE*)kCallSite;
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(site, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) return;
    if (memcmp(site, kOriginal, sizeof(kOriginal)) != 0) return;   // not the expected Fouc.exe

    DWORD old;
    if (!VirtualProtect(site, 6, PAGE_EXECUTE_READWRITE, &old)) return;
    site[0] = 0xE8;                                                // call rel32
    *(DWORD*)(site + 1) = (DWORD)&FixedEnumDisplaySettingsA - (kCallSite + 5);
    site[5] = 0x90;                                                // nop
    VirtualProtect(site, 6, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 6);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        char path[MAX_PATH];
        DWORD n = GetModuleFileNameA(nullptr, path, MAX_PATH);
        if (n >= 8 && lstrcmpiA(path + n - 8, "Fouc.exe") == 0) ApplyPatch();
    }
    return TRUE;
}
