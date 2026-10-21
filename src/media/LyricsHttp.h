#pragma once
#include <windows.h>
#include <winhttp.h>
#include <algorithm>
#include <string>
#include <limits>
namespace LyricsHttp {
inline std::wstring UserAgent(const std::wstring& contact) {
    if (contact.empty() || contact.size() > 1024) return {};
    for (auto c : contact) if (c <= L' ' || c == L'(' || c == L')' || c == L'<' || c == L'>') return {};
    bool valid = false;
    if (contact.starts_with(L"https://") || contact.starts_with(L"http://")) {
        URL_COMPONENTS url{}; url.dwStructSize = sizeof(url); url.dwHostNameLength = static_cast<DWORD>(-1);
        valid = WinHttpCrackUrl(contact.c_str(), static_cast<DWORD>(contact.size()), 0, &url) && url.dwHostNameLength > 0;
    } else {
        auto at = contact.find(L'@');
        valid = at != std::wstring::npos && at > 0 && contact.find(L'@', at + 1) == std::wstring::npos &&
            contact.find(L'.', at + 2) != std::wstring::npos && contact.back() != L'.' && contact.find_first_of(L"/\\:;") == std::wstring::npos;
    }
    return valid ? L"GlassSplitDock/1.0 (" + contact + L")" : L"";
}
inline std::wstring ConfiguredUserAgent() {
    wchar_t contact[1026]{};
    const auto length = GetEnvironmentVariableW(L"GLASSSPLIT_LRCLIB_CONTACT", contact, static_cast<DWORD>(std::size(contact)));
    if (!length) return UserAgent(L"https://github.com/auxilialary/minimal-dock");
    return length < std::size(contact) ? UserAgent(contact) : L"";
}
inline unsigned RetrySeconds(const std::wstring& value, unsigned fallback = 60) {
    if (value.empty()) return fallback;
    if (value.find_first_not_of(L"0123456789") == std::wstring::npos) {
        // Saturate rather than wrapping a long server-supplied cooldown.
        unsigned long long seconds = 0;
        for (auto c : value) seconds = std::min<unsigned long long>(MAXDWORD, seconds * 10 + (c - L'0'));
        return static_cast<unsigned>(std::max(1ull, seconds));
    }
    if (value.size() < 20 || value.find(L"GMT") == std::wstring::npos) return fallback;
    SYSTEMTIME date{}; FILETIME ft{};
    if (!WinHttpTimeToSystemTime(value.c_str(), &date) || !SystemTimeToFileTime(&date, &ft)) return fallback;
    ULARGE_INTEGER target{}, now{}; target.LowPart = ft.dwLowDateTime; target.HighPart = ft.dwHighDateTime;
    GetSystemTimeAsFileTime(&ft); now.LowPart = ft.dwLowDateTime; now.HighPart = ft.dwHighDateTime;
    return target.QuadPart > now.QuadPart ? static_cast<unsigned>(std::min<ULONGLONG>((target.QuadPart - now.QuadPart) / 10000000 + 1, MAXDWORD)) : 1;
}
}
