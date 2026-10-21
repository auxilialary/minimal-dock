#include "WindowMotion.h"
#include <cmath>

void WindowMotion::Move(HWND hwnd, RECT target, DWORD duration, ULONGLONG start) {
    if (!hwnd) return;
    if (running_ && EqualRect(&target, &to_)) return;

    RECT current{};
    GetWindowRect(hwnd, &current);
    if (EqualRect(&current, &target)) { Stop(hwnd); return; }

    if (!IsWindowVisible(hwnd) || duration == 0) {
        Stop(hwnd);
        SetWindowPos(hwnd, nullptr, target.left, target.top,
            target.right - target.left, target.bottom - target.top,
            SWP_NOACTIVATE | SWP_NOZORDER);
        return;
    }

    from_ = current;
    to_ = target;
    start_ = start ? start : GetTickCount64();
    duration_ = duration;
    running_ = true;
    if (!SetTimer(hwnd, timerId_, 16, nullptr)) {
        Stop(hwnd);
        SetWindowPos(hwnd, nullptr, target.left, target.top,
            target.right - target.left, target.bottom - target.top,
            SWP_NOACTIVATE | SWP_NOZORDER);
    }
}

void WindowMotion::Tick(HWND hwnd) {
    if (!running_) return;
    double t = double(GetTickCount64() - start_) / double(duration_);
    if (t > 1.0) t = 1.0;
    double e = WindowMotionProgress(t);

    const RECT frame = WindowMotionFrame(from_, to_, e);
    SetWindowPos(hwnd, nullptr, frame.left, frame.top,
        frame.right - frame.left, frame.bottom - frame.top,
        SWP_NOACTIVATE | SWP_NOZORDER);
    if (t >= 1.0) { Stop(hwnd); PostMessageW(hwnd, WM_APP + 21, 0, 0); }
}

void WindowMotion::Stop(HWND hwnd) {
    if (running_) KillTimer(hwnd, timerId_);
    running_ = false;
}