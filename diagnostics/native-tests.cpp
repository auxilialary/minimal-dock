#include "../src/windows/IslandLayout.h"
#include "../src/windows/IconBitmap.h"
#include "../src/windows/IconIdentity.h"
#include <shobjidl.h>
#include <shellapi.h>
#include <propkey.h>
#include <propvarutil.h>
#include <cstdio>
#include <string>
#include <cstdlib>
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "ole32.lib")
void Check(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
HICON MakeIcon(bool legacy) {
    BITMAPINFO info{};
    info.bmiHeader = { sizeof(BITMAPINFOHEADER), 2, -2, 1, 32, BI_RGB, 0, 0, 0, 0, 0 };
    void* bits = nullptr;
    HBITMAP color = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    auto* pixels = static_cast<DWORD*>(bits);
    // transparent black; premultiplied red at 50%; opaque blue; opaque black.
    pixels[0] = 0; pixels[1] = legacy ? 0x00ff0000 : 0x80800000;
    pixels[2] = legacy ? 0x000000ff : 0xff0000ff; pixels[3] = legacy ? 0 : 0xff000000;
    const BYTE maskBits[] = {0x80, 0, 0, 0};
    HBITMAP mask = CreateBitmap(2, 2, 1, 1, maskBits);
    ICONINFO iconInfo{ TRUE, 0, 0, mask, color };
    HICON icon = CreateIconIndirect(&iconInfo);
    DeleteObject(mask); DeleteObject(color);
    return icon;
}
int main() {
    Check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "shell COM startup");
    ULONG_PTR token{}; Gdiplus::GdiplusStartupInput input;
    Check(Gdiplus::GdiplusStartup(&token, &input, nullptr) == Gdiplus::Ok, "GDI+ startup");
    for (const UINT dpi : {96u, 120u, 144u, 192u}) {
        auto contour = CalculateContour(17, dpi, MulDiv(188, dpi, 96), MulDiv(56, dpi, 96));
        Check(contour.radius == MulDiv(17, dpi, 96), "radius snaps to physical pixel");
        Check(contour.strokeRadius + contour.strokeInset == contour.radius, "stroke matches clip contour");
        Check(contour.strokeInset == 0.5f, "one physical pixel stroke inset");
        const int pixels = IconPhysicalSize(29, dpi);
        Check(pixels == (dpi == 96 ? 29 : dpi == 120 ? 36 : dpi == 144 ? 44 : 58), "DPI icon target size");
    }
    for (int pixels = 1; pixels <= 256; ++pixels) {
        const int source = IconSourceSize(pixels);
        Check(source >= pixels, "shell source never below physical target");
        Check(source == (pixels <= 32 ? 32 : pixels <= 48 ? 48 : 256), "smallest sufficient shell source");
    }
    wchar_t windows[MAX_PATH]{};
    GetWindowsDirectoryW(windows, MAX_PATH);
    const std::wstring explorer = std::wstring(windows) + L"\\explorer.exe";
    for (int target : {29, 36, 44, 58, 116, 232}) {
        auto shell = LoadShellIcon(explorer.c_str(), target);
        Check(shell && shell->GetWidth() >= static_cast<UINT>(target), "real shell source meets DPI target");
        std::printf("Shell icon: target=%d source=%ux%u\n", target, shell->GetWidth(), shell->GetHeight());
        auto resized = ResizeIcon(*shell, target);
        Check(resized && resized->GetWidth() == static_cast<UINT>(target), "real shell PNG target size");
    }
    {
        Gdiplus::Bitmap rectangle(80, 40, PixelFormat32bppARGB);
        Gdiplus::Graphics draw(&rectangle); draw.Clear(Gdiplus::Color(255, 20, 80, 200));
        auto square = ResizeIcon(rectangle, 100);
        Gdiplus::Color pixel;
        square->GetPixel(50, 5, &pixel); Check(pixel.GetA() == 0, "rectangle keeps transparent letterbox");
        square->GetPixel(5, 50, &pixel); Check(pixel.GetA() == 255, "rectangle full width preserved");
        square->GetPixel(50, 50, &pixel); Check(pixel.GetB() == 200, "rectangle color preserved");
    }
    {
        HWND window = CreateWindowExW(0, L"STATIC", L"Icon identity test", WS_POPUP, 0, 0, 1, 1,
            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        Check(window != nullptr, "synthetic window");
        HICON icon = MakeIcon(false);
        SendMessageW(window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
        Check(WindowIcon(window) == icon, "window icon takes precedence");
        IPropertyStore* store = nullptr;
        Check(SUCCEEDED(SHGetPropertyStoreForWindow(window, IID_PPV_ARGS(&store))), "window properties");
        PROPVARIANT value{}; InitPropVariantFromString(L"Dock.Tests.SpecificIdentity", &value);
        store->SetValue(PKEY_AppUserModel_ID, value); PropVariantClear(&value); store->Release();
        Check(AppIdentity(window) == L"Dock.Tests.SpecificIdentity", "explicit AUMID resolves");
        auto chosen = ResolveIcon(window, explorer, L"", icon, 232);
        Check(chosen && chosen->GetWidth() == 2, "unrelated executable artwork cannot replace window icon");
        DestroyWindow(window); DestroyIcon(icon);
    }
    int layouts = 0;
    for (const int origin : {-3840, 0, 1920}) for (const int screen : {320, 640, 1280, 1920, 3840})
    for (const int dpi : {96, 120, 144, 192, 288}) for (int count = 0; count <= 80; ++count) for (int reserve : {20}) {
        auto scale = [dpi](int n) { return MulDiv(n, dpi, 96); };
        RECT monitor{origin, -300, origin + screen, 1860};
        auto r = CalculateIslands(monitor, scale(56), scale(120), scale(14), scale(16),
            scale(count * 54 + reserve), scale(100), count > 0);
        Check(r.system.left >= monitor.left && r.system.right <= monitor.right, "system on monitor");
        if (count) {
            if (r.stacked) {
                Check(std::abs((r.apps.left + r.apps.right) - (monitor.left + monitor.right)) <= 1, "stacked apps centered");
                Check(std::abs((r.system.left + r.system.right) - (monitor.left + monitor.right)) <= 1, "stacked system centered");
                Check(r.apps.top - r.system.bottom == scale(16), "stacked gap");
            } else {
                Check(std::abs((r.apps.left + r.apps.right) - (monitor.left + monitor.right)) <= 1, "applications anchor centered");
                Check(r.system.left - r.apps.right == scale(16), "horizontal DIP gap");
                Check(r.apps.top == r.system.top, "same row");
            }
            Check(r.apps.left >= monitor.left && r.apps.right <= monitor.right, "apps on monitor");
            RECT overlap{}; Check(!IntersectRect(&overlap, &r.apps, &r.system), "no overlap");

        } else Check(std::abs((r.system.left + r.system.right) - (monitor.left + monitor.right)) <= 1, "empty center");
        ++layouts;
    }
    // Hidden HWND probes exercise the fixed layout at every animation sample.
    // Production hover has no native bridge command (browser source contract).
    for (int dpi : {96, 120, 144, 192}) {
        auto scale = [dpi](int n) { return MulDiv(n, dpi, 96); };
        auto r = CalculateIslands({0, 0, 3840, 2160}, scale(56), scale(120),
            scale(14), scale(16), scale(400), scale(100), true);
        HWND apps = CreateWindowExW(0, L"STATIC", L"apps bounds probe", WS_POPUP,
            r.apps.left, r.apps.top, r.apps.right-r.apps.left, r.apps.bottom-r.apps.top,
            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        HWND system = CreateWindowExW(0, L"STATIC", L"system bounds probe", WS_POPUP,
            r.system.left, r.system.top, r.system.right-r.system.left, r.system.bottom-r.system.top,
            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        Check(apps && system, "bounds probes created");
        for (int frame = 0; frame < 180; ++frame) {
            SendMessageW(apps, frame < 120 ? WM_MOUSEMOVE : WM_MOUSELEAVE, 0, MAKELPARAM(frame, 40));
            RECT a{}, b{}; GetWindowRect(apps, &a); GetWindowRect(system, &b);
            Check(EqualRect(&a, &r.apps), "applications HWND fixed throughout hover samples");
            Check(EqualRect(&b, &r.system), "system HWND fixed throughout hover samples");
        }
        DestroyWindow(apps); DestroyWindow(system);
    }
    for (int gap : {0, 8, 16, 32}) {
        auto r = CalculateIslands({-1920, 0, 0, 1080}, 56, 188, 14, gap, 290, 100, true);
        Check(!r.stacked && r.system.left - r.apps.right == gap, "configurable gap");
        Check(r.apps.left + r.apps.right == -1920, "custom gap applications center");
        auto empty = CalculateIslands({-1920, 0, 0, 1080}, 56, 188, 14, gap, 290, 100, false);
        Check(IsRectEmpty(&empty.apps), "no empty applications placeholder");
    }
    for (bool legacy : {false, true}) {
        HICON icon = MakeIcon(legacy); auto bitmap = DecodeIcon(icon); DestroyIcon(icon);
        Check(bitmap != nullptr, "decode");
        auto resized = ResizeIcon(*bitmap, 1);
        Check(resized && resized->GetWidth() == 1 && resized->GetHeight() == 1, "explicit bicubic output size");
        Gdiplus::Color sample; resized->GetPixel(0, 0, &sample);
        Check(sample.GetA() > 0 && sample.GetA() < 255, "resample retains partial alpha");
        IStream* stream = nullptr; CreateStreamOnHGlobal(nullptr, TRUE, &stream);
        const CLSID png{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
        Check(bitmap->Save(stream, &png) == Gdiplus::Ok, "PNG encode");
        LARGE_INTEGER start{}; stream->Seek(start, STREAM_SEEK_SET, nullptr);
        {
            Gdiplus::Bitmap decoded(stream);
            Gdiplus::Color c;
            decoded.GetPixel(0, 0, &c); Check(c.GetA() == 0, "transparent pixel survives PNG");
            decoded.GetPixel(1, 0, &c); Check(c.GetA() == (legacy ? 255 : 128) && c.GetR() >= 254 && c.GetB() == 0, "red alpha and unpremultiply");
            decoded.GetPixel(0, 1, &c); Check(c.GetA() == 255 && c.GetB() == 255 && c.GetR() == 0, "blue channel order");
            decoded.GetPixel(1, 1, &c); Check(c.GetA() == 255 && c.GetR() == 0, "opaque black preserved");
        }
        stream->Release();
    }
    Gdiplus::GdiplusShutdown(token);
    CoUninitialize();
    std::printf("PASS: %d monitor/DPI/count layouts; modern and legacy icon PNG alpha/color round trips\n", layouts);
}

