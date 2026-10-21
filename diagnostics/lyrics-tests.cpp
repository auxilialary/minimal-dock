#include "../src/media/LyricsService.h"
#include "../src/media/LyricsModel.h"
#include "../src/media/LyricsHttp.h"
#include "../src/windows/PopupLayout.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <cstdio>
#include <cstdlib>
void CheckLine(bool value, int line) { if (!value) { std::printf("FAIL lyrics line %d\n", line); std::exit(1); } }
#define Check(value) CheckLine(value, __LINE__)
std::wstring Await(LyricsService& service) {
    for (int i = 0; i < 400; ++i) { auto result = service.Read(); if (!result.empty()) return result; Sleep(5); }
    Check(false); return {};
}
std::wstring Row(const wchar_t* title = L"Song") {
    return L"{\"trackName\":\"" + std::wstring(title) + L"\",\"artistName\":\"Artist\",\"duration\":200,\"id\":3,\"syncedLyrics\":null,\"plainLyrics\":\"hello\",\"instrumental\":false}";
}
#include "lyrics-soundcloud-fixtures.h"
#include "lyrics-title-only-fixtures.h"
int main() {
    winrt::init_apartment();
    struct Apartment { ~Apartment() { winrt::uninit_apartment(); } } apartment;
    Check(LyricsHttp::UserAgent(L"").empty());
    Check(LyricsHttp::UserAgent(L"https://example.org/project").find(L"GlassSplitDock/1.0") == 0);
    Check(!LyricsHttp::UserAgent(L"contact@example.org").empty());
    Check(LyricsHttp::UserAgent(L"https://example.org/\r\nInjected:yes").empty());
    Check(LyricsHttp::UserAgent(L"unconfirmed").empty());
    Check(LyricsHttp::RetrySeconds(L"30") == 30);
    Check(LyricsHttp::RetrySeconds(L"0") == 1);
    Check(LyricsHttp::RetrySeconds(L"99999999999999999999999") == MAXDWORD);
    Check(LyricsHttp::RetrySeconds(L"-1") == 60);
    Check(LyricsHttp::RetrySeconds(L"invalid", 5) == 5);
    Check(LyricsHttp::RetrySeconds(L"Wed, 21 Oct 2015 07:28:00 GMT") == 1);
    // Default contact works without network access; explicit invalid overrides remain errors.
    { wchar_t saved[2048]{}; GetEnvironmentVariableW(L"GLASSSPLIT_LRCLIB_CONTACT", saved, 2048);
      SetEnvironmentVariableW(L"GLASSSPLIT_LRCLIB_CONTACT", nullptr);
      Check(LyricsHttp::ConfiguredUserAgent() == L"GlassSplitDock/1.0 (https://github.com/auxilialary/minimal-dock)");
      SetEnvironmentVariableW(L"GLASSSPLIT_LRCLIB_CONTACT", L"invalid");
      Check(LyricsHttp::ConfiguredUserAgent().empty());
      SetEnvironmentVariableW(L"GLASSSPLIT_LRCLIB_CONTACT", *saved ? saved : nullptr); }
    MediaSnapshot track; track.title = L"Song"; track.artist = L"Artist"; track.duration = 200;
    Check(!Lyrics::Parse(Row(), track, false).empty());
    Check(Lyrics::Parse(Row(L"Other"), track, false).empty());
    auto bad = track; bad.artist = L"Wrong"; Check(Lyrics::Parse(Row(), bad, false).empty());
    bad = track; bad.duration = 210; Check(!Lyrics::Parse(Row(), bad, false).empty());
    bad = track; bad.artist.clear(); Check(Lyrics::Parse(Row(), bad, false).empty());
    Check(Lyrics::Parse(L"not json", track, false).empty());
    Check(!Lyrics::Parse(L"[{}," + Row() + L"]", track, true).empty());
    auto instrumental = winrt::Windows::Data::Json::JsonObject::Parse(Row());
    instrumental.SetNamedValue(L"instrumental", winrt::Windows::Data::Json::JsonValue::CreateBooleanValue(true));
    instrumental.SetNamedValue(L"plainLyrics", winrt::Windows::Data::Json::JsonValue::CreateNullValue());
    Check(Lyrics::Parse(std::wstring(instrumental.Stringify()), track, false).find(L"true") != std::wstring::npos);
    auto synced = winrt::Windows::Data::Json::JsonObject::Parse(Row());
    synced.SetNamedValue(L"syncedLyrics", winrt::Windows::Data::Json::JsonValue::CreateStringValue(L"[00:01]Synced"));
    Check(Lyrics::Parse(L"[" + Row() + L"," + std::wstring(synced.Stringify()) + L"]", track, true).find(L"Synced") != std::wstring::npos);
    Check(Lyrics::Clean(L"eurooo! (prod.risii+notee1)") == L"eurooo");
    Check(Lyrics::Clean(L"Song feat. Guest") == L"song");
    Check(Lyrics::Clean(L"Song feat. Guest (Remix)") == L"song remix");
    Check(Lyrics::Clean(L"SONG! 🎵 [free download]") == L"song");
    Check(Lyrics::Normalize(L"ПРИВЕТ　Мир") == L"привет мир");
    bad = track; bad.title = L"Song (Remix)"; Check(Lyrics::Parse(Row(), bad, false).empty());
    bad = track; bad.title = L"Song (prod. Someone)"; Check(!Lyrics::Parse(Row(), bad, false).empty());
    bad = track; bad.duration = 400; Check(Lyrics::Parse(Row(), bad, false).empty());
    bad = track; bad.title = L"Song Different"; Check(Lyrics::Parse(Row(), bad, false).empty());
    Check(Lyrics::Parse(L"[]", track, true).empty());
    std::atomic<int> calls = 0;
    {
        LyricsService service([&](const auto&) { ++calls; return LyricsReply{200, L"[" + Row() + L"]"}; });
        auto empty = bad; empty.title.clear(); service.Request(empty); Check(Await(service).find(L"metadata") != std::wstring::npos); Check(calls == 0);
    }
    {
        LyricsService service([&](const auto&) { ++calls; return LyricsReply{}; });
        service.Request(track); Check(Await(service).find(L"network") != std::wstring::npos); Check(calls == 1);
        calls = 0;
    }
    {
        LyricsService service([&](const auto&) { ++calls; return LyricsReply{200, L"[" + Row() + L"]"}; });
        service.Request(track); Check(Await(service).find(L"hello") != std::wstring::npos);
        service.Request(track); Check(!Await(service).empty()); Check(calls == 1);
    }
    {
        calls = 0;
        LyricsService service([&](const auto& path) { ++calls; return path.find(L"/api/get") == 0 ? LyricsReply{404, L""} : LyricsReply{200, L"[" + Row() + L"]"}; });
        service.Request(track); Check(Await(service).find(L"hello") != std::wstring::npos); Check(calls == 1);
    }
    {
        calls = 0;
        LyricsService service([&](const auto&) { ++calls; return LyricsReply{429, L"", 30}; });
        service.Request(track); Check(Await(service).find(L"state") != std::wstring::npos);
        service.Request(track); Check(Await(service).find(L"rate-limited") != std::wstring::npos); Check(calls == 1);
    }
    for (unsigned status : {0u, 500u}) {
        LyricsService service([status](const auto&) { return LyricsReply{status, L""}; });
        service.Request(track); Check(Await(service).find(status == 0 ? L"network" : L"temporary") != std::wstring::npos);
    }
    {
        LyricsService service([](const auto&) { return LyricsReply{200, std::wstring(262145, L'x')}; });
        service.Request(track); Check(Await(service).find(L"state") != std::wstring::npos);
    }
    {
        std::mutex mutex; std::condition_variable wake; bool entered = false, release = false;
        LyricsService service([&](const auto&) {
            std::unique_lock lock(mutex); entered = true; wake.notify_all(); wake.wait(lock, [&] { return release; });
            return LyricsReply{200, L"[" + Row() + L"]"};
        });
        service.Request(track);
        { std::unique_lock lock(mutex); Check(wake.wait_for(lock, std::chrono::seconds(2), [&] { return entered; })); }
        auto replacement = track; replacement.title = L"Other";
        service.Request(replacement);
        { std::lock_guard lock(mutex); release = true; wake.notify_all(); }
        Check(Await(service).find(L"state") != std::wstring::npos);
        service.Invalidate(); Check(service.Read().empty());
    }
    {
        calls = 0; auto example = track; example.title = L"eurooo! (prod.risii+notee1)"; example.artist = L"3umph"; example.duration = 118;
        LyricsService service([&](const auto& path) {
            ++calls;
            if (path == L"/api/search?q=3umph%20eurooo") return LyricsReply{200, L"[{\"trackName\":\"eurooo\",\"artistName\":\"3umph\",\"duration\":123,\"plainLyrics\":\"mock\"}]"};
            return path.find(L"/api/get") == 0 ? LyricsReply{404, L""} : LyricsReply{200, L"[]"};
        });
        service.Request(example); Check(Await(service).find(L"mock") != std::wstring::npos); Check(calls == 1);
    }
    {
        auto missing = track; missing.artist.clear(); missing.title = L"Distinctive Song";
        LyricsService service([&](const auto& path) {
            Check(path == L"/api/search?q=distinctive%20song");
            return LyricsReply{200, L"[" + Row(L"Distinctive Song") + L"]"};
        });
        service.Request(missing); Check(Await(service).find(L"hello") != std::wstring::npos);
    }
    {
        auto example = track; example.title = L"3umph - Jesica (prod. Sonshitsuuu, Risii)"; example.artist = L"n1zamu";
        auto identity = Lyrics::SearchIdentity(example);
        Check(identity.artist == L"3umph" && identity.title == L"jesica");
        for (auto separator : {L" – ", L" — ", L": "}) {
            example.title = L"3umph" + std::wstring(separator) + L"Jesica";
            Check(Lyrics::SearchIdentity(example).title == L"jesica");
        }
        const std::wstring candidate = L"[{\"trackName\":\"Jessica\",\"artistName\":\"3umph\",\"duration\":200,\"plainLyrics\":\"mock\"}]";
        Check(!Lyrics::Parse(candidate, identity, true).empty());
        auto wrong = identity; wrong.duration = 210; Check(!Lyrics::Parse(candidate, wrong, true).empty());
        wrong = identity; wrong.artist = L"n1zamu"; Check(Lyrics::Parse(candidate, wrong, true).empty());
        calls = 0;
        LyricsService service([&](const auto& path) { ++calls; Check(path == L"/api/search?q=3umph%20jesica"); return LyricsReply{200, candidate}; });
        example.title = L"3umph - Jesica (prod. Sonshitsuuu, Risii)";
        service.Request(example); Check(Await(service).find(L"mock") != std::wstring::npos); Check(calls == 1);
    }
    {
        calls = 0; std::vector<std::wstring> queries;
        LyricsService service([&](const auto& path) { ++calls; queries.push_back(path); return LyricsReply{200, L"[]"}; });
        service.Request(track); Check(Await(service).find(L"not-found") != std::wstring::npos); Check(calls == 2);
        Check(queries[0] != queries[1]);
        service.Invalidate(); service.Request(track); Check(Await(service).find(L"not-found") != std::wstring::npos); Check(calls == 2);
    }
    for (unsigned status : {429u, 503u, 500u}) {
        calls = 0;
        LyricsService service([&](const auto&) { ++calls; return LyricsReply{status, L"", 30}; });
        service.Request(track); auto result = Await(service);
        Check(result.find(status == 429 ? L"rate-limited" : L"temporary") != std::wstring::npos); Check(calls == 1);
        if (status == 500) continue;
        Check(result.find(L"retrySeconds") != std::wstring::npos);
        auto next = track; next.title = L"Next";
        service.Invalidate(); service.Request(next); Check(!Await(service).empty()); Check(calls == 1);
    }
    {
        calls = 0;
        LyricsService service([&](const auto&) { ++calls; return LyricsReply{200, L"bad json"}; });
        service.Request(track); Check(Await(service).find(L"malformed") != std::wstring::npos); Check(calls == 1);
    }
    {
        calls = 0;
        const auto start = std::chrono::steady_clock::now();
        { LyricsService service([&](const auto&) { ++calls; return LyricsReply{200, L"[]"}; });
          service.Request(track); while (!calls) Sleep(1); service.Invalidate(); }
        Check(calls == 1); Check(std::chrono::steady_clock::now() - start < std::chrono::seconds(1));
    }
    {
        // A cancelled track's late 429 must still protect the pending track.
        calls = 0; std::mutex mutex; std::condition_variable wake; bool entered = false, release = false;
        LyricsService service([&](const auto&) {
            ++calls; std::unique_lock lock(mutex); entered = true; wake.notify_all();
            wake.wait(lock, [&] { return release; }); return LyricsReply{429, L"", 30};
        });
        service.Request(track);
        { std::unique_lock lock(mutex); Check(wake.wait_for(lock, std::chrono::seconds(2), [&] { return entered; })); }
        for (int i = 0; i < 10; ++i) { auto next = track; next.title = std::to_wstring(i); service.Request(next); }
        { std::lock_guard lock(mutex); release = true; wake.notify_all(); }
        Check(Await(service).find(L"rate-limited") != std::wstring::npos); Check(calls == 1);
    }
    {
        std::vector<std::chrono::steady_clock::time_point> stamps;
        LyricsService service([&](const auto&) { stamps.push_back(std::chrono::steady_clock::now()); return LyricsReply{200, L"[]"}; });
        service.Request(track); Check(!Await(service).empty());
        Check(stamps.size() == 2); Check(stamps[1] - stamps[0] >= std::chrono::milliseconds(300));
        auto next = track; next.title = L"Another"; service.Request(next); Check(!Await(service).empty());
        Check(stamps.size() == 4); Check(stamps[2] - stamps[1] >= std::chrono::milliseconds(300));
    }
    for (auto failure : {LyricsFailure::Network, LyricsFailure::Malformed}) {
        calls = 0; LyricsService service([&](const auto&) { ++calls; return LyricsReply{200, L"", 60, failure}; });
        service.Request(track); Check(Await(service).find(failure == LyricsFailure::Network ? L"network" : L"malformed") != std::wstring::npos);
        Check(calls == 1);
    }
    for (int dpi : {96,120,144,192}) {
        RECT work{-1920,0,0,1080}, anchor{-1000,1000,-780,1056};
        auto popup = CalculatePopup(work, anchor, -890, MulDiv(380,dpi,96), MulDiv(420,dpi,96), 10);
        Check(popup.left >= work.left && popup.right <= work.right && popup.top >= work.top && popup.bottom <= anchor.top);
    }
    SoundCloudFixtures();
    TitleOnlyFixtures();
    std::puts("PASS lyrics native: response/matching, null/plain, fallback, cache, stale replacement, invalidation, 429, failure, oversize, DPI placement");
}
