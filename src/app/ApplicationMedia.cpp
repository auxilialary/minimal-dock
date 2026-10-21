#include "Application.h"
#include "../web/Diagnostics.h"
namespace {
std::wstring JsonText(const std::wstring& text) {
    std::wstring out = L"\"";
    for (wchar_t c : text) {
        if (c == L'"' || c == L'\\') { out += L'\\'; out += c; }
        else if (c < 32) out += L' ';
        else out += c;
    }
    return out + L"\"";
}
}
void Application::PublishMedia() {
    if (stopping_) return;
    const auto data = media_.Read();
    popups_.MediaChanged(data, mediaWindow_.Handle());
    Diagnostics::Log(L"media.publish playing=" + std::to_wstring(data.playing) +
        L" boot=" + std::to_wstring(mediaView_.Ready()) + L" painted=" + std::to_wstring(mediaView_.Painted()));
    if (data.playing && environment_ && !mediaViewStarted_) {
        mediaViewStarted_ = true;
        Diagnostics::Log(L"media.controller.request");
        if (!mediaView_.Create(environment_.Get(), mediaWindow_.Handle(), mediaWindow_.WebRoot(),
            uiDirectory_, L"media", [this] { PublishMedia(); Update(); },
            [this](const std::wstring& command) { Command(command); })) PostQuitMessage(1);
    }
    // Boot/paint callbacks replay the current snapshot even if the first notification preceded readiness.
    mediaView_.Displayed(IsWindowVisible(mediaWindow_.Handle()) != FALSE);
    mediaView_.Post(L"{\"type\":\"media\",\"playing\":" + std::wstring(data.playing ? L"true" : L"false") +
        L",\"title\":" + JsonText(data.title) + L",\"artwork\":" + JsonText(data.artwork) +
        L",\"elapsed\":" + std::to_wstring(data.elapsed) + L",\"duration\":" + std::to_wstring(data.duration) + L"}");
}
