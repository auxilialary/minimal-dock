#pragma once
#include <windows.h>
#include <cmath>

inline double WindowMotionProgress(double t) {
    if (t <= 0) return 0;
    if (t >= 1) return 1;
    const double remaining = 1 - t;
    return 1 - remaining * remaining * remaining;
}
// Preserve a shared horizontal center at every animation sample, including
// physical-pixel rounding on odd-width and negative-coordinate monitors.
inline RECT WindowMotionFrame(RECT from, RECT to, double progress) {
    auto mix = [progress](LONG a, LONG b) { return LONG(std::lround(a + (b - a) * progress)); };
    LONG width = mix(from.right - from.left, to.right - to.left);
    LONG x = mix(from.left, to.left);
    const LONG center = to.left + to.right;
    if (from.left + from.right == center) {
        if ((width & 1) != (center & 1)) ++width;
        x = (center - width) / 2;
    }
    const LONG y = mix(from.top, to.top);
    return {x, y, x + width, y + mix(from.bottom - from.top, to.bottom - to.top)};
}
class WindowMotion {
public:
    static constexpr DWORD DefaultDuration = 280;
    void Move(HWND hwnd, RECT target, DWORD duration = DefaultDuration, ULONGLONG start = 0);
    void Tick(HWND hwnd);
    void Stop(HWND hwnd);
private:
    RECT from_{}, to_{};
    ULONGLONG start_ = 0;
    DWORD duration_ = 280;
    bool running_ = false;
    static constexpr UINT_PTR timerId_ = 0x444;
public:
    static constexpr UINT_PTR TimerId() { return timerId_; }
};