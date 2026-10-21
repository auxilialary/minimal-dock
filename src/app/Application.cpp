#include "Application.h"
#include "StartupPolicy.h"
#include "../windows/IslandLayout.h"
#include <shellscalingapi.h>
#pragma comment(lib, "shcore.lib")
#include "../web/Diagnostics.h"
#include "../web/WebBridge.h"
#include <wrl/event.h>
#include <objidl.h>
#include <shellapi.h>
#include <gdiplus.h>
#include <filesystem>
#include <fstream>
#include <regex>
#include <iterator>
#include <algorithm>
#include <string>
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")

using Microsoft::WRL::Callback;

namespace {
    HWND eventWindow = nullptr;
    constexpr UINT RefreshMessage = WM_APP + 20;
    constexpr UINT_PTR RefreshTimer = 0x445;
    constexpr UINT_PTR StartupTimer = 0x446;

    int Scaled(HWND hwnd, int value) {
        return MulDiv(value, GetDpiForWindow(hwnd), 96);
    }
}

void CALLBACK Application::ForegroundEvent(HWINEVENTHOOK, DWORD event,
    HWND hwnd, LONG object, LONG child, DWORD, DWORD) {
    if (eventWindow && object == OBJID_WINDOW && child == CHILDID_SELF && hwnd == GetForegroundWindow()) {
        PostMessageW(eventWindow, WM_APP + 72, 0, 0);
        if (event == EVENT_SYSTEM_FOREGROUND) PostMessageW(eventWindow, RefreshMessage, 0, 0);
    }
}

