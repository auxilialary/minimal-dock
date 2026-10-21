#pragma once
#include <windows.h>
#include <vector>

class TaskbarManager {
public:
    ~TaskbarManager() { Restore(); }
    void Hide();
    void Restore();
    static void ForceShow();
private:
    std::vector<HWND> hidden_;
};