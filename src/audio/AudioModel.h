#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include <windows.h>
namespace Audio {
inline float Scalar(int percent) { return std::clamp(percent, 0, 100) / 100.0f; }
inline int Percent(float scalar) { return static_cast<int>(std::lround(std::clamp(scalar, 0.0f, 1.0f) * 100)); }
struct Row {
    unsigned id = 0;
    DWORD pid = 0;
    std::wstring name, path, identity;
    int volume = 0;
    bool muted = false;
    std::string icon;
    bool operator==(const Row&) const = default;
};
struct Snapshot { std::vector<Row> rows; std::wstring error; };
}
