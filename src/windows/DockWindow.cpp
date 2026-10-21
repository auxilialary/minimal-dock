#include "DockWindow.h"
#include "../web/WebViewHost.h"
#include "../web/Diagnostics.h"


bool DockWindow::Create(HINSTANCE instance, const wchar_t* name,
    std::function<void(UINT, WPARAM, LPARAM)> handler, int radiusDip, float borderOpacity) {
    handler_ = std::move(handler);

    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.hInstance = instance;
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = Procedure;
        wc.lpszClassName = L"GlassSplitDockWindow";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        if (!RegisterClassW(&wc)) return false;
        registered = true;
    }

    hwnd_ = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP,
        L"GlassSplitDockWindow", name, WS_POPUP | WS_CLIPCHILDREN,
        0, 0, 1, 1, nullptr, nullptr, instance, this);

    if (!hwnd_) return false;
    if (!effects_.Initialize(hwnd_, radiusDip, borderOpacity)) {
        Destroy();
        MessageBoxW(nullptr, L"Colorless host backdrop could not be initialized. Windows 11 is required. Dock will exit without changing the taskbar.", L"Dock", MB_ICONERROR);
        return false;
    }
    return true;
}

void DockWindow::Attach(WebViewHost* view) {
    view_ = view;
    if (view_) view_->Resize();
}

void DockWindow::PopupMode(bool interactive) {
    popup_ = true; interactive_ = interactive;
    auto style = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    style = interactive ? style & ~(WS_EX_NOACTIVATE | WS_EX_TRANSPARENT) :
        style | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT;
    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, style);
}

void DockWindow::Place(RECT rect, bool animate, ULONGLONG start, DWORD duration) {
    motion_.Move(hwnd_, rect, animate ? duration : 0, start);
}

void DockWindow::Show(bool visible) {
    if (!hwnd_) return;
    if (view_) view_->Displayed(visible);
    if (visible && !IsWindowVisible(hwnd_)) {
        Diagnostics::Log(L"native.show hwnd=" + std::to_wstring(reinterpret_cast<UINT_PTR>(hwnd_)));
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    } else if (!visible && IsWindowVisible(hwnd_)) {
        motion_.Stop(hwnd_);
        Diagnostics::Log(L"native.hide hwnd=" + std::to_wstring(reinterpret_cast<UINT_PTR>(hwnd_)));
        ShowWindow(hwnd_, SW_HIDE);
    }
}

void DockWindow::Destroy() {
    if (!hwnd_) return;
    motion_.Stop(hwnd_);
    effects_.Close();
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
}

LRESULT CALLBACK DockWindow::Procedure(HWND hwnd, UINT msg,
    WPARAM wp, LPARAM lp) {
    DockWindow* self = reinterpret_cast<DockWindow*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<DockWindow*>(cs->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(self));
        return TRUE;
    }
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
    if (msg == WM_GETOBJECT && self->view_) {
        if (LRESULT result = self->view_->Accessibility(wp, lp)) return result;
    }

    if (self->popup_ && !self->interactive_ && msg == WM_NCHITTEST) return HTTRANSPARENT;
    if (self->popup_ && msg == WM_MOUSEACTIVATE) return self->interactive_ ? MA_ACTIVATE : MA_NOACTIVATE;
    if (self->handler_ && msg == WM_CAPTURECHANGED) self->handler_(WM_MOUSELEAVE, 0, 0);
    if (self->handler_ && (msg == WM_MOUSEMOVE || msg == WM_MOUSELEAVE)) self->handler_(msg, wp, lp);
    if (self->view_ && self->interactive_ && self->view_->Input(msg, wp, lp)) return TRUE;

    if (msg == WM_NCDESTROY) {
        self->effects_.Close();
        self->motion_.Stop(hwnd);
        self->hwnd_ = nullptr;
        self->view_ = nullptr;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    if (msg == WM_DESTROY && !self->popup_) PostQuitMessage(0);
    if (msg == WM_QUERYENDSESSION) return TRUE;
    if (msg == WM_ENDSESSION && wp) PostQuitMessage(0);
    if (msg == WM_DWMCOMPOSITIONCHANGED || msg == WM_THEMECHANGED || msg == WM_SETTINGCHANGE)
        self->effects_.Refresh(hwnd);
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_PAINT) {
        PAINTSTRUCT paint{};
        BeginPaint(hwnd, &paint);
        // All visible pixels belong to Composition and the transparent WebView.
        EndPaint(hwnd, &paint);
        return 0;
    }
    if (msg == WM_TIMER && wp == WindowMotion::TimerId()) {
        self->motion_.Tick(hwnd);
        return 0;
    }
    if (msg == WM_WINDOWPOSCHANGED && GetEnvironmentVariableW(L"GLASSSPLIT_DIAGNOSTICS", nullptr, 0)) {
        RECT r{}; GetWindowRect(hwnd, &r);
        Diagnostics::Log(L"HWND=" + std::to_wstring(reinterpret_cast<UINT_PTR>(hwnd)) +
            L" actual=" + std::to_wstring(r.left) + L"," + std::to_wstring(r.top) +
            L"," + std::to_wstring(r.right) + L"," + std::to_wstring(r.bottom));
    }
    if (msg == WM_MOVE && self->view_) self->view_->Moved();
    if (msg == WM_SIZE || msg == WM_DPICHANGED) self->effects_.Resize(hwnd);
    if (msg == WM_SIZE && self->view_) self->view_->Resize();
    if (msg == WM_CLOSE) {
        if (self->popup_) { if (self->handler_) self->handler_(msg, wp, lp); }
        else PostQuitMessage(0);
        return 0;
    }
    if (self->handler_) self->handler_(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}
