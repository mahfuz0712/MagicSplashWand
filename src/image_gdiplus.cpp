// Windows image decoding through GDI+ (PNG, JPEG, GIF, TIFF, BMP, ...).
// NOTE: all standard headers are included *before* <windows.h>/<gdiplus.h>, and
// NOMINMAX is deliberately not defined here, because GDI+ headers want min/max.
#ifdef _WIN32

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "image.h"

#include <windows.h>
#include <gdiplus.h>

static ULONG_PTR g_gdiplusToken = 0;

bool initImaging() {
    Gdiplus::GdiplusStartupInput in;
    return Gdiplus::GdiplusStartup(&g_gdiplusToken, &in, nullptr) == Gdiplus::Ok;
}

void shutdownImaging() {
    if (g_gdiplusToken) {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        g_gdiplusToken = 0;
    }
}

bool loadImageFile(const fs::path& p, Image& out, std::string& err) {
    std::wstring wpath = p.wstring();
    Gdiplus::Bitmap bmp(wpath.c_str(), FALSE);
    if (bmp.GetLastStatus() != Gdiplus::Ok) {
        err = "GDI+ could not open/decode this file";
        return false;
    }
    UINT w = bmp.GetWidth(), h = bmp.GetHeight();
    if (w == 0 || h == 0 || w > 32768 || h > 32768) {
        err = "invalid image size";
        return false;
    }
    Gdiplus::Rect rc(0, 0, (INT)w, (INT)h);
    Gdiplus::BitmapData data;
    if (bmp.LockBits(&rc, Gdiplus::ImageLockModeRead, PixelFormat24bppRGB, &data) != Gdiplus::Ok) {
        err = "GDI+ LockBits failed";
        return false;
    }
    out.w = (int)w;
    out.h = (int)h;
    out.rgb.assign((size_t)w * h * 3, 0);
    for (UINT y = 0; y < h; y++) {
        const uint8_t* src = (const uint8_t*)data.Scan0 + (ptrdiff_t)y * data.Stride;
        uint8_t* dst = out.rgb.data() + (size_t)y * w * 3;
        for (UINT x = 0; x < w; x++) {
            dst[x * 3 + 0] = src[x * 3 + 2];
            dst[x * 3 + 1] = src[x * 3 + 1];
            dst[x * 3 + 2] = src[x * 3 + 0];
        }
    }
    bmp.UnlockBits(&data);
    return true;
}

#endif  // _WIN32
