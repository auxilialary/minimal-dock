#pragma once
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <memory>
// Produces straight-alpha ARGB for the PNG encoder, without resizing.
std::unique_ptr<Gdiplus::Bitmap> DecodeIcon(HICON icon);

inline int IconPhysicalSize(int dip, UINT dpi) { return MulDiv(dip, static_cast<int>(dpi), 96); }
std::unique_ptr<Gdiplus::Bitmap> ResizeIcon(Gdiplus::Bitmap& source, int pixels);

// Smallest shell image-list bucket at or above the physical target.
inline int IconSourceSize(int pixels) { return pixels <= 32 ? 32 : pixels <= 48 ? 48 : 256; }

std::unique_ptr<Gdiplus::Bitmap> LoadShellIcon(const wchar_t* path, int pixels);
