#pragma once
#include <windows.h>
// A two-physical-pixel input strip. No global hook, polling, activation or painting.
class EdgeTrigger {
public:
    ~EdgeTrigger() { Stop(); }
    HWND Handle() const { return window_; }
    static constexpr UINT Enter = WM_APP + 73, Pointer = WM_APP + 71;
    bool Create(HWND owner) {
        owner_ = owner;
        WNDCLASSW wc{}; wc.lpfnWndProc = Procedure; wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"GlassSplitEdgeTrigger";
        if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        window_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_LAYERED,
            wc.lpszClassName, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, wc.hInstance, this);
        if (!window_) return false;
        if (!SetLayeredWindowAttributes(window_, 0, 1, LWA_ALPHA)) { Stop(); return false; }
        return true;
    }
    void Place(RECT monitor, bool enabled) {
        if (!window_) return;
        if (!enabled) { ShowWindow(window_, SW_HIDE); return; }
        SetWindowPos(window_, HWND_TOPMOST, monitor.left, monitor.bottom - 2,
            monitor.right - monitor.left, 2, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    void Stop() { if (window_) DestroyWindow(window_); window_ = nullptr; }
private:
    HWND window_ = nullptr, owner_ = nullptr;
    static LRESULT CALLBACK Procedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<EdgeTrigger*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<EdgeTrigger*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
        if (self && message == WM_MOUSEMOVE) {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0}; TrackMouseEvent(&track);
            PostMessageW(self->owner_, Enter, 0, 0);
        }
        if (self && message == WM_MOUSELEAVE) PostMessageW(self->owner_, Pointer, WM_MOUSELEAVE, 0);
        return DefWindowProcW(hwnd, message, wp, lp);
    }
};
