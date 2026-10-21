#include <windows.h>
#include <shellapi.h>
#include <roapi.h>
#include <string>
#include "app/Application.h"
#include "safety/Watchdog.h"
#include "web/Performance.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    Performance::Begin();
    // Isolation probes must keep the taskbar visible just like diagnostics mode.
    if (GetEnvironmentVariableW(L"GLASSSPLIT_BACKDROP_SOURCE_ONLY", nullptr, 0) ||
        GetEnvironmentVariableW(L"GLASSSPLIT_BACKDROP_ONLY", nullptr, 0))
        SetEnvironmentVariableW(L"GLASSSPLIT_DIAGNOSTICS", L"1");
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc == 3 && wcscmp(argv[1], L"--watchdog") == 0) {
        DWORD pid = wcstoul(argv[2], nullptr, 10);
        LocalFree(argv);
        return RunWatchdog(pid);
    }
    if (argv) LocalFree(argv);

    HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\GlassSplitDockInstance");
    if (!single || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (single) CloseHandle(single);
        return 0;
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HRESULT hr = RoInitialize(RO_INIT_SINGLETHREADED);
    if (FAILED(hr)) {
        CloseHandle(single);
        return 1;
    }

    int result = 1;
    Performance::Mark(L"native.initialized");
    try {
        Application app;
        result = app.Run(instance);
    } catch (...) {
        TaskbarManager::ForceShow();
    }
    RoUninitialize();
    CloseHandle(single);
    Performance::Mark(L"process.exit");
    Performance::Flush();
    return result;
}
