#pragma once
#include <windows.h>
#include <algorithm>

struct IslandLayout { RECT system{}, apps{}, media{}; bool stacked = false; };
// Physical pixels on the selected monitor. Overflowing applications scroll in CSS.
inline IslandLayout CalculateIslands(RECT b, int height, int systemWidth,
    int margin, int gap, int desired, int minimum, bool running) {
    const int available = std::max(1, int(b.right - b.left) - 2 * margin);
    const int sw = std::min(systemWidth, available);
    const int y = b.bottom - margin - height;
    IslandLayout result;
    // Reserve equal space on both sides so the applications HWND owns the center.
    const int capacity = available - 2 * (sw + gap);
    result.stacked = running && capacity < minimum;
    int width = running ? std::clamp(desired, 1,
        std::max(1, result.stacked ? available : capacity)) : 0;
    // Integral HWND edges: match monitor parity for an exact physical center.
    if (width > 1 && (width % 2 != (b.right - b.left) % 2)) --width;
    const int x = b.left + (b.right - b.left - sw) / 2;
    const int ax = b.left + (b.right - b.left - width) / 2;
    const int sx = running && !result.stacked ? ax + width + gap : x;
    result.system = { sx, y, sx + sw, y + height };
    if (!running) return result;
    result.apps = { ax, y, ax + width, y + height };
    // On narrow screens retain usable controls on two independently centered rows.
    if (result.stacked) OffsetRect(&result.system, 0, -height - gap);
    return result;
}

struct IslandContour { float radius, strokeInset, strokeRadius; };
inline IslandContour CalculateContour(int radiusDip, UINT dpi, int width, int height) {
    const float radius = static_cast<float>(std::clamp(MulDiv(radiusDip, static_cast<int>(dpi), 96),
        0, std::max(0, std::min(width, height) / 2)));
    return { radius, 0.5f, std::max(0.0f, radius - 0.5f) };
}

// Media uses the available left flank without ever changing the apps anchor/width.
inline IslandLayout CalculateMediaIslands(RECT b, int height, int systemWidth,
    int margin, int gap, int desired, int minimum, bool running, int mediaWidth) {
    auto result = CalculateIslands(b, height, systemWidth, margin, gap, desired, minimum, running);
    if (mediaWidth <= 0) return result;
    const int available = std::max(1, int(b.right - b.left) - 2 * margin);
    const int width = std::min(mediaWidth, available);
    const RECT anchor = running ? result.apps : result.system;
    int x = anchor.left - gap - width;
    int y = anchor.top;
    if (x < b.left + margin) {
        x = b.left + (b.right - b.left - width) / 2;
        y = std::min(result.system.top, anchor.top) - gap - height;
        result.stacked = true;
    }
    result.media = {x, y, x + width, y + height};
    return result;
}
