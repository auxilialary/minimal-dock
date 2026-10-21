#pragma once
#include <windows.h>
inline bool DismissPopup(UINT mouseMessage, RECT bounds, POINT point) {
    return mouseMessage == WM_LBUTTONDOWN && !PtInRect(&bounds, point);
}
