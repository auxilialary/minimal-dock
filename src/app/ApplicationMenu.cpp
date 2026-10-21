#include "Application.h"
#include "../shell/Autostart.h"

void Application::ShowMenu() {
    Autostart::Registry startup;
    const auto state = Autostart::Inspect(startup, Autostart::Executable());
    popups_.ShowMenu(L"{\"type\":\"menu\",\"startup\":" + std::wstring(state.enabled ? L"true" : L"false") +
        L",\"startupUnavailable\":" + (state.error ? L"true" : L"false") +
        L",\"visibility\":" + std::to_wstring(static_cast<unsigned>(visibilityMode_)) + L"}",
        [this](unsigned selected) { MenuAction(selected); });
}
void Application::MenuAction(unsigned selected) {
    Autostart::Registry startup;
    const auto path = Autostart::Executable();
    if (selected == 5) {
        if (!appOrder_.Reset()) MessageBoxW(systemWindow_.Handle(), L"Could not reset app order.", L"Dock", MB_ICONERROR);
        lastAppsJson_.clear(); Update(); return;
    }
    if (selected >= 10 && selected <= 12) {
        const auto mode = Visibility::Decode(selected - 10);
        if (Visibility::Save(mode) == ERROR_SUCCESS) {
            visibilityMode_ = mode; edgeReveal_ = false; Layout();
        } else MessageBoxW(systemWindow_.Handle(), L"Could not save visibility preference.", L"Dock", MB_ICONERROR);
    }
    PostMessageW(systemWindow_.Handle(), EdgeTrigger::Pointer, WM_MOUSELEAVE, 0);
    if (selected == 1) Command(L"sound");
    if (selected == 2) Command(L"network");
    if (selected == 4) {
        const auto current = Autostart::Inspect(startup, path);
        const auto error = current.error ? current.error : Autostart::Set(startup, path, !current.enabled);
        if (error) {
            wchar_t* detail = nullptr;
            FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0, reinterpret_cast<LPWSTR>(&detail), 0, nullptr);
            const auto text = L"Could not change Start with Windows. Error " + std::to_wstring(error) +
                L".\n" + (detail ? std::wstring(detail) : L"Registry access failed.");
            MessageBoxW(systemWindow_.Handle(), text.c_str(), L"Dock", MB_ICONERROR);
            if (detail) LocalFree(detail);
        }
    }
    if (selected == 3) PostQuitMessage(0);
}