int Application::Run(HINSTANCE instance) {
    Performance::Scope nativeTiming(L"application.run");
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    uiDirectory_ = (std::filesystem::path(exe).parent_path() / L"ui").wstring();

    std::ifstream theme(std::filesystem::path(uiDirectory_) / L"theme.css");
    std::string css((std::istreambuf_iterator<char>(theme)), {});
    auto metric = [&css](const char* name) {
        std::smatch match;
        if (!std::regex_search(css, match, std::regex(std::string(name) + R"(:\s*([0-9]+)px)")))
            throw std::runtime_error("Missing theme metric");
        return std::stoi(match[1]);
    };
    tracker_.SetIconDip(metric("--dock-icon-size"));
    const int radiusDip = metric("--dock-radius");
    std::smatch opacityMatch;
    if (!std::regex_search(css, opacityMatch, std::regex(R"(--dock-border-opacity:\s*([0-9.]+))")))
        throw std::runtime_error("Missing border opacity");
    const float borderOpacity = std::stof(opacityMatch[1]);
    radiusDip_ = radiusDip; borderOpacity_ = borderOpacity;
    dockHeight_ = metric("--dock-height");
    systemWidth_ = metric("--system-width");
    mediaWidth_ = metric("--media-width");
    margin_ = metric("--window-margin");
    gap_ = metric("--window-gap");
    appsPadding_ = metric("--apps-padding-total");
    appStep_ = metric("--app-step");
    minimumWidth_ = metric("--apps-min-width");
    Performance::Mark(L"native.theme.loaded");

    if (!systemWindow_.Create(instance, L"Dock System",
        [this](UINT m, WPARAM w, LPARAM l) { OnMessage(m, w, l); }, radiusDip, borderOpacity))
        return 1;
    if (!appsWindow_.Create(instance, L"Dock Applications",
        [this](UINT m, WPARAM w, LPARAM l) {
            if (m == WM_DPICHANGED || m == WM_MOUSEMOVE || m == WM_MOUSELEAVE) OnMessage(m, w, l);
        }, radiusDip, borderOpacity))
        return 1;

    if (!mediaWindow_.Create(instance, L"Dock Now Playing",
        [this](UINT m, WPARAM w, LPARAM l) { if (m == WM_DPICHANGED || m == WM_MOUSEMOVE || m == WM_MOUSELEAVE) OnMessage(m, w, l); }, radiusDip, borderOpacity)) return 1;
    mediaWindow_.Place({0, 0, Scaled(systemWindow_.Handle(), mediaWidth_), Scaled(systemWindow_.Handle(), dockHeight_)}, false);
    mediaWindow_.Attach(&mediaView_);
    Performance::Mark(L"windows.composition.created");

    // Give WebView a real viewport before navigation or readiness measurements.
    const int initialHeight = Scaled(systemWindow_.Handle(), dockHeight_);
    systemWindow_.Place({ 0, 0, Scaled(systemWindow_.Handle(), systemWidth_), initialHeight }, false);
    appsWindow_.Place({ 0, 0, Scaled(appsWindow_.Handle(), minimumWidth_), initialHeight }, false);
    systemWindow_.Attach(&systemView_);
    appsWindow_.Attach(&appsView_);

    eventWindow = systemWindow_.Handle();
    visibilityMode_ = Visibility::Load();
    appOrder_.Load();
    if (!edge_.Create(eventWindow)) return 1;
    taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");
    geometryHook_ = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE,
        nullptr, ForegroundEvent, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    shellMessage_ = RegisterWindowMessageW(L"SHELLHOOK");
    RegisterShellHookWindow(eventWindow);
    foregroundHook_ = SetWinEventHook(EVENT_SYSTEM_FOREGROUND,
        EVENT_SYSTEM_FOREGROUND, nullptr, ForegroundEvent,
        0, 0, WINEVENT_OUTOFCONTEXT);

    RegisterHotKey(eventWindow, 1,
        MOD_CONTROL | MOD_SHIFT | MOD_ALT, 'D');

    Gdiplus::GdiplusStartupInput input;
    ULONG_PTR gdiplusToken = 0;
    if (Gdiplus::GdiplusStartup(&gdiplusToken, &input, nullptr) != Gdiplus::Ok) return 1;
    tracker_.Start(eventWindow, appsWindow_.Handle());
    // Covers environment creation as well as controller/paint handshakes at logon.
    if (!SetTimer(eventWindow, StartupTimer, 30000, nullptr)) {
        Stop(); Gdiplus::GdiplusShutdown(gdiplusToken); return 1;
    }

    wchar_t local[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    std::wstring data = n && n < MAX_PATH
        ? std::wstring(local) + L"\\GlassSplit\\WebView2"
        : (std::filesystem::path(exe).parent_path() / L"WebView2Data").wstring();

    webData_ = data;
    RequestEnvironment();

    MSG msg{};
    int result = 0;
    BOOL messageResult;
    while ((messageResult = GetMessageW(&msg, nullptr, 0, 0)) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    result = messageResult == -1 ? 1 : static_cast<int>(msg.wParam);
    Stop();
    Gdiplus::GdiplusShutdown(gdiplusToken);
    return result;
}

void Application::OnEnvironment(HRESULT hr,
    ICoreWebView2Environment* environment) {
    Performance::Mark(L"environment.completed");
    if (stopping_) return;
    if (FAILED(hr) || !environment) {
        RetryEnvironment();
        return;
    }

    LPWSTR version = nullptr;
    if (SUCCEEDED(environment->get_BrowserVersionString(&version)) && version) {
        Diagnostics::Log(std::wstring(L"WebView runtime: ") + version);
        CoTaskMemFree(version);
    }
    environment_ = environment;
    if (!popups_.Initialize(environment, uiDirectory_, systemWindow_.Handle(), appsWindow_.Handle(),
        radiusDip_, borderOpacity_)) { PostQuitMessage(1); return; }
    if (!systemView_.Create(environment_.Get(), systemWindow_.Handle(), systemWindow_.WebRoot(),
        uiDirectory_, L"system",
        [this]() {
            Update();
        },
        [this](const std::wstring& command) { Command(command); })) {
        PostQuitMessage(1);
    }
    Update();
}

void Application::OnMessage(UINT message, WPARAM wp, LPARAM) {
    if (stopping_) return;
    if (VisibilityMessage(message, wp)) return;
    if (message == taskbarCreated_) { Update(); return; }
    if (message == WM_TIMER && wp == 0x449) { Update(); return; }
    if (message == WM_TIMER && wp == 0x448) {
        KillTimer(eventWindow, 0x448); RequestEnvironment(); return;
    }
    if (message == WindowInventory::Changed) { if (tracker_.Take()) Update(); return; }
    if (message == Watchdog::ReadyMessage) { if (wp) Update(); else PostQuitMessage(1); return; }
    if (message == MediaService::Changed) { PublishMedia(); Update(); return; }
    if (message == AudioMixer::Changed) { popups_.AudioChanged(); return; }
    if (message == WM_TIMER && wp == 0x447) {
        KillTimer(eventWindow, 0x447);
        if (!mediaVisible_) mediaWindow_.Show(false);
        return;
    }
    if (message == WM_ENDSESSION && wp) {
        taskbar_.Restore();
        watchdog_.MarkClean();
        PostQuitMessage(0);
        return;
    }
    if (message == WM_APP + 21) { Layout(); return; }
    if (message == shellMessage_ ||
        message == RefreshMessage ||
        message == WM_DISPLAYCHANGE ||
        message == WM_DPICHANGED ||
        message == WM_SETTINGCHANGE) {
        if (!refreshPending_) {
            refreshPending_ = true;
            if (!SetTimer(eventWindow, RefreshTimer, 140, nullptr)) {
                refreshPending_ = false;
                Refresh();
            }
        }
    } else if (message == WM_TIMER && wp == RefreshTimer) {
        KillTimer(eventWindow, RefreshTimer);
        refreshPending_ = false;
        Refresh();
    } else if (message == WM_TIMER && wp == StartupTimer) {
        KillTimer(eventWindow, StartupTimer);
        if (!fullReady_) {
            Diagnostics::Log(L"Startup timeout: interface did not pass rendered readiness");
            PostQuitMessage(1);
        }
    } else if (message == WM_HOTKEY && wp == 1) {
        PostQuitMessage(0);
    }
}

void Application::Refresh() {
    if (stopping_) return;
    tracker_.Request();
    Layout();
}

void Application::Update() {
    if (stopping_) return;
    popups_.Refresh(tracker_.Apps());

    if (!tracker_.Apps().empty() && environment_ && !appsStarted_) {
        appsStarted_ = true;
        lastAppsJson_.clear();
        if (!appsView_.Create(environment_.Get(), appsWindow_.Handle(), appsWindow_.WebRoot(),
            uiDirectory_, L"apps",
            [this]() { Update(); },
            [this](const std::wstring& command) { Command(command); })) {
            appsStarted_ = false;
            Diagnostics::Log(L"Applications WebView creation failed");
            PostQuitMessage(1);
        }
    }

    if (tracker_.Apps().empty() && appsStarted_ && appsView_.Ready()) {
        appsWindow_.Show(false);
        appsView_.Close();
        appsStarted_ = false;
    }

    if (appsView_.Ready()) {
        auto json = WebBridge::AppsJson(appOrder_.Sort(tracker_.Apps()));
        if (json != lastAppsJson_) { appsView_.Post(json); lastAppsJson_ = std::move(json); }
    }


    // Rendering is initialization; visibility and Explorer availability are independent.
    if (!usable_ && systemView_.Painted()) {
        usable_ = true; Performance::Mark(L"island.system.usable"); Performance::Memory();
        if (!mediaStarted_) { mediaStarted_ = true; media_.Start(systemWindow_.Handle()); }
    }
    const bool rendered = StartupPolicy::Initialized(systemView_.Painted(), tracker_.Ready(),
        tracker_.Apps().empty() || appsView_.Painted(), media_.Ready(),
        !media_.Read().playing || mediaView_.Painted());
    if (rendered && !fullReady_) {
        fullReady_ = true; KillTimer(eventWindow, StartupTimer); KillTimer(eventWindow, 0x449);
        Performance::Mark(L"application.ready");
    }
    if (!fullReady_) SetTimer(eventWindow, 0x449, 250, nullptr);
    if (rendered && !Diagnostics::Enabled()) {
        if (!watchdogStarted_) {
            watchdogStarted_ = true;
            if (!watchdog_.Start(systemWindow_.Handle())) { PostQuitMessage(1); return; }
        }
        if (StartupPolicy::CanProtect(systemView_.Painted(), tracker_.Ready(),
            tracker_.Apps().empty() || appsView_.Painted(), watchdog_.Ready(),
            FindWindowW(L"Shell_TrayWnd", nullptr) != nullptr)) {
            taskbar_.Hide(); protectedTaskbar_ = true;
        }
    }
    Layout();
}

void Application::Command(const std::wstring& command) {
    if (!dockVisible_ && command != L"exit") return;
    if (command.rfind(L"reorder:", 0) == 0) {
        appOrder_.Set(command.substr(7), tracker_.Apps()); lastAppsJson_.clear(); Update(); return;
    }
    if (popups_.Command(command, tracker_.Apps())) return;
    popups_.Hide();
    if (command == L"menu") {
        ShowMenu();
    } else if (command == L"exit") {
        PostQuitMessage(0);
    } else if (command == L"sound") {
        ShellExecuteW(nullptr, L"open", L"ms-settings:sound",
            nullptr, nullptr, SW_SHOWNORMAL);
    } else if (command == L"network") {
        ShellExecuteW(nullptr, L"open", L"ms-settings:network",
            nullptr, nullptr, SW_SHOWNORMAL);
    } else if (command.rfind(L"activate:", 0) == 0) {
        try {
            UINT_PTR id = static_cast<UINT_PTR>(
                std::stoull(command.substr(9)));
            HWND hwnd = reinterpret_cast<HWND>(id);
            auto& apps = tracker_.Apps();
            bool known = std::any_of(apps.begin(), apps.end(),
                [hwnd](const RunningApp& app) { return app.hwnd == hwnd; });
            if (known) tracker_.Activate(hwnd);
        } catch (...) {}
    }
}

void Application::Stop() {
    if (stopping_) return;
    stopping_ = true;
    lifetime_.reset();
    taskbar_.Restore();
    watchdog_.MarkClean(); // Join/cancel callbacks while the owner HWND still exists.

    edge_.Stop();
    if (geometryHook_) UnhookWinEvent(geometryHook_);
    if (foregroundHook_) UnhookWinEvent(foregroundHook_);
    if (eventWindow) {
        DeregisterShellHookWindow(eventWindow);
        UnregisterHotKey(eventWindow, 1);
        KillTimer(eventWindow, RefreshTimer);
        KillTimer(eventWindow, StartupTimer);
        KillTimer(eventWindow, 0x448); KillTimer(eventWindow, 0x449);
        KillTimer(eventWindow, 0x450); KillTimer(eventWindow, 0x451);
    }
    eventWindow = nullptr;

    tracker_.Stop();
    media_.Stop();
    KillTimer(systemWindow_.Handle(), 0x447);
    popups_.Stop();
    mediaView_.Close();
    appsView_.Close();
    systemView_.Close();
    mediaWindow_.Destroy();
    appsWindow_.Destroy();
    systemWindow_.Destroy();

    taskbar_.Restore();
    environment_.Reset();
}
