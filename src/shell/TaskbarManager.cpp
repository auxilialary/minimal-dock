#include "TaskbarManager.h"
#include <algorithm>

namespace {
    void VisitTaskbars(void (*action)(HWND, void*), void* context) {
        struct State {
            void (*action)(HWND, void*);
            void* context;
        } state{ action, context };

        EnumWindows([](HWND hwnd, LPARAM param) -> BOOL {
            auto* state = reinterpret_cast<State*>(param);
            wchar_t name[100]{};
            GetClassNameW(hwnd, name, 100);
            if (wcscmp(name, L"Shell_TrayWnd") == 0 ||
                wcscmp(name, L"Shell_SecondaryTrayWnd") == 0) {
                state->action(hwnd, state->context);
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&state));
    }
}

void TaskbarManager::Hide() {
    VisitTaskbars([](HWND hwnd, void* ptr) {
        auto* list = static_cast<std::vector<HWND>*>(ptr);
        if (IsWindowVisible(hwnd)) {
            if (std::find(list->begin(), list->end(), hwnd) == list->end())
                list->push_back(hwnd);
            ShowWindow(hwnd, SW_HIDE);
        }
    }, &hidden_);
}

void TaskbarManager::Restore() {
    for (HWND hwnd : hidden_) {
        if (IsWindow(hwnd)) ShowWindow(hwnd, SW_SHOW);
    }
    hidden_.clear();
}

void TaskbarManager::ForceShow() {
    VisitTaskbars([](HWND hwnd, void*) {
        ShowWindow(hwnd, SW_SHOW);
    }, nullptr);
}