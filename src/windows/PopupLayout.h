#pragma once
#include <windows.h>
#include <algorithm>
inline RECT CalculatePopup(RECT work, RECT anchor, int center, int width, int height, int gap) {
    width = std::clamp(width, 1, static_cast<int>(std::max(1L, work.right - work.left)));
    height = std::clamp(height, 1, static_cast<int>(std::max(1L, work.bottom - work.top)));
    const LONG left = std::clamp<LONG>(center - width / 2, work.left, work.right - width);
    LONG top = anchor.top - gap - height;
    if (top < work.top) top = anchor.bottom + gap;
    top = std::clamp<LONG>(top, work.top, work.bottom - height);
    return {left, top, left + width, top + height};
}
