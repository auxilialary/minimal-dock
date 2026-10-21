#pragma once
#include "WindowTracker.h"
#include <algorithm>
#include <cwctype>
#include <sstream>

// UI-thread owned. Application IDs are AUMIDs or normalized executable paths.
class AppOrder {
public:
    explicit AppOrder(const wchar_t* registryPath = L"Software\\GlassSplit") : registryPath_(registryPath) {}
    static std::wstring Key(const RunningApp& app) {
        auto key = app.identity.empty() ? app.path : app.identity;
        std::transform(key.begin(), key.end(), key.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        return key;
    }
    void Load() {
        DWORD bytes = 0;
        if (RegGetValueW(HKEY_CURRENT_USER, registryPath_.c_str(), L"AppOrder", RRF_RT_REG_MULTI_SZ, nullptr, nullptr, &bytes) || bytes > 65536 || bytes < 4) return;
        std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 2, 0);
        if (RegGetValueW(HKEY_CURRENT_USER, registryPath_.c_str(), L"AppOrder", RRF_RT_REG_MULTI_SZ, nullptr, buffer.data(), &bytes)) return;
        for (size_t i = 0; i < buffer.size() && buffer[i] && keys_.size() < 256;) {
            std::wstring key(buffer.data() + i); i += key.size() + 1;
            if (key.size() <= 2048 && std::find(keys_.begin(), keys_.end(), key) == keys_.end()) keys_.push_back(std::move(key));
        }
    }
    std::vector<RunningApp> Sort(const std::vector<RunningApp>& apps) const {
        auto sorted = apps;
        std::stable_sort(sorted.begin(), sorted.end(), [&](const auto& a, const auto& b) {
            return std::find(keys_.begin(), keys_.end(), Key(a)) < std::find(keys_.begin(), keys_.end(), Key(b));
        });
        return sorted;
    }
    bool Set(const std::wstring& ids, const std::vector<RunningApp>& apps) {
        std::wistringstream input(ids); UINT_PTR id = 0; std::vector<std::wstring> keys;
        while (input >> id) {
            const auto found = std::find_if(apps.begin(), apps.end(), [id](const auto& app) { return reinterpret_cast<UINT_PTR>(app.hwnd) == id; });
            if (found == apps.end()) return false;
            auto key = Key(*found);
            if (key.empty() || key.size() > 2048 || std::find(keys.begin(), keys.end(), key) != keys.end()) return false;
            keys.push_back(std::move(key));
        }
        if (!input.eof() || keys.size() != apps.size() || keys.size() > 256) return false;
        for (const auto& key : keys_) if (keys.size() < 256 && std::find(keys.begin(), keys.end(), key) == keys.end()) keys.push_back(key);
        std::vector<wchar_t> buffer;
        for (const auto& key : keys) { buffer.insert(buffer.end(), key.begin(), key.end()); buffer.push_back(0); }
        buffer.push_back(0); if (buffer.size() == 1) buffer.push_back(0);
        if (buffer.size() * sizeof(wchar_t) > 65536) return false;
        HKEY registry = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, registryPath_.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &registry, nullptr)) return false;
        const auto error = RegSetValueExW(registry, L"AppOrder", 0, REG_MULTI_SZ, reinterpret_cast<const BYTE*>(buffer.data()), static_cast<DWORD>(buffer.size() * sizeof(wchar_t)));
        RegCloseKey(registry); if (error) return false;
        keys_ = std::move(keys); return true;
    }
    bool Reset() {
        const auto error = RegDeleteKeyValueW(HKEY_CURRENT_USER, registryPath_.c_str(), L"AppOrder");
        if (error != ERROR_SUCCESS && error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) return false;
        keys_.clear(); return true;
    }
private:
    std::wstring registryPath_;
    std::vector<std::wstring> keys_;
};
