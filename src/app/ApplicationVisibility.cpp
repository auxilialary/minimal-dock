#include "Application.h"
#include <dwmapi.h>
#include "../web/Diagnostics.h"
#pragma comment(lib, "dwmapi.lib")
namespace {
constexpr UINT_PTR LeaveTimer = 0x450, HideTimer = 0x451;
bool Inside(HWND hwnd, POINT point) {
    RECT rect{};
    return hwnd && IsWindowVisible(hwnd) && GetWindowRect(hwnd, &rect) && PtInRect(&rect, point);
}
}
bool Application::OwnWindow(HWND hwnd) const {
    return hwnd && (hwnd == systemWindow_.Handle() || hwnd == appsWindow_.Handle() ||
        hwnd == mediaWindow_.Handle() || hwnd == popups_.Handle());
}
bool Application::ForegroundState(RECT monitor, const IslandLayout& layout, bool& obstructed) {
    HWND foreground = GetForegroundWindow();
    if (foreground && !OwnWindow(foreground)) externalForeground_ = foreground;
    foreground = externalForeground_;
    if (!IsWindow(foreground) || IsIconic(foreground) || !IsWindowVisible(foreground)) return false;
    wchar_t name[100]{}; GetClassNameW(foreground, name, 100);
    if (!wcscmp(name, L"Progman") || !wcscmp(name, L"WorkerW") ||
        !wcscmp(name, L"Shell_TrayWnd") || !wcscmp(name, L"Shell_SecondaryTrayWnd")) return false;
    DWORD cloaked = 0;
    DwmGetWindowAttribute(foreground, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    if (cloaked) return false;
    RECT rect{};
    if (FAILED(DwmGetWindowAttribute(foreground, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect))) &&
        !GetWindowRect(foreground, &rect)) return false;
    const bool overlaps = Visibility::Overlaps(rect, layout.system) ||
        (!tracker_.Apps().empty() && Visibility::Overlaps(rect, layout.apps)) ||
        (media_.Read().playing && Visibility::Overlaps(rect, layout.media));
    obstructed = overlaps;
    return Visibility::Fullscreen(rect, monitor, IsZoomed(foreground) != FALSE);
}
void Application::HideIslands(bool immediate) {
    const HWND owner = systemWindow_.Handle();
    popups_.Hide();
    BOOL animations = TRUE; SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
    immediate = immediate || !animations;
    if (immediate) {
        KillTimer(owner, HideTimer); hiding_ = false;
        for (auto* window : {&systemWindow_, &appsWindow_, &mediaWindow_}) {
            window->Show(false); window->Fade(true, 0);
        }
    } else if (!hiding_) {
        hiding_ = true;
        const auto start = GetTickCount64();
        for (auto* window : {&systemWindow_, &appsWindow_, &mediaWindow_}) {
            if (!IsWindowVisible(window->Handle())) continue;
            RECT rect{}; GetWindowRect(window->Handle(), &rect);
            OffsetRect(&rect, 0, MulDiv(8, static_cast<int>(layoutDpi_), 96));
            window->Fade(false, 160); window->Place(rect, true, start, 160);
        }
        if (!SetTimer(owner, HideTimer, 170, nullptr)) HideIslands(true);
    }
    dockVisible_ = false;
}
void Application::RevealIsland(DockWindow& window, RECT target, bool animate, ULONGLONG start) {
    BOOL animations = TRUE; SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
    if (!IsWindowVisible(window.Handle())) {
        RECT from = target; OffsetRect(&from, 0, MulDiv(8, static_cast<int>(layoutDpi_), 96));
        window.Place(from, false); window.Fade(false, 0); window.Show(true);
    }
    window.Fade(true, animations ? 160 : 0);
    window.Place(target, animate && animations, start, 160);
}
bool Application::ApplyVisibility(const IslandLayout& layout, RECT monitor, bool sameMonitor) {
    bool obstructed = false;
    const bool fullscreen = ForegroundState(monitor, layout, obstructed);
    if (fullscreen) edgeReveal_ = false;
    edge_.Place(monitor, fullReady_ && visibilityMode_ != Visibility::Mode::Always && !fullscreen);
    const bool visible = Visibility::Show(visibilityMode_, fullscreen, obstructed, edgeReveal_);
    if (visible != dockVisible_ || fullscreen) Diagnostics::Log(L"visibility.policy mode=" +
        std::to_wstring(static_cast<int>(visibilityMode_)) + L" fullscreen=" + std::to_wstring(fullscreen) +
        L" obstructed=" + std::to_wstring(obstructed) + L" edge=" + std::to_wstring(edgeReveal_) +
        L" show=" + std::to_wstring(visible));
    groupReveal_ = visible && (!dockVisible_ || hiding_);
    if (!visible) {
        HideIslands(fullscreen || !sameMonitor || (!dockVisible_ && !hiding_));
        return false;
    }
    if (hiding_) { KillTimer(systemWindow_.Handle(), HideTimer); hiding_ = false; }
    dockVisible_ = true;
    return true;
}
bool Application::VisibilityMessage(UINT message, WPARAM wp) {
    const HWND owner = systemWindow_.Handle();
    if (message == WM_APP + 72) {
        HWND foreground = GetForegroundWindow();
        if (foreground && !OwnWindow(foreground)) {
            if (externalForeground_ != foreground) { edgeReveal_ = false; popups_.Hide(); }
        }
        Layout(); return true;
    }
    if (message == EdgeTrigger::Enter) {
        if (!fullReady_ || visibilityMode_ == Visibility::Mode::Always) return true;
        // Recheck foreground synchronously: queued edge events must never reveal over a game.
        MONITORINFO info{sizeof(info)};
        GetMonitorInfoW(layoutMonitor_, &info);
        bool obstructed = false;
        if (!ForegroundState(info.rcMonitor, {}, obstructed)) {
            if (!edgeReveal_) { edgeReveal_ = true; Layout(); }
            SetTimer(owner, LeaveTimer, 600, nullptr);
        }
        return true;
    }
    if (message == WM_MOUSEMOVE || message == WM_MOUSELEAVE || message == EdgeTrigger::Pointer) {
        if (visibilityMode_ != Visibility::Mode::Always && dockVisible_)
            SetTimer(owner, LeaveTimer, 350, nullptr);
        return message == EdgeTrigger::Pointer;
    }
    if (message == WM_TIMER && wp == HideTimer) {
        KillTimer(owner, HideTimer);
        if (hiding_) HideIslands(true);
        return true;
    }
    if (message == WM_TIMER && wp == LeaveTimer) {
        KillTimer(owner, LeaveTimer);
        POINT point{};
        if (popups_.MenuOpen() || !GetCursorPos(&point)) return true;
        if (GetCapture() && OwnWindow(GetCapture())) return true;
        if (Inside(systemWindow_.Handle(), point) || Inside(appsWindow_.Handle(), point) ||
            Inside(mediaWindow_.Handle(), point) || (popups_.Interactive() && Inside(popups_.Handle(), point))) return true;
        // A short grace period bridges the monitor edge, gaps and popup margins.
        edgeReveal_ = false;
        if (visibilityMode_ == Visibility::Mode::Auto) HideIslands(false);
        else Layout();
        return true;
    }
    return false;
}
