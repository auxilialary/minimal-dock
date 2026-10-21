#pragma once
#include <windows.h>
#include <windowsx.h>

// WebView2 expects physical client pixels, not DIP; wheel LPARAM is screen pixels.
namespace MouseInput {
inline bool IsWheel(UINT message) { return message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL; }
inline POINT Position(HWND owner, UINT message, LPARAM lp) {
    POINT point{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
    if (IsWheel(message)) ScreenToClient(owner, &point);
    return message == WM_MOUSELEAVE ? POINT{} : point;
}
inline UINT32 Data(UINT message, WPARAM wp) {
    if (IsWheel(message)) return static_cast<UINT32>(static_cast<INT32>(GET_WHEEL_DELTA_WPARAM(wp)));
    if (message == WM_XBUTTONDOWN || message == WM_XBUTTONUP || message == WM_XBUTTONDBLCLK)
        return GET_XBUTTON_WPARAM(wp);
    return 0;
}
}
