#pragma once
#include "WindowTracker.h"
#include "../web/Performance.h"
#include <condition_variable>
#include <mutex>
#include <thread>
#include <functional>
#include <objbase.h>

// One worker owns all shell COM objects and the icon cache. Only snapshots cross
// apartments; requests coalesce while shell extensions or foreign windows stall.
class WindowInventory {
public:
    static constexpr UINT Changed = WM_APP + 22;
    using Scanner = std::function<std::vector<RunningApp>()>;
    explicit WindowInventory(Scanner scanner = {}) : scanner_(std::move(scanner)) {}
    ~WindowInventory() { Stop(); }
    void SetIconDip(int size) { iconDip_ = size; }
    void Start(HWND target, HWND apps) {
        target_ = target; appsWindow_ = apps;
        worker_ = std::thread([this] {
            const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            if (FAILED(hr)) { PostMessageW(target_, WM_CLOSE, 0, 0); return; }
            WindowTracker tracker; tracker.SetIconDip(iconDip_);
            for (;;) {
                { std::unique_lock lock(mutex_);
                  wake_.wait(lock, [this] { return stop_ || requested_; });
                  if (stop_) break;
                  requested_ = false; }
                try {
                    Performance::Scope timing(L"windows.enumerate.icons");
                    std::vector<RunningApp> snapshot;
                    if (scanner_) snapshot = scanner_();
                    else { tracker.Refresh(target_, appsWindow_); snapshot = tracker.Apps(); }
                    std::lock_guard lock(mutex_);
                    if (stop_) break;
                    result_ = std::move(snapshot);
                    if (!available_) PostMessageW(target_, Changed, 0, 0);
                    available_ = true;
                } catch (...) {
                    std::lock_guard lock(mutex_);
                    if (!stop_) PostMessageW(target_, WM_CLOSE, 0, 0);
                    break;
                }
            }
            if (SUCCEEDED(hr)) CoUninitialize();
        });
        Request();
    }
    void Request() {
        std::lock_guard lock(mutex_);
        requested_ = true; wake_.notify_one();
    }
    bool Take() {
        std::lock_guard lock(mutex_);
        if (!available_) return false;
        apps_ = std::move(result_); available_ = false; ready_ = true;
        return true;
    }
    bool Ready() const { return ready_; }
    const std::vector<RunningApp>& Apps() const { return apps_; }
    void Activate(HWND target) const { WindowTracker{}.Activate(target); }
    void Stop() {
        { std::lock_guard lock(mutex_); stop_ = true; wake_.notify_one(); }
        if (worker_.joinable()) worker_.join();
    }
private:
    std::thread worker_;
    Scanner scanner_;
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stop_ = false, requested_ = false, available_ = false, ready_ = false;
    HWND target_ = nullptr, appsWindow_ = nullptr;
    int iconDip_ = 29;
    std::vector<RunningApp> apps_, result_;
};
