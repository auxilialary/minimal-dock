#include "IconBitmap.h"
#include <vector>
#include <shellapi.h>
#include <shobjidl.h>
#include <commctrl.h>
#include <commoncontrols.h>
#include <wrl/client.h>
#pragma comment(lib, "shell32.lib")
#include <algorithm>
#include <cstring>

namespace {
bool ReadBitmap(HBITMAP bitmap, int width, int height, std::vector<BYTE>& pixels) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    pixels.resize(static_cast<size_t>(width) * height * 4);
    HDC dc = GetDC(nullptr);
    const bool ok = GetDIBits(dc, bitmap, 0, static_cast<UINT>(height),
        pixels.data(), &info, DIB_RGB_COLORS) == height;
    ReleaseDC(nullptr, dc);
    return ok;
}
}

std::unique_ptr<Gdiplus::Bitmap> DecodeIcon(HICON icon) {
    ICONINFO info{};
    if (!GetIconInfo(icon, &info)) return {};
    struct Cleanup {
        ICONINFO& info;
        ~Cleanup() { if (info.hbmColor) DeleteObject(info.hbmColor);
            if (info.hbmMask) DeleteObject(info.hbmMask); }
    } cleanup{info};
    BITMAP source{};
    if (!GetObjectW(info.hbmColor ? info.hbmColor : info.hbmMask, sizeof(source), &source)) return {};
    const int width = source.bmWidth;
    const int height = info.hbmColor ? source.bmHeight : source.bmHeight / 2;
    if (width <= 0 || height <= 0) return {};
    std::vector<BYTE> pixels, mask;
    if (info.hbmColor && !ReadBitmap(info.hbmColor, width, height, pixels)) return {};
    bool alpha = false;
    if (source.bmBitsPixel == 32 && info.hbmColor) {
        for (size_t i = 3; i < pixels.size(); i += 4) alpha |= pixels[i] != 0;
    }
    if (!alpha) {
        // Legacy icons use the AND mask; DrawIconEx supplies their color/XOR data.
        BITMAPINFO dib{};
        dib.bmiHeader = { sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB, 0, 0, 0, 0, 0 };
        void* bits = nullptr;
        HDC dc = CreateCompatibleDC(nullptr);
        HBITMAP bitmap = CreateDIBSection(dc, &dib, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!bitmap) { DeleteDC(dc); return {}; }
        HGDIOBJ old = SelectObject(dc, bitmap);
        std::memset(bits, 0, static_cast<size_t>(width) * height * 4);
        const BOOL drawn = DrawIconEx(dc, 0, 0, icon, width, height, 0, nullptr, DI_NORMAL);
        GdiFlush();
        pixels.assign(static_cast<BYTE*>(bits), static_cast<BYTE*>(bits) + static_cast<size_t>(width) * height * 4);
        SelectObject(dc, old);
        DeleteObject(bitmap);
        DeleteDC(dc);
        if (!drawn || !ReadBitmap(info.hbmMask, width,
            info.hbmColor ? height : height * 2, mask)) return {};
        for (size_t i = 0; i < pixels.size(); i += 4) pixels[i + 3] = mask[i] ? 0 : 255;
    }
    for (size_t i = 0; i < pixels.size(); i += 4) {
        const unsigned a = pixels[i + 3];
        for (size_t c = 0; c < 3; ++c) {
            // Win32 32-bit icon channels are premultiplied; PNG expects straight alpha.
            pixels[i + c] = a == 0 ? 0 : static_cast<BYTE>(alpha ?
                std::min(255u, (unsigned(pixels[i + c]) * 255 + a / 2) / a) : pixels[i + c]);
        }
    }
    auto bitmap = std::make_unique<Gdiplus::Bitmap>(width, height, PixelFormat32bppARGB);
    Gdiplus::BitmapData data{};
    Gdiplus::Rect area(0, 0, width, height);
    if (bitmap->LockBits(&area, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &data) != Gdiplus::Ok) return {};
    for (int y = 0; y < height; ++y) std::memcpy(static_cast<BYTE*>(data.Scan0) + y * data.Stride,
        pixels.data() + static_cast<size_t>(y) * width * 4, static_cast<size_t>(width) * 4);
    bitmap->UnlockBits(&data);
    return bitmap;
}

