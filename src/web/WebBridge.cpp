#include "WebBridge.h"
#include <windows.h>

namespace {
    std::string Utf8(const std::wstring& value) {
        if (value.empty()) return {};
        int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
            static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        if (!size) return {};
        std::string out(size, '\0');
        WideCharToMultiByte(CP_UTF8, 0, value.data(),
            static_cast<int>(value.size()), out.data(), size, nullptr, nullptr);
        return out;
    }

    std::string EscapeJson(const std::string& value) {
        std::string out = "\"";
        constexpr char digits[] = "0123456789abcdef";
        for (unsigned char c : value) {
            if (c == '"' || c == '\\') {
                out += '\\';
                out += static_cast<char>(c);
            } else if (c < 32) {
                out += "\\u00";
                out += digits[c >> 4];
                out += digits[c & 15];
            } else {
                out += static_cast<char>(c);
            }
        }
        return out + "\"";
    }

    std::string Quote(const std::wstring& value) {
        return EscapeJson(Utf8(value));
    }

    std::wstring Wide(const std::string& text) {
        if (text.empty()) return {};
        int size = MultiByteToWideChar(CP_UTF8, 0, text.data(),
            static_cast<int>(text.size()), nullptr, 0);
        std::wstring result(size, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(),
            static_cast<int>(text.size()), result.data(), size);
        return result;
    }
}

std::wstring WebBridge::AppsJson(const std::vector<RunningApp>& apps) {
    std::string json = "{\"type\":\"apps\",\"items\":[";
    for (size_t i = 0; i < apps.size(); ++i) {
        const auto& app = apps[i];
        if (i) json += ",";
        json += "{\"id\":";
        json += EscapeJson(std::to_string(
            reinterpret_cast<UINT_PTR>(app.hwnd)));
        json += ",\"title\":" + Quote(app.title);
        json += ",\"icon\":" + EscapeJson(app.icon);
        json += ",\"active\":";
        json += app.active ? "true" : "false";
        json += "}";
    }
    json += "]}";
    return Wide(json);
}