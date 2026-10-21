#include "Popups.h"
#include <cmath>
#include <winrt/Windows.Data.Json.h>
namespace {
std::wstring Text(const std::wstring& value) {
    using namespace winrt::Windows::Data::Json;
    return std::wstring(JsonValue::CreateStringValue(value).Stringify());
}
}
void Popups::MediaChanged(const MediaSnapshot& track, HWND anchor) {
    const bool changed = track.title != track_.title || track.artist != track_.artist ||
        track.album != track_.album || track.player != track_.player;
    track_ = track; mediaAnchor_ = anchor; mediaStamp_ = track.sampledAt ? track.sampledAt : GetTickCount64();
    if (mode_ != Mode::Lyrics) return;
    if (!track.playing) { Hide(); return; }
    if (changed) {
        lyricsResult_.clear(); lyrics_->Request(track);
    }
    Position(380, 420); PublishLyrics();
}
void Popups::PublishLyrics() {
    BOOL animations = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
    double elapsed = track_.elapsed + (track_.playing ? (GetTickCount64() - mediaStamp_) / 1000.0 : 0);
    if (!std::isfinite(elapsed)) elapsed = 0;
    PostContent(L"{\"type\":\"lyrics\",\"title\":" + Text(track_.title) +
        L",\"artist\":" + Text(track_.artist) + L",\"playing\":" + (track_.playing ? L"true" : L"false") +
        L",\"elapsed\":" + std::to_wstring(elapsed) + L",\"duration\":" +
        std::to_wstring(std::isfinite(track_.duration) ? track_.duration : 0) +
        L",\"reduced\":" + (animations ? L"false" : L"true") +
        L",\"loading\":" + (lyricsResult_.empty() ? L"true" : L"false") +
        L",\"lyrics\":" + (lyricsResult_.empty() ? L"{}" : lyricsResult_) + L"}");
}
