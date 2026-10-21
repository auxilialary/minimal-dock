#include "WindowTracker.h"
#include "IconBitmap.h"
#include "IconIdentity.h"
#include "../web/Performance.h"
#include <commctrl.h>
#include <commoncontrols.h>
#include <wrl/client.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <objidl.h>
#include <gdiplus.h>
#include <wincrypt.h>
#include <objidl.h>
#include <map>
#include <algorithm>
#include <cwctype>
#include <memory>
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "crypt32.lib")

namespace {
    std::wstring Executable(HWND hwnd) {
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
            FALSE, pid);
        if (!process) return {};
        std::wstring result(32768, L'\0');
        DWORD length = static_cast<DWORD>(result.size());
        BOOL ok = QueryFullProcessImageNameW(
            process, 0, result.data(), &length);
        CloseHandle(process);
        if (!ok) return {};
        result.resize(length);
        return result;
    }

    std::wstring WindowTitle(HWND hwnd) {
        int length = GetWindowTextLengthW(hwnd);
        if (length <= 0) return {};
        std::wstring text(static_cast<size_t>(length) + 1, L'\0');
        GetWindowTextW(hwnd, text.data(), length + 1);
        text.resize(wcslen(text.c_str()));
        return text;
    }

    std::wstring Lower(std::wstring str) {
        std::transform(str.begin(), str.end(), str.begin(),
            [](wchar_t c) { return std::towlower(c); });
        return str;
    }

    bool Eligible(HWND hwnd, HWND a, HWND b) {
        if (hwnd == a || hwnd == b || !IsWindowVisible(hwnd))
            return false;
        if (GetWindow(hwnd, GW_OWNER) &&
            !(GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_APPWINDOW))
            return false;
        LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        if (style & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE))
            return false;

        DWORD cloaked = 0;
        if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED,
            &cloaked, sizeof(cloaked))) && cloaked)
            return false;
        if (WindowTitle(hwnd).empty()) return false;

        wchar_t cls[100]{};
        GetClassNameW(hwnd, cls, 100);
        if (wcscmp(cls, L"Progman") == 0 ||
            wcscmp(cls, L"WorkerW") == 0 ||
            wcscmp(cls, L"Shell_TrayWnd") == 0 ||
            wcscmp(cls, L"Shell_SecondaryTrayWnd") == 0)
            return false;

        return true;
    }
}

std::string WindowTracker::IconFor(const RunningApp& app) {
    HICON icon = app.hwnd ? WindowIcon(app.hwnd) : nullptr;
    const auto cacheKey = app.path + L"#" + app.identity + L"#" +
        std::to_wstring(reinterpret_cast<UINT_PTR>(app.hwnd)) + L"#" +
        std::to_wstring(reinterpret_cast<UINT_PTR>(icon)) + L"#" + std::to_wstring(iconPixels_);
    auto found = icons_.find(cacheKey);
    if (found != icons_.end()) return found->second;
    Performance::Scope timing(L"icon.cache.miss.resolve.encode");

    std::string result;
    auto bitmap = ResolveIcon(app.hwnd, app.path, app.identity, icon, iconPixels_);
    // Keep native source pixels; CSS contains the complete canvas without cropping.

    if (bitmap && bitmap->GetLastStatus() == Gdiplus::Ok) {
        IStream* stream = nullptr;
        if (SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) {
            const CLSID png = {
                0x557cf406, 0x1a04, 0x11d3,
                {0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}
            };
            if (bitmap->Save(stream, &png) == Gdiplus::Ok) {
                HGLOBAL memory{};
                if (SUCCEEDED(GetHGlobalFromStream(stream, &memory))) {
                    void* bytes = GlobalLock(memory);
                    STATSTG stat{};
                    stream->Stat(&stat, STATFLAG_NONAME);
                    DWORD size = stat.cbSize.HighPart == 0 ? stat.cbSize.LowPart : 0;
                    if (bytes && size) {
                        DWORD outputSize = 0;
                        CryptBinaryToStringA(
                            static_cast<const BYTE*>(bytes), size,
                            CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                            nullptr, &outputSize);
                        std::string encoded(outputSize, '\0');
                        if (CryptBinaryToStringA(
                            static_cast<const BYTE*>(bytes), size,
                            CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                            encoded.data(), &outputSize)) {
                            if (!encoded.empty() && encoded.back() == '\0')
                                encoded.pop_back();
                            result = "data:image/png;base64," + encoded;
                        }
                    }
                    if (bytes) GlobalUnlock(memory);
                }
            }
            stream->Release();
        }
    }

    constexpr size_t budget = 16 * 1024 * 1024;
    if (!result.empty() && result.size() <= budget) {
        while (!iconOrder_.empty() && (icons_.size() >= 256 || iconBytes_ + result.size() > budget)) {
            const auto oldest = icons_.find(iconOrder_.front());
            if (oldest != icons_.end()) { iconBytes_ -= oldest->second.size(); icons_.erase(oldest); }
            iconOrder_.pop_front();
        }
        icons_[cacheKey] = result; iconOrder_.push_back(cacheKey); iconBytes_ += result.size();
        if (Performance::enabled) Performance::Mark(L"icon.cache.bytes=" + std::to_wstring(iconBytes_) + L";entries=" + std::to_wstring(icons_.size()));
    }
    return result;
}

void WindowTracker::Refresh(HWND systemWindow, HWND appsWindow) {
    iconPixels_ = IconPhysicalSize(iconDip_ * 4, GetDpiForWindow(appsWindow));
    struct Context {
        HWND a, b, foreground;
        std::map<std::wstring, RunningApp> found;
    } context{ systemWindow, appsWindow, GetAncestor(GetForegroundWindow(), GA_ROOT), {} };


    EnumWindows([](HWND hwnd, LPARAM value) -> BOOL {
        auto& ctx = *reinterpret_cast<Context*>(value);
        if (!Eligible(hwnd, ctx.a, ctx.b)) return TRUE;

        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == GetCurrentProcessId()) return TRUE;

        std::wstring path = Executable(hwnd);
        if (path.empty()) return TRUE;
        std::wstring identity = AppIdentity(hwnd);
        std::wstring key = identity.empty() ? Lower(path) : identity;

        auto it = ctx.found.find(key);
        if (it == ctx.found.end()) {
            RunningApp app{};
            app.hwnd = hwnd;
            app.pid = pid;
            app.path = path;
            app.identity = identity;
            app.title = WindowTitle(hwnd);
            app.active = hwnd == ctx.foreground;
            ctx.found.emplace(key, std::move(app));
        } else if (hwnd == ctx.foreground) {
            it->second.hwnd = hwnd;
            it->second.title = WindowTitle(hwnd);
            it->second.active = true;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&context));

    for (auto& [key, app] : context.found) {
        if (std::find(order_.begin(), order_.end(), key) == order_.end())
            order_.push_back(key);

    }

    order_.erase(std::remove_if(order_.begin(), order_.end(),
        [&](const auto& key) {
            return context.found.find(key) == context.found.end();
        }), order_.end());

    apps_.clear();
    for (const auto& key : order_) {
        auto it = context.found.find(key);
        if (it == context.found.end()) continue;
        RunningApp app = std::move(it->second);
        app.icon = IconFor(app);
        apps_.push_back(std::move(app));
    }
}

void WindowTracker::Activate(HWND target) const {
    if (!IsWindow(target)) return;
    if (IsIconic(target)) ShowWindow(target, SW_RESTORE);
    SetForegroundWindow(target);
}
