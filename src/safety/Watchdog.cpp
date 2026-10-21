#include "Watchdog.h"
#include "../shell/TaskbarManager.h"
#include <string>
#include <vector>
#include "../web/Performance.h"

namespace {
    std::wstring EventName(DWORD pid) {
        return L"Local\\GlassSplitCleanExit_" + std::to_wstring(pid);
    }
}

bool Watchdog::Start(HWND owner) {
    if (worker_.joinable()) return false;
    try {
        worker_ = std::thread([this, owner] {
            Performance::Scope timing(L"watchdog.handshake");
            bool result = false;
            try { result = initializer_ ? initializer_(owner) : Initialize(owner); }
            catch (...) { if (cleanEvent_) SetEvent(cleanEvent_); }
            ready_ = result;
            Performance::Mark(result ? L"watchdog.ready" : L"watchdog.failed");
            PostMessageW(owner, ReadyMessage, result, 0);
        });
    } catch (...) { return false; }
    return true;
}
bool Watchdog::Initialize(HWND owner) {
    if (cleanEvent_) return false;
    DWORD pid = GetCurrentProcessId();
    cleanEvent_ = CreateEventW(nullptr, TRUE, FALSE, EventName(pid).c_str());
    if (!cleanEvent_) return false;

    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, (EventName(pid) + L"_ready").c_str());
    if (!ready) return false;
    wchar_t exe[MAX_PATH]{};
    DWORD length = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if (!length || length == MAX_PATH) { CloseHandle(ready); return false; }

    std::wstring command = L"\"" + std::wstring(exe) +
        L"\" --watchdog " + std::to_wstring(pid);
    std::vector<wchar_t> args(command.begin(), command.end());
    args.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION proc{};
    BOOL result = CreateProcessW(exe, args.data(),
        nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
        nullptr, nullptr, &startup, &proc);

    if (!result) { CloseHandle(ready); return false; }
    CloseHandle(proc.hThread);
    HANDLE handles[]{ ready, proc.hProcess };
    DWORD status = WaitForMultipleObjects(2, handles, FALSE, 5000);
    CloseHandle(ready);
    if (status != WAIT_OBJECT_0) {
        CloseHandle(proc.hProcess);
        SetEvent(cleanEvent_);
        return false;
    }
    process_ = proc.hProcess;
    if (!RegisterWaitForSingleObject(&wait_, process_,
        [](void* context, BOOLEAN) {
            PostMessageW(static_cast<HWND>(context), WM_CLOSE, 0, 0);
        }, owner, INFINITE, WT_EXECUTEONLYONCE)) {
        SetEvent(cleanEvent_);
        return false;
    }
    return true;
}

void Watchdog::MarkClean() {
    if (worker_.joinable()) worker_.join();
    if (cleanEvent_) SetEvent(cleanEvent_);
    if (wait_) { UnregisterWaitEx(wait_, INVALID_HANDLE_VALUE); wait_ = nullptr; }
}

Watchdog::~Watchdog() {
    MarkClean();
    if (wait_) UnregisterWaitEx(wait_, INVALID_HANDLE_VALUE);
    if (process_) CloseHandle(process_);
    if (cleanEvent_) CloseHandle(cleanEvent_);
}

int RunWatchdog(DWORD parentPid) {
    if (!parentPid) return 1;
    HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, parentPid);
    HANDLE clean = OpenEventW(SYNCHRONIZE, FALSE,
        EventName(parentPid).c_str());
    if (!parent) {
        if (clean) CloseHandle(clean);
        return 1;
    }

    HANDLE ready = OpenEventW(EVENT_MODIFY_STATE, FALSE,
        (EventName(parentPid) + L"_ready").c_str());
    if (!clean || !ready) {
        if (ready) CloseHandle(ready);
        if (clean) CloseHandle(clean);
        CloseHandle(parent);
        return 1;
    }
    SetEvent(ready);
    CloseHandle(ready);
    DWORD result;
    if (clean) {
        HANDLE handles[]{ clean, parent };
        result = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
    } else {
        WaitForSingleObject(parent, INFINITE);
        result = WAIT_OBJECT_0 + 1;
    }

    // A crash means the clean-exit event was not signalled.
    if (!clean || result != WAIT_OBJECT_0)
        TaskbarManager::ForceShow();

    if (clean) CloseHandle(clean);
    CloseHandle(parent);
    return 0;
}
