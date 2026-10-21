#include "Popups.h"
#include <shellapi.h>
#include <winrt/base.h>
#include "../windows/PopupLayout.h"
#include "../windows/PopupInteraction.h"
#include <sstream>
#include <cmath>
#include <shellscalingapi.h>
#include "../web/Performance.h"
namespace {
constexpr UINT_PTR ExitTimer = 0x501;
std::wstring Quote(const std::wstring& input) {
    std::wstring out = L"\"";
    const wchar_t digits[] = L"0123456789abcdef";
    for (wchar_t c : input) {
        if (c == L'"' || c == L'\\') { out += L'\\'; out += c; }
        else if (c < 32) { out += L"\\u00"; out += digits[c >> 4]; out += digits[c & 15]; }
        else out += c;
    }
    return out + L"\"";
}
}
bool Popups::Initialize(ICoreWebView2Environment* environment, const std::wstring& directory,
    HWND owner, HWND apps, int radius, float border) {
    owner_ = owner; apps_ = apps;
    environment_ = environment; directory_ = directory; radius_ = radius; border_ = border;
    initialized_ = environment != nullptr;
    return initialized_;
}
bool Popups::EnsureView() {
    if (window_.Handle()) return true;
    Performance::Mark(L"popup.first.request");
    if (!window_.Create(GetModuleHandleW(nullptr), L"Dock Popup",
        [this](UINT m, WPARAM w, LPARAM l) { Message(m, w, l); }, radius_, border_)) return false;
    window_.PopupMode(false);
    window_.Place({0, 0, 320, 150}, false);
    window_.Attach(&view_);
    initialized_ = view_.Create(environment_.Get(), window_.Handle(), window_.WebRoot(), directory_,
        L"popup", [this] { if (mode_ != Mode::Hidden) { Publish(); Present(); } },
        [this](const std::wstring& command) { Command(command, running_); });
    return initialized_;
}
void Popups::Message(UINT message, WPARAM wp, LPARAM) {
    if (message == OutsideMessage && wp == revision_ && Interactive()) Hide();
    if (message == LyricsMessage && mode_ == Mode::Lyrics) {
        auto result = lyrics_->Read();
        if (!result.empty()) { lyricsResult_ = std::move(result); PublishLyrics(); }
    }
    if (message == WM_MOUSEMOVE || message == WM_MOUSELEAVE)
        PostMessageW(owner_, WM_APP + 71, message, 0);
    if (message == WM_ACTIVATE && LOWORD(wp) == WA_INACTIVE && Interactive()) {
        POINT point{}; RECT anchor{};
        const bool islandClick = mode_ == Mode::Lyrics && GetCursorPos(&point) &&
            GetWindowRect(mediaAnchor_, &anchor) && PtInRect(&anchor, point) && (GetAsyncKeyState(VK_LBUTTON) & 0x8000);
        if (!islandClick) Hide();
    }
    if ((message == WM_TIMECHANGE || message == WM_SETTINGCHANGE) && mode_ == Mode::Calendar) Publish();
    if (message == WM_CLOSE || (message == WM_KEYDOWN && wp == VK_ESCAPE)) Hide();
    if (message == WM_TIMER && wp == ExitTimer) {
        KillTimer(window_.Handle(), ExitTimer);
        if (mode_ == Mode::Hidden) {
            window_.Show(false); view_.Post(L"{\"type\":\"popup:hidden\"}");
        }
    }
    if ((message == WM_DPICHANGED || message == WM_DISPLAYCHANGE) && mode_ != Mode::Hidden)
        Position(width_, height_);
}
void Popups::Position(int width, int height) {
    width_ = width; height_ = height;
    HWND anchorWindow = mode_ == Mode::Lyrics ? mediaAnchor_ : Interactive() ? owner_ : apps_;
    RECT anchor{}; GetWindowRect(anchorWindow, &anchor);
    MONITORINFO monitor{sizeof(monitor)};
    if (!GetMonitorInfoW(MonitorFromWindow(anchorWindow, MONITOR_DEFAULTTONEAREST), &monitor)) return;
    int dpi = static_cast<int>(GetDpiForWindow(anchorWindow));
    if (mode_ == Mode::Menu) {
        const auto target = MonitorFromPoint(menuPoint_, MONITOR_DEFAULTTONEAREST);
        UINT x = 96, y = 96;
        if (SUCCEEDED(GetDpiForMonitor(target, MDT_EFFECTIVE_DPI, &x, &y))) dpi = static_cast<int>(x);
        if (!GetMonitorInfoW(target, &monitor)) return;
    }
    auto scale = [dpi](int dip) { return static_cast<int>(std::ceil(dip * dpi / 96.0)); };
    if (mode_ == Mode::Menu) {
        RECT cursor{menuPoint_.x, menuPoint_.y, menuPoint_.x, menuPoint_.y};
        window_.Place(CalculatePopup(monitor.rcWork, cursor, menuPoint_.x + scale(width) / 2,
            scale(width), scale(height), 0), false); return;
    }
    const int center = mode_ == Mode::Calendar ? anchor.left + static_cast<int>(std::lround(center_ * dpi / 96)) :
        (MixerOpen() || mode_ == Mode::Lyrics) ? (anchor.left + anchor.right) / 2 :
        anchor.left + static_cast<int>(std::lround(center_ * dpi / 96));
    window_.Place(CalculatePopup(monitor.rcWork, anchor, center, scale(width), scale(height),
        scale(Interactive() ? 10 : 8)), false);
}
void Popups::Present() {
    if (!view_.Painted() || !contentReady_ || mode_ == Mode::Hidden) return;
    KillTimer(window_.Handle(), ExitTimer);
    if (fadingOut_) { fadingOut_ = false; window_.Fade(true, 160); }
    if (!IsWindowVisible(window_.Handle())) {
        Performance::Mark(L"popup.visible");
        window_.Fade(false, 0);
        window_.Show(true);
        BOOL animations = TRUE;
        SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
        window_.Fade(true, animations ? 160 : 0);
    }
    if (Interactive()) { BeginDismiss(); SetForegroundWindow(window_.Handle()); SetFocus(window_.Handle()); }
}
bool Popups::Command(const std::wstring& command, const std::vector<RunningApp>& apps) {
    if (!initialized_) return false;
    if (command.rfind(L"menu:action:", 0) == 0) {
        unsigned action = 0; std::wistringstream input(command.substr(12));
        if (mode_ == Mode::Menu && (input >> action)) { auto handler = menuAction_; Hide(); if (handler) handler(action); }
        return true;
    }
    if (command == L"lyrics" || command == L"mixer" || command.rfind(L"calendar:", 0) == 0 ||
        (command.rfind(L"tooltip:", 0) == 0 && command != L"tooltip:hide")) {
        Performance::Mark(L"popup.command:" + command.substr(0, command.find(L':')));
        if (!EnsureView()) { PostQuitMessage(1); return true; }
    }
    if (command.rfind(L"volume:wheel:", 0) == 0) {
        audio_.Open(owner_);
        std::wistringstream in(command.substr(13)); double delta = 0;
        if (in >> delta) audio_.Adjust(0, delta);
        return true;
    }
    if (command.rfind(L"audio:wheel ", 0) == 0) {
        std::wistringstream in(command.substr(12)); unsigned id = 0; double delta = 0;
        if (MixerOpen() && (in >> id >> delta) && id) audio_.Adjust(id, delta);
        return true;
    }
    if (command.rfind(L"popup:present:", 0) == 0) {
        unsigned revision = 0; std::wistringstream in(command.substr(14));
        if ((in >> revision) && revision == revision_ && mode_ != Mode::Hidden) {
            if (mode_ == Mode::Calendar) {
                int width = 0, height = 0; RECT client{}; GetClientRect(window_.Handle(), &client);
                const double scale = GetDpiForWindow(window_.Handle()) / 96.0;
                if (!(in >> width >> height) || std::abs(width * scale - client.right) > scale ||
                    std::abs(height * scale - client.bottom) > scale) return true;
            }
            contentReady_ = true; Present();
        }
        return true;
    }
    if (command == L"lyrics:retry") {
        if (mode_ == Mode::Lyrics && lyrics_) { lyricsResult_.clear(); lyrics_->Request(track_); PublishLyrics(); }
        return true;
    }
    if (command == L"lyrics:genius") {
        if (mode_ == Mode::Lyrics && !track_.title.empty()) {
            auto bytes = winrt::to_string(track_.artist + L" " + track_.title);
            std::wstring url = L"https://genius.com/search?q=";
            constexpr wchar_t hex[] = L"0123456789ABCDEF";
            for (unsigned char c : bytes) { url += L'%'; url += hex[c >> 4]; url += hex[c & 15]; }
            ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        return true;
    }
    if (command == L"lyrics") {
        if (mode_ == Mode::Lyrics) { Hide(); return true; }
        if (!track_.playing || !IsWindowVisible(mediaAnchor_)) return true;
        Hide(); mode_ = Mode::Lyrics; window_.PopupMode(true);
        if (!lyrics_) lyrics_ = std::make_unique<LyricsService>(LyricsService::Transport{},
            [window = window_.Handle()] { PostMessageW(window, LyricsMessage, 0, 0); });
        lyricsResult_.clear(); lyrics_->Request(track_);
        Position(380, 420); Publish(); Present(); return true;
    }
    if (command == L"mixer") {
        audio_.Open(owner_);
        if (MixerOpen()) { Hide(); return true; }
        Hide(); running_ = apps; mode_ = Mode::Mixer; window_.PopupMode(true);
        Position(320, 130); Publish(); Present(); return true;
    }
    if (command.rfind(L"calendar:", 0) == 0) {
        std::wistringstream position(command.substr(9)); double center = 0;
        if (!(position >> center) || !std::isfinite(center)) return true;
        center_ = center;
        if (mode_ == Mode::Calendar) { Hide(); return true; }
        Hide(); mode_ = Mode::Calendar; window_.PopupMode(true);
        Position(320, 340); Publish(); Present(); return true;
    }
    if (command == L"popup:close") { Hide(); return true; }
    if (command == L"tooltip:hide") {
        if (mode_ == Mode::Tooltip) Hide(false);
        return true;
    }
    if (command.rfind(L"tooltip:", 0) == 0) {
        if (Interactive() || !IsWindowVisible(apps_)) return true;
        std::wistringstream in(command.substr(8)); UINT_PTR id = 0; double center = 0;
        if (!(in >> id >> center) || !std::isfinite(center)) return true;
        auto item = std::find_if(apps.begin(), apps.end(), [id](const auto& app) {
            return reinterpret_cast<UINT_PTR>(app.hwnd) == id;
        });
        if (item == apps.end() || !IsWindow(item->hwnd)) { Hide(); return true; }
        RECT client{}; GetClientRect(apps_, &client);
        center_ = std::clamp(center, 0.0, client.right * 96.0 / GetDpiForWindow(apps_));
        const bool changed = mode_ != Mode::Tooltip || hovered_ != item->hwnd;
        hovered_ = item->hwnd;
        wchar_t title[4096]{}; GetWindowTextW(hovered_, title, 4096);
        const bool titleChanged = title_ != title; title_ = title;
        if (title_.empty()) { Hide(); return true; }
        mode_ = Mode::Tooltip; window_.PopupMode(false);
        if (changed) Position(200, 36); else Position(width_, height_);
        if (changed || titleChanged) Publish(); Present(); return true;
    }
    if (command.rfind(L"popup:size:", 0) == 0) {
        int width = 0, height = 0; unsigned revision = 0; std::wistringstream in(command.substr(11));
        if (mode_ != Mode::Hidden && (in >> width >> height >> revision) && revision == revision_)
            Position(mode_ == Mode::Menu ? 240 : mode_ == Mode::Lyrics ? 380 : Interactive() ? 320 : std::clamp(width, 40, 360), std::clamp(height, 32, 480));
        return true;
    }
    if (command.rfind(L"audio:", 0) == 0) {
        std::wistringstream in(command.substr(6)); std::wstring action; unsigned id; int value;
        if (MixerOpen() && (in >> action >> id >> value) && (action == L"volume" || action == L"mute"))
            audio_.Set(id, value, value != 0, action == L"mute");
        return true;
    }
    return false;
}
void Popups::PostContent(std::wstring json) {
    json.insert(1, L"\"revision\":" + std::to_wstring(revision_) + L",");
    view_.Post(json);
}
void Popups::Publish() {
    if (mode_ == Mode::Menu) { PostContent(menuState_); return; }
    if (mode_ == Mode::Lyrics) { PublishLyrics(); return; }
    if (mode_ == Mode::Tooltip) {
        PostContent(L"{\"type\":\"tooltip\",\"title\":" + Quote(title_) + L"}"); return;
    }
    if (mode_ == Mode::Calendar) {
        wchar_t locale[LOCALE_NAME_MAX_LENGTH]{}, first[8]{}, date[256]{}, hour[8]{}, pattern[128]{}, am[64]{}, pm[64]{};
        GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH);
        GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_IFIRSTDAYOFWEEK, first, 8);
        GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_ITIME, hour, 8);
        GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_STIMEFORMAT, pattern, 128);
        GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_S1159, am, 64);
        GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_S2359, pm, 64);
        GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_LONGDATE, nullptr, nullptr, date, 256, nullptr);
        PostContent(L"{\"type\":\"calendar\",\"locale\":" + Quote(locale) + L",\"date\":" + Quote(date) +
            L",\"timePattern\":" + Quote(pattern) + L",\"am\":" + Quote(am) + L",\"pm\":" + Quote(pm) + L",\"firstDay\":" + std::to_wstring((_wtoi(first) + 1) % 7) + L",\"hour12\":" + (hour[0] == L'0' ? L"true" : L"false") + L"}");
        return;
    }
    if (!MixerOpen()) return;
    auto snapshot = audio_.Read();
    // Lazy audio must deliver its first snapshot before presenting an empty mixer.
    if (snapshot.rows.empty() && snapshot.error.empty()) return;
    std::wstring json = L"{\"type\":\"mixer\",\"error\":" + Quote(snapshot.error) + L",\"rows\":[";
    bool first = true;
    for (auto& row : snapshot.rows) {
        std::string icon;
        auto app = std::find_if(running_.begin(), running_.end(), [&](const auto& a) { return row.pid && a.pid == row.pid; });
        if (app != running_.end()) icon = app->icon;
        else icon = row.icon;
        if (!first) json += L","; first = false;
        // Keep sessions separate: same executable, helper PID or grouping GUID does not prove shared identity.
        json += L"{\"id\":" + std::to_wstring(row.id) + L",\"name\":" + Quote(row.name) +
            L",\"identity\":" + Quote(row.identity) + L",\"volume\":" + std::to_wstring(row.volume) +
            L",\"muted\":" + (row.muted ? L"true" : L"false") + L",\"icon\":" +
            Quote(std::wstring(icon.begin(), icon.end())) + L"}";
    }
    PostContent(json + L"]}");
}
void Popups::Refresh(const std::vector<RunningApp>& apps) {
    running_ = apps;
    if (mode_ == Mode::Tooltip) {
        auto found = std::find_if(apps.begin(), apps.end(), [this](const auto& a) { return a.hwnd == hovered_; });
        if (found == apps.end()) Hide();
        else {
            wchar_t title[4096]{}; GetWindowTextW(hovered_, title, 4096);
            if (title_ != title) { title_ = title; Publish(); }
            Position(width_, height_);
        }
    } else if (MixerOpen()) { Position(width_, height_); Publish(); }
}
void Popups::Hide(bool immediate) {
    bool wasMixer = Interactive();
    EndDismiss();
    if (lyrics_) lyrics_->Invalidate();
    mode_ = Mode::Hidden; hovered_ = nullptr;
    ++revision_; contentReady_ = false;
    if (owner_) PostMessageW(owner_, WM_APP + 71, WM_MOUSELEAVE, 0);
    if (!window_.Handle()) return;
    if (GetCapture() == window_.Handle()) ReleaseCapture();
    if (immediate || wasMixer) {
        fadingOut_ = false;
        view_.Post(L"{\"type\":\"popup:hidden\"}");
        KillTimer(window_.Handle(), ExitTimer); window_.Show(false);
    }
    else {
        fadingOut_ = true;
        window_.Fade(false, 160);
        if (!SetTimer(window_.Handle(), ExitTimer, 160, nullptr)) window_.Show(false);
    }
}
void Popups::Stop() {
    Hide(); lyrics_.reset(); audio_.Close(); initialized_ = false; view_.Close(); window_.Destroy(); environment_.Reset();
}
