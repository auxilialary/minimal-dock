#pragma once
#include <windows.h>
#include <string>
#include <cstdio>
#include "Performance.h"

namespace Diagnostics {
inline bool Enabled() {
    static const bool enabled = GetEnvironmentVariableW(L"GLASSSPLIT_DIAGNOSTICS", nullptr, 0) != 0;
    return enabled;
}
inline void Log(const std::wstring& message) {
    if (!Enabled()) return;
    if (Performance::enabled) { Performance::Mark(message); return; }
    OutputDebugStringW((L"Dock: " + message + L"\n").c_str());
    wchar_t directory[MAX_PATH]{};
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", directory, MAX_PATH)) return;
    std::wstring folder = std::wstring(directory) + L"\\GlassSplit";
    CreateDirectoryW(folder.c_str(), nullptr);
    FILE* file = nullptr;
    if (_wfopen_s(&file, (folder + L"\\startup.log").c_str(), L"a, ccs=UTF-8") == 0 && file) {
        SYSTEMTIME now{};
        GetLocalTime(&now);
        fwprintf(file, L"%02u:%02u:%02u [%lu] %ls\n", now.wHour, now.wMinute,
            now.wSecond, GetCurrentProcessId(), message.c_str());
        fclose(file);
    }
}
inline bool Check(HRESULT hr, const wchar_t* operation) {
    if (SUCCEEDED(hr)) return true;
    wchar_t code[32]{};
    swprintf_s(code, L" HRESULT=0x%08X", static_cast<unsigned>(hr));
    Log(std::wstring(operation) + code);
    return false;
}
}
