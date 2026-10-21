#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <deque>

struct RunningApp {
    HWND hwnd = nullptr;
    DWORD pid = 0;
    std::wstring path;
    std::wstring title;
    std::wstring identity;
    std::string icon;
    bool active = false;
};

class WindowTracker {
public:
    void SetIconDip(int size) { iconDip_ = size; }
    void Refresh(HWND systemWindow, HWND appsWindow);
    const std::vector<RunningApp>& Apps() const { return apps_; }
    void Activate(HWND target) const;
    std::string IconFor(const RunningApp& app);
private:
    int iconDip_ = 29, iconPixels_ = 29;
    std::vector<RunningApp> apps_;
    std::vector<std::wstring> order_;
    std::unordered_map<std::wstring, std::string> icons_;
    std::deque<std::wstring> iconOrder_;
    size_t iconBytes_ = 0;
};
