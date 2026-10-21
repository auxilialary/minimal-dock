#pragma once
#include <windows.h>
#include <thread>
#include <atomic>
#include <functional>

class Watchdog {
public:
    using Initializer = std::function<bool(HWND)>;
    explicit Watchdog(Initializer initializer = {}) : initializer_(std::move(initializer)) {}
    static constexpr UINT ReadyMessage = WM_APP + 23;
    bool Start(HWND owner);
    bool Ready() const { return ready_; }
    void MarkClean();
    ~Watchdog();
private:
    bool Initialize(HWND owner);
    std::thread worker_;
    Initializer initializer_;
    std::atomic<bool> ready_ = false;
    HANDLE process_ = nullptr;
    HANDLE wait_ = nullptr;
    HANDLE cleanEvent_ = nullptr;
};
int RunWatchdog(DWORD parentPid);
