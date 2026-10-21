#include "Popups.h"

void Popups::ShowMenu(std::wstring state, std::function<void(unsigned)> action) {
    if (!initialized_ || !EnsureView()) return;
    Hide(); mode_ = Mode::Menu; window_.PopupMode(true);
    menuState_ = std::move(state); menuAction_ = std::move(action);
    GetCursorPos(&menuPoint_);
    Position(240, 254); Publish(); Present();
}
