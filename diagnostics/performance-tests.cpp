#include "../src/windows/WindowInventory.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include "../src/safety/Watchdog.h"
#include "../src/shell/TaskbarManager.h"


void Check(bool condition, const char* message) {
    if (!condition) { std::printf("FAIL: %s\n", message); std::exit(1); }
}
// This fixture must never manipulate the real taskbar, including on failure.
void TaskbarManager::ForceShow() { Check(false, "unexpected real watchdog recovery"); }
int main() {
    Check(!Performance::enabled && !Performance::events, "recording allocates nothing before opt-in");
    SetEnvironmentVariableW(L"GLASSSPLIT_PERF", L"1");
    Performance::Begin();
    Check(GetEnvironmentVariableW(L"GLASSSPLIT_DIAGNOSTICS", nullptr, 0) != 0, "benchmark implies safe diagnostics");
    const DWORD uiThread = GetCurrentThreadId();
    std::mutex mutex;
    std::condition_variable wake;
    bool release = false;
    std::atomic<int> scans = 0;
    std::atomic<DWORD> scanThread = 0;
    HWND target = CreateWindowExW(0, L"STATIC", L"Inventory test", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
    Check(target != nullptr, "message-only fixture created");
    WindowInventory inventory([&] {
        scanThread = GetCurrentThreadId();
        const int number = ++scans;
        if (number == 1) {
            std::unique_lock lock(mutex);
            wake.notify_one();
            Check(wake.wait_for(lock, std::chrono::seconds(5), [&] { return release; }), "bounded fixture wait");
        }
        RunningApp app; app.title = std::to_wstring(number);
        return std::vector<RunningApp>{app};
    });
    inventory.Start(target, target);
    { std::unique_lock lock(mutex);
      Check(wake.wait_for(lock, std::chrono::seconds(5), [&] { return scans.load() == 1; }), "worker begins"); }
    Check(!inventory.Ready() && !inventory.Take(), "unfinished enumeration is not an empty-desktop result");
    for (int i = 0; i < 100; ++i) inventory.Request();
    Check(scans == 1 && scanThread != uiThread, "slow scan does not block caller or spawn extra workers");
    { std::lock_guard lock(mutex); release = true; wake.notify_one(); }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (scans < 2 && std::chrono::steady_clock::now() < deadline) Sleep(1);
    Check(scans == 2, "request burst coalesces to one follow-up");
    while (!inventory.Take() && std::chrono::steady_clock::now() < deadline) Sleep(1);
    inventory.Stop();
    inventory.Take();
    Check(inventory.Ready() && inventory.Apps().size() == 1 && inventory.Apps()[0].title == L"2", "latest snapshot committed on UI thread");
    MSG message{};
    int changes = 0;
    while (PeekMessageW(&message, target, WindowInventory::Changed, WindowInventory::Changed, PM_REMOVE)) ++changes;
    Check(changes >= 1 && changes <= 2, "completion messages are coalesced");
    release = false;
    std::atomic<bool> handshake = false;
    Watchdog watchdog([&](HWND) {
        std::unique_lock lock(mutex); handshake = true; wake.notify_one();
        Check(wake.wait_for(lock, std::chrono::seconds(5), [&] { return release; }), "bounded fake watchdog wait");
        return true;
    });
    Check(watchdog.Start(target), "watchdog handshake dispatched");
    { std::unique_lock lock(mutex);
      Check(wake.wait_for(lock, std::chrono::seconds(5), [&] { return handshake.load(); }), "watchdog worker begins"); }
    Check(!watchdog.Ready(), "pending watchdog cannot pass taskbar readiness");
    { std::lock_guard lock(mutex); release = true; wake.notify_one(); }
    watchdog.MarkClean();
    Check(watchdog.Ready(), "watchdog readiness arrives after handshake");
    Check(PeekMessageW(&message, target, Watchdog::ReadyMessage, Watchdog::ReadyMessage, PM_REMOVE) && message.wParam,
        "watchdog posts success to owner thread");
    Watchdog failedWatchdog([](HWND) { return false; });
    Check(failedWatchdog.Start(target), "failed handshake dispatched");
    failedWatchdog.MarkClean();
    Check(!failedWatchdog.Ready(), "failed handshake never passes readiness");
    Check(PeekMessageW(&message, target, Watchdog::ReadyMessage, Watchdog::ReadyMessage, PM_REMOVE) && !message.wParam,
        "watchdog posts failure to owner thread");
    DestroyWindow(target);
    for (size_t i = 0; i < Performance::capacity + 10; ++i) Performance::Mark(L"bounded.fixture");
    Check(Performance::count == Performance::capacity && Performance::dropped >= 10, "diagnostic buffer is bounded");
    std::puts("PASS: safe opt-in, allocation gate, worker affinity, blocked scan responsiveness, readiness, coalescing, snapshot handoff, cancellation/join, asynchronous watchdog gate, bounded trace");
}