std::unique_ptr<Gdiplus::Bitmap> ResizeIcon(Gdiplus::Bitmap& source, int pixels) {
    auto result = std::make_unique<Gdiplus::Bitmap>(pixels, pixels, PixelFormat32bppPARGB);
    Gdiplus::Graphics graphics(result.get());
    graphics.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    Gdiplus::ImageAttributes attributes;
    attributes.SetWrapMode(Gdiplus::WrapModeTileFlipXY);
    graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
    const double ratio = std::min(double(pixels) / source.GetWidth(), double(pixels) / source.GetHeight());
    const int width = std::max(1, int(source.GetWidth() * ratio));
    const int height = std::max(1, int(source.GetHeight() * ratio));
    if (graphics.DrawImage(&source, Gdiplus::Rect((pixels-width)/2, (pixels-height)/2, width, height), 0, 0,
        static_cast<INT>(source.GetWidth()), static_cast<INT>(source.GetHeight()),
        Gdiplus::UnitPixel, &attributes) != Gdiplus::Ok) return {};
    return result;
}

std::unique_ptr<Gdiplus::Bitmap> LoadShellIcon(const wchar_t* path, int pixels) {
    SHFILEINFOW info{};
    SHGetFileInfoW(path, 0, &info, sizeof(info), SHGFI_ICON | SHGFI_LARGEICON);

    // Request a sufficient shell image-list fallback before accepting 32px.
    SHFILEINFOW index{};
    Microsoft::WRL::ComPtr<IImageList> images;
    const int bucket = IconSourceSize(pixels);
    if (SHGetFileInfoW(path, 0, &index, sizeof(index), SHGFI_SYSICONINDEX) &&
        SUCCEEDED(SHGetImageList(bucket == 32 ? SHIL_LARGE : bucket == 48 ? SHIL_EXTRALARGE : SHIL_JUMBO,
            IID_PPV_ARGS(&images)))) {
        HICON fallback = nullptr;
        if (SUCCEEDED(images->GetIcon(index.iIcon, ILD_TRANSPARENT, &fallback)) && fallback) {
            DestroyIcon(info.hIcon);
            info.hIcon = fallback;
        }
    }
    auto bitmap = DecodeIcon(info.hIcon);
    DestroyIcon(info.hIcon);
    // Request a standard source bucket, not the final odd-sized target: the
    // shell otherwise may GDI-stretch to 29/36/44/58 before we can resample.
    // The shell image factory selects an icon for the requested size, unlike
    // SHIL_JUMBO, which can return a small icon padded inside a 256px canvas.
    Microsoft::WRL::ComPtr<IShellItemImageFactory> factory;
    HBITMAP shellBitmap = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(path, nullptr, IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(factory->GetImage({ bucket, bucket },
            static_cast<SIIGBF>(SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK), &shellBitmap))) {
        // Wrap the shell's premultiplied BGRA as an HICON so the same tested
        // alpha conversion handles both factory and legacy fallback sources.
        BITMAP details{};
        if (GetObjectW(shellBitmap, sizeof(details), &details)) {
            std::vector<BYTE> opaqueMask(static_cast<size_t>((details.bmWidth + 15) / 16 * 2) * details.bmHeight, 0);
            HBITMAP mask = CreateBitmap(details.bmWidth, details.bmHeight, 1, 1, opaqueMask.data());
            ICONINFO source{ TRUE, 0, 0, mask, shellBitmap };
            HICON icon = CreateIconIndirect(&source);
            if (icon) { auto decoded = DecodeIcon(icon); if (decoded) bitmap = std::move(decoded); DestroyIcon(icon); }
            if (mask) DeleteObject(mask);
        }
        DeleteObject(shellBitmap);
    }
    return bitmap;
}
