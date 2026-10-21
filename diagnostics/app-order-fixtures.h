#pragma once
#include "../src/windows/AppOrder.h"
#pragma comment(lib, "advapi32.lib")
void AppOrderFixtures() {
    const auto key = L"Software\\GlassSplitTestAppOrder-" + std::to_wstring(GetCurrentProcessId());
    AppOrder order(key.c_str());
    std::vector<RunningApp> apps(3);
    for (size_t i = 0; i < apps.size(); ++i) {
        apps[i].hwnd = reinterpret_cast<HWND>(i + 1); apps[i].path = L"C:\\App" + std::to_wstring(i) + L".exe";
    }
    apps[1].identity = L"Packaged.App!App";
    Check(order.Set(L"3 1 2", apps), "save custom order to isolated test key");
    AppOrder loaded(key.c_str()); loaded.Load();
    auto sorted = loaded.Sort(apps);
    Check(sorted[0].hwnd == apps[2].hwnd && sorted[1].hwnd == apps[0].hwnd, "registry order round trip");
    apps[2].hwnd = reinterpret_cast<HWND>(99);
    Check(loaded.Sort(apps)[0].hwnd == apps[2].hwnd, "reopened HWND retains stable identity order");
    auto closed = apps; closed.erase(closed.begin());
    Check(loaded.Sort(closed)[0].path == apps[2].path, "closed application preserves remaining order");
    RunningApp fresh; fresh.path = L"new.exe"; fresh.hwnd = reinterpret_cast<HWND>(4); apps.push_back(fresh);
    Check(loaded.Sort(apps).back().path == fresh.path, "new app appends");
    Check(!loaded.Set(L"1 1 1 1", apps), "duplicate reorder rejected");
    Check(!loaded.Set(L"1 2 3 4", apps), "stale HWND rejected");
    Check(!loaded.Set(L"1 2", apps), "incomplete reorder rejected");
    Check(loaded.Reset(), "reset persisted order");
    AppOrder reset(key.c_str()); reset.Load();
    Check(reset.Sort(apps)[0].path == apps[0].path, "reset restores inventory order");
    Check(RegDeleteKeyW(HKEY_CURRENT_USER, key.c_str()) == ERROR_SUCCESS, "remove isolated test key");
}
