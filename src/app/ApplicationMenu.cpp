#include "Application.h"
#include "../shell/Autostart.h"

void Application::ShowMenu() {
    POINT pt{};
    GetCursorPos(&pt);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"Настройки звука");
    AppendMenuW(menu, MF_STRING, 2, L"Настройки сети");
    Autostart::Registry startup;
    const auto path = Autostart::Executable();
    const auto state = Autostart::Inspect(startup, path);
    const wchar_t* label = state.error ? L"Start with Windows (state unavailable)" :
        state.outdated ? L"Start with Windows (update executable path)" : L"Start with Windows";
    AppendMenuW(menu, MF_STRING | (state.enabled ? MF_CHECKED : MF_UNCHECKED), 4, label);
    HMENU visibility = CreatePopupMenu();
    AppendMenuW(visibility, MF_STRING, 10, L"Always Visible");
    AppendMenuW(visibility, MF_STRING, 11, L"Auto Hide");
    AppendMenuW(visibility, MF_STRING, 12, L"Smart Hide");
    CheckMenuRadioItem(visibility, 10, 12, 10 + static_cast<UINT>(visibilityMode_), MF_BYCOMMAND);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(visibility), L"Visibility");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 3, L"Закрыть Dock");
    menuOpen_ = true;
    UINT selected = TrackPopupMenu(menu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON,
        pt.x, pt.y, 0, systemWindow_.Handle(), nullptr);
    menuOpen_ = false;
    DestroyMenu(menu);
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


