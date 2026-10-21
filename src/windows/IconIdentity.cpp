#include "IconIdentity.h"
#include <shobjidl.h>
#include <propkey.h>
#include <propvarutil.h>
#include <appmodel.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")
#include <wrl/client.h>
#pragma comment(lib, "propsys.lib")
using Microsoft::WRL::ComPtr;
std::wstring AppIdentity(HWND window) {
    ComPtr<IPropertyStore> store;
    PROPVARIANT value{};
    std::wstring result;
    if (SUCCEEDED(SHGetPropertyStoreForWindow(window, IID_PPV_ARGS(&store)))) {
        if (SUCCEEDED(store->GetValue(PKEY_AppUserModel_ID, &value)) && value.vt == VT_LPWSTR)
            result = value.pwszVal;
        PropVariantClear(&value);
    }
    if (!result.empty()) return result;
    DWORD pid = 0; GetWindowThreadProcessId(window, &pid);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process) {
        UINT32 length = 0;
        if (GetApplicationUserModelId(process, &length, nullptr) == ERROR_INSUFFICIENT_BUFFER) {
            std::wstring id(length, L'\0');
            if (GetApplicationUserModelId(process, &length, id.data()) == ERROR_SUCCESS) {
                id.resize(length ? length - 1 : 0); result = id;
            }
        }
        CloseHandle(process);
    }
    return result;
}
HICON WindowIcon(HWND window) {
    DWORD_PTR result = 0;
    for (WPARAM kind : { WPARAM(ICON_BIG), WPARAM(ICON_SMALL2), WPARAM(ICON_SMALL) }) {
        if (SendMessageTimeoutW(window, WM_GETICON, kind, GetDpiForWindow(window),
            SMTO_ABORTIFHUNG | SMTO_BLOCK, 40, &result) && result)
            return reinterpret_cast<HICON>(result);
    }
    auto icon = reinterpret_cast<HICON>(GetClassLongPtrW(window, GCLP_HICON));
    return icon ? icon : reinterpret_cast<HICON>(GetClassLongPtrW(window, GCLP_HICONSM));
}
namespace {
bool SameArtwork(Gdiplus::Bitmap& a, Gdiplus::Bitmap& b) {
    auto smallA = ResizeIcon(a, 16), smallB = ResizeIcon(b, 16);
    if (!smallA || !smallB) return false;
    unsigned difference = 0;
    for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) {
        Gdiplus::Color ca, cb; smallA->GetPixel(x, y, &ca); smallB->GetPixel(x, y, &cb);
        difference += abs(int(ca.GetA()) - cb.GetA());
        difference += abs(int(ca.GetR()) * ca.GetA() / 255 - int(cb.GetR()) * cb.GetA() / 255);
        difference += abs(int(ca.GetG()) * ca.GetA() / 255 - int(cb.GetG()) * cb.GetA() / 255);
        difference += abs(int(ca.GetB()) * ca.GetA() / 255 - int(cb.GetB()) * cb.GetA() / 255);
    }
    return difference < 16 * 16 * 4 * 12;
}
}
std::unique_ptr<Gdiplus::Bitmap> ResolveIcon(HWND hwnd, const std::wstring& path,
    const std::wstring& identity, HICON icon, int pixels) {
    auto window = icon ? DecodeIcon(icon) : nullptr;
    // AUMID distinguishes packaged apps sharing a host and desktop shortcut identities.
    if (identity.find(L'!') != std::wstring::npos) {
        auto shell = LoadShellIcon((L"shell:AppsFolder\\" + identity).c_str(), pixels);
        if (shell) return shell;
    }
    ComPtr<IPropertyStore> store;
    PROPVARIANT resourceValue{};
    if (!identity.empty() && SUCCEEDED(SHGetPropertyStoreForWindow(hwnd, IID_PPV_ARGS(&store)))) {
        if (SUCCEEDED(store->GetValue(PKEY_AppUserModel_RelaunchIconResource, &resourceValue)) &&
            resourceValue.vt == VT_LPWSTR) {
            wchar_t resourcePath[32768]{};
            ExpandEnvironmentStringsW(resourceValue.pwszVal, resourcePath, 32768);
            const int index = PathParseIconLocationW(resourcePath);
            HICON relaunch = nullptr;
            if (SUCCEEDED(SHDefExtractIconW(resourcePath, index, 0, &relaunch, nullptr, pixels)) && relaunch) {
                auto decoded = DecodeIcon(relaunch); DestroyIcon(relaunch);
                PropVariantClear(&resourceValue);
                if (decoded) return decoded;
            }
        }
        PropVariantClear(&resourceValue);
    }
    if (window && window->GetWidth() >= static_cast<UINT>(pixels)) return window;
    // Retain a window-specific icon even if only a small source is exposed: an
    // executable's larger launcher icon is not an identity-preserving substitute.
    if (window) {
        auto large = LoadShellIcon(path.c_str(), pixels);
        if (large && large->GetWidth() > window->GetWidth() && SameArtwork(*window, *large)) return large;
        return window;
    }
    auto shell = LoadShellIcon(path.c_str(), pixels);
    if (shell) return shell;
    HICON resource = nullptr;
    if (SUCCEEDED(SHDefExtractIconW(path.c_str(), 0, 0, &resource, nullptr, pixels)) && resource) {
        auto decoded = DecodeIcon(resource); DestroyIcon(resource); return decoded;
    }
    return {};
}
