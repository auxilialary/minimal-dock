#pragma once
#include "IconBitmap.h"
#include <string>
std::wstring AppIdentity(HWND window);
HICON WindowIcon(HWND window);
std::unique_ptr<Gdiplus::Bitmap> ResolveIcon(HWND window, const std::wstring& path,
    const std::wstring& identity, HICON icon, int pixels);
