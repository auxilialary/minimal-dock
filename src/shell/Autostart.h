#pragma once
#include <windows.h>
#include <string>
#include <vector>
#pragma comment(lib, "advapi32.lib")

namespace Autostart {
inline constexpr wchar_t Key[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
inline constexpr wchar_t Name[] = L"GlassSplitDock";
struct Store {
    virtual ~Store() = default;
    virtual LSTATUS Read(std::wstring& value) = 0;
    virtual LSTATUS Write(const std::wstring& value) = 0;
    virtual LSTATUS Remove() = 0;
};
struct Registry final : Store {
    LSTATUS Read(std::wstring& value) override {
        DWORD bytes = 0;
        auto status = RegGetValueW(HKEY_CURRENT_USER, Key, Name, RRF_RT_REG_SZ, nullptr, nullptr, &bytes);
        if (status != ERROR_SUCCESS) return status;
        std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 1);
        status = RegGetValueW(HKEY_CURRENT_USER, Key, Name, RRF_RT_REG_SZ, nullptr, buffer.data(), &bytes);
        if (status == ERROR_SUCCESS) value = buffer.data();
        return status;
    }
    LSTATUS Write(const std::wstring& value) override {
        HKEY key = nullptr;
        auto status = RegCreateKeyExW(HKEY_CURRENT_USER, Key, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
        if (status != ERROR_SUCCESS) return status;
        status = RegSetValueExW(key, Name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
            static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(key); return status;
    }
    LSTATUS Remove() override {
        auto status = RegDeleteKeyValueW(HKEY_CURRENT_USER, Key, Name);
        return status == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : status;
    }
};
inline std::wstring Executable() {
    std::vector<wchar_t> buffer(32768);
    DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    return size && size < buffer.size() ? std::wstring(buffer.data(), size) : L"";
}
inline std::wstring Command(const std::wstring& path) { return L"\"" + path + L"\""; }
struct State { bool enabled = false, outdated = false; LSTATUS error = ERROR_SUCCESS; };
inline State Inspect(Store& store, const std::wstring& path) {
    std::wstring value;
    auto error = store.Read(value);
    if (error == ERROR_FILE_NOT_FOUND) return {};
    if (error != ERROR_SUCCESS) return {false, false, error};
    return {value == Command(path), value != Command(path), ERROR_SUCCESS};
}
inline LSTATUS Set(Store& store, const std::wstring& path, bool enabled) {
    if (path.empty()) return ERROR_BAD_PATHNAME;
    auto error = enabled ? store.Write(Command(path)) : store.Remove();
    if (error != ERROR_SUCCESS) return error;
    auto state = Inspect(store, path);
    if (state.error) return state.error;
    return state.enabled == enabled && !state.outdated ? ERROR_SUCCESS : ERROR_WRITE_FAULT;
}
}
