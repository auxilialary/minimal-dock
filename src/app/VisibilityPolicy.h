#pragma once
#include <windows.h>
#include <cstdlib>
namespace Visibility {
enum class Mode : DWORD { Always, Auto, Smart };
inline Mode Decode(DWORD value) { return value <= 2 ? static_cast<Mode>(value) : Mode::Always; }
inline bool Show(Mode mode, bool fullscreen, bool obstructed, bool reveal) {
    return !fullscreen && (mode == Mode::Always || reveal || (mode == Mode::Smart && !obstructed));
}
inline bool Overlaps(RECT a, RECT b) { RECT intersection{}; return IntersectRect(&intersection, &a, &b) != FALSE; }
inline bool Fullscreen(RECT a, RECT monitor, bool maximized) {
    return !maximized && abs(a.left - monitor.left) <= 3 && abs(a.top - monitor.top) <= 3 &&
        abs(a.right - monitor.right) <= 3 && abs(a.bottom - monitor.bottom) <= 3;
}
inline Mode Load() {
    DWORD value = 0, bytes = sizeof(value);
    const auto result = RegGetValueW(HKEY_CURRENT_USER, L"Software\\GlassSplit", L"VisibilityMode",
        RRF_RT_REG_DWORD, nullptr, &value, &bytes);
    return result == ERROR_SUCCESS ? Decode(value) : Mode::Always;
}
inline LSTATUS Save(Mode mode) {
    const DWORD value = static_cast<DWORD>(mode);
    return RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\GlassSplit", L"VisibilityMode", REG_DWORD, &value, sizeof(value));
}
}
