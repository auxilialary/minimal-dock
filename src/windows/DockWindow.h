#pragma once
#include <windows.h>
#include <functional>
#include "WindowMotion.h"
#include "../shell/WindowEffects.h"

class WebViewHost;

class DockWindow {
public:
    bool Create(HINSTANCE instance, const wchar_t* name,
        std::function<void(UINT, WPARAM, LPARAM)> handler, int radiusDip, float borderOpacity);
    void Attach(WebViewHost* view);
    void Place(RECT rect, bool animate, ULONGLONG start = 0, DWORD duration = WindowMotion::DefaultDuration);
    void Show(bool visible);
    void Destroy();
    IUnknown* WebRoot() const { return effects_.WebRoot(); }
    HWND Handle() const { return hwnd_; }
    void PopupMode(bool interactive);
    void Fade(bool visible, int milliseconds) { effects_.Fade(visible, milliseconds); }
private:
    static LRESULT CALLBACK Procedure(HWND, UINT, WPARAM, LPARAM);
    HWND hwnd_ = nullptr;
    bool popup_ = false, interactive_ = true;
    WebViewHost* view_ = nullptr;
    WindowMotion motion_;
    WindowEffects effects_;
    std::function<void(UINT, WPARAM, LPARAM)> handler_;
};
