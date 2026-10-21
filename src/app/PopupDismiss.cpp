#include "Popups.h"
#include "../windows/PopupInteraction.h"
Popups* Popups::hookOwner_ = nullptr;
void Popups::BeginDismiss() {
    if (mouseHook_) return;
    hookOwner_ = this;
    mouseHook_ = SetWindowsHookExW(WH_MOUSE_LL, OutsideClick, GetModuleHandleW(nullptr), 0);
}
void Popups::EndDismiss() {
    if (mouseHook_) { UnhookWindowsHookEx(mouseHook_); mouseHook_ = nullptr; }
    if (hookOwner_ == this) hookOwner_ = nullptr;
}
LRESULT CALLBACK Popups::OutsideClick(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && wp == WM_LBUTTONDOWN && hookOwner_) {
        auto* self = hookOwner_;
        const auto point = reinterpret_cast<const MSLLHOOKSTRUCT*>(lp)->pt;
        RECT bounds{}, anchor{};
        GetWindowRect(self->window_.Handle(), &bounds);
        const bool toggle = self->mode_ == Mode::Lyrics && IsWindowVisible(self->mediaAnchor_) &&
            GetWindowRect(self->mediaAnchor_, &anchor) && PtInRect(&anchor, point);
        if (!toggle && self->Interactive() && DismissPopup(WM_LBUTTONDOWN, bounds, point))
            PostMessageW(self->window_.Handle(), OutsideMessage, self->revision_, 0);
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}
