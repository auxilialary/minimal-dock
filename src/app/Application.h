#pragma once
#include <windows.h>
#include <objbase.h>
#include <WebView2.h>
#include <wrl/client.h>
#include <string>
#include "../windows/DockWindow.h"
#include "../windows/WindowTracker.h"
#include "../windows/WindowInventory.h"
#include "../web/WebViewHost.h"
#include "../shell/TaskbarManager.h"
#include "../safety/Watchdog.h"
#include "Popups.h"
#include "VisibilityPolicy.h"
#include "../windows/EdgeTrigger.h"
#include "../windows/IslandLayout.h"
#include "../media/MediaService.h"

static_assert(MediaService::Changed != EdgeTrigger::Enter &&
    MediaService::Changed != EdgeTrigger::Pointer, "Media events must bypass the visibility input handler");

class Application {
public:
    ~Application() { Stop(); }
    int Run(HINSTANCE instance);
private:
    static void CALLBACK ForegroundEvent(HWINEVENTHOOK, DWORD,
        HWND, LONG, LONG, DWORD, DWORD);
    void OnMessage(UINT message, WPARAM wp, LPARAM lp);
    void OnEnvironment(HRESULT hr, ICoreWebView2Environment* environment);
    void RequestEnvironment();
    void RetryEnvironment();
    void Refresh();
    void Update();
    void Layout();
    void PublishMedia();
    void Command(const std::wstring& command);
    void ShowMenu();
    void Stop();
    bool VisibilityMessage(UINT message, WPARAM wp);
    bool ApplyVisibility(const IslandLayout& layout, RECT monitor, bool sameMonitor);
    bool OwnWindow(HWND hwnd) const;
    bool ForegroundState(RECT monitor, const IslandLayout& layout, bool& obstructed);
    void HideIslands(bool immediate);
    void RevealIsland(DockWindow& window, RECT target, bool animate, ULONGLONG start);
    EdgeTrigger edge_;
    Visibility::Mode visibilityMode_ = Visibility::Mode::Always;
    HWND externalForeground_ = nullptr;
    HWINEVENTHOOK geometryHook_ = nullptr;
    UINT taskbarCreated_ = 0;
    bool edgeReveal_ = false, dockVisible_ = false, hiding_ = false, menuOpen_ = false;
    bool groupReveal_ = false;

    std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
    DockWindow systemWindow_, appsWindow_, mediaWindow_;
    WebViewHost systemView_, appsView_, mediaView_;
    MediaService media_;
    int mediaWidth_ = 220;
    bool mediaVisible_ = false;
    WindowInventory tracker_;
    TaskbarManager taskbar_;
    Watchdog watchdog_;
    Popups popups_;
    int radiusDip_ = 17;
    float borderOpacity_ = .23f;
    Microsoft::WRL::ComPtr<ICoreWebView2Environment> environment_;
    std::wstring uiDirectory_, webData_;
    unsigned environmentAttempts_ = 0;
    int dockHeight_ = 0, systemWidth_ = 0, margin_ = 0, gap_ = 0;
    int appsPadding_ = 0, appStep_ = 0, minimumWidth_ = 0;
    HMONITOR layoutMonitor_ = nullptr;
    UINT layoutDpi_ = 0;
    RECT layoutBounds_{};
    UINT shellMessage_ = 0;
    HWINEVENTHOOK foregroundHook_ = nullptr;
    bool appsStarted_ = false;
    bool protectedTaskbar_ = false;
    bool watchdogStarted_ = false;
    bool refreshPending_ = false;
    bool stopping_ = false;
    bool usable_ = false, fullReady_ = false, mediaStarted_ = false;
    bool mediaViewStarted_ = false;
    std::wstring lastAppsJson_;
};
