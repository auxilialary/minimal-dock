#include "Application.h"
#include "../windows/IslandLayout.h"
#include "../web/Diagnostics.h"
#include <shellscalingapi.h>

void Application::Layout() {
    Performance::Scope timing(L"ui.layout");
    if (!systemView_.Ready()) return;

    MONITORINFO info{ sizeof(info) };
    HMONITOR monitor = MonitorFromWindow(
        systemWindow_.Handle(), MONITOR_DEFAULTTOPRIMARY);
    if (!GetMonitorInfoW(monitor, &info)) return;

    UINT dpiX = 96, dpiY = 96;
    GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    auto scale = [dpiX](int value) { return MulDiv(value, static_cast<int>(dpiX), 96); };
    const bool running = !tracker_.Apps().empty();
    const bool mediaPlaying = media_.Read().playing && mediaView_.Painted();
    auto layout = CalculateMediaIslands(info.rcMonitor, scale(dockHeight_),
        scale(systemWidth_), scale(margin_), scale(gap_),
        scale(appsPadding_ + static_cast<int>(tracker_.Apps().size()) * appStep_),
        scale(minimumWidth_), running, mediaPlaying ? scale(mediaWidth_) : 0);
    // Keep a stable selected monitor: the system HWND starts on the primary and
    // stays there across foreground changes. Never animate across DPI/topology changes.
    const bool sameMonitor = monitor == layoutMonitor_ && dpiX == layoutDpi_ &&
        EqualRect(&layoutBounds_, &info.rcMonitor);
    layoutMonitor_ = monitor;
    layoutDpi_ = dpiX;
    layoutBounds_ = info.rcMonitor;
    if (!ApplyVisibility(layout, info.rcMonitor, sameMonitor)) return;
    // Hide before the system crosses the center on the last-app transition.
    if (!running) appsWindow_.Show(false);
    const ULONGLONG motionStart = GetTickCount64();
    if (mediaPlaying && groupReveal_) {
        RevealIsland(mediaWindow_, layout.media, sameMonitor, motionStart);
    } else if (mediaPlaying) {
        const bool appearing = !mediaVisible_ || !IsWindowVisible(mediaWindow_.Handle());
        KillTimer(systemWindow_.Handle(), 0x447);
        if (!IsWindowVisible(mediaWindow_.Handle())) {
            RECT start = layout.media; OffsetRect(&start, -scale(8), 0);
            mediaWindow_.Place(start, false); mediaWindow_.Fade(false, 0);
            mediaWindow_.Show(true);
        }
        if (appearing) mediaWindow_.Fade(true, 160);
        mediaWindow_.Place(layout.media, sameMonitor, motionStart);
    } else if (mediaVisible_ && IsWindowVisible(mediaWindow_.Handle())) {
        mediaWindow_.Fade(false, 160);
        RECT end{}; GetWindowRect(mediaWindow_.Handle(), &end); OffsetRect(&end, -scale(8), 0);
        mediaWindow_.Place(end, sameMonitor, motionStart);
        if (!SetTimer(systemWindow_.Handle(), 0x447, 160, nullptr)) mediaWindow_.Show(false);
    }
    mediaVisible_ = mediaPlaying;
    if (running && appsView_.Ready()) {
        if (!IsWindowVisible(appsWindow_.Handle())) {
            // Vacate the center before showing the anchored applications surface.
            systemWindow_.Place(layout.system, false);
            appsWindow_.Place(layout.apps, false);
        }
        if (groupReveal_) RevealIsland(appsWindow_, layout.apps, sameMonitor, motionStart);
        else { appsWindow_.Place(layout.apps, sameMonitor, motionStart); appsWindow_.Show(true); }
    } else appsWindow_.Show(false);
    if (groupReveal_) RevealIsland(systemWindow_, layout.system, sameMonitor, motionStart);
    else { systemWindow_.Place(layout.system, sameMonitor, motionStart); systemWindow_.Show(true); }
    popups_.FollowMediaAnchor();
    if (!tracker_.Apps().empty() && appsView_.Painted()) {
        static bool firstApps = false;
        if (!firstApps) { firstApps = true; Performance::Mark(L"island.apps.visible"); }
    }
    if (GetEnvironmentVariableW(L"GLASSSPLIT_DIAGNOSTICS", nullptr, 0)) {
        auto rect = [](RECT r) { return std::to_wstring(r.left) + L"," + std::to_wstring(r.top) +
            L"," + std::to_wstring(r.right) + L"," + std::to_wstring(r.bottom); };
        Diagnostics::Log(L"Layout monitor=" + rect(info.rcMonitor) + L" dpi=" +
            std::to_wstring(dpiX) + L" system target=" + rect(layout.system) +
            L" apps target=" + rect(layout.apps) + L" stacked=" + std::to_wstring(layout.stacked));
    }
}

