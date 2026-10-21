#pragma once
#include <string>
#include <vector>
#include "../windows/WindowTracker.h"

namespace WebBridge {
    std::wstring AppsJson(const std::vector<RunningApp>& apps);
}