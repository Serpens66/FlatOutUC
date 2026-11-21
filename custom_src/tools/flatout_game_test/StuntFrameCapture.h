#pragma once

#include <windows.h>
#include <d3d9.h>
#include <cstdio>

namespace stunt_ghost {
// Read the game's back buffer only. A zero width keeps the native resolution
// so the ghost shader's dither pattern is preserved in the diagnostic BMP.
inline bool CaptureBackBuffer(void* renderer, const wchar_t* path, UINT requestedWidth) {
    auto* device = *reinterpret_cast<IDirect3DDevice9**>(static_cast<char*>(renderer) + 0x60);
    if (!device) return false;
    IDirect3DSurface9 *back = nullptr, *resolved = nullptr, *cpu = nullptr;
    if (FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back))) return false;
    D3DSURFACE_DESC desc{};
    D3DLOCKED_RECT pixels{};
    bool success = false;
    if (SUCCEEDED(back->GetDesc(&desc)) && desc.Width && desc.Height &&
        (desc.Format == D3DFMT_X8R8G8B8 || desc.Format == D3DFMT_A8R8G8B8)) {
        const UINT width = requestedWidth ? requestedWidth : desc.Width;
        const UINT height = requestedWidth ? width * desc.Height / desc.Width : desc.Height;
        if (width && height && width <= 8192 && height <= 8192 &&
            SUCCEEDED(device->CreateRenderTarget(width, height, desc.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &resolved, nullptr)) &&
            SUCCEEDED(device->StretchRect(back, nullptr, resolved, nullptr, requestedWidth ? D3DTEXF_LINEAR : D3DTEXF_NONE)) &&
            SUCCEEDED(device->CreateOffscreenPlainSurface(width, height, desc.Format, D3DPOOL_SYSTEMMEM, &cpu, nullptr)) &&
            SUCCEEDED(device->GetRenderTargetData(resolved, cpu)) &&
            SUCCEEDED(cpu->LockRect(&pixels, nullptr, D3DLOCK_READONLY))) {
            FILE* output;
            if (!_wfopen_s(&output, path, L"wb")) {
                BITMAPFILEHEADER file{};
                BITMAPINFOHEADER bitmap{};
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
                    fwrite(static_cast<const char*>(pixels.pBits) + y * pixels.Pitch, 4, width, output);
                const bool written = !ferror(output);
                success = fclose(output) == 0 && written;
            }
            cpu->UnlockRect();
        }
    }
    if (cpu) cpu->Release();
    if (resolved) resolved->Release();
    back->Release();
    return success;
}
}
