#pragma once
inline void TitleOnlyFixtures() {
    using namespace winrt::Windows::Data::Json;
    auto row = [](const wchar_t* title, const wchar_t* artist, double duration = 200) {
        JsonObject item;
        item.SetNamedValue(L"trackName", JsonValue::CreateStringValue(title));
        item.SetNamedValue(L"artistName", JsonValue::CreateStringValue(artist));
        item.SetNamedValue(L"duration", JsonValue::CreateNumberValue(duration));
        item.SetNamedValue(L"plainLyrics", JsonValue::CreateStringValue(L"title fixture"));
        return std::wstring(item.Stringify());
    };
    auto parse = [](const std::wstring& rows, const MediaSnapshot& track) {
        return Lyrics::Parse(L"[" + rows + L"]", track, true, nullptr, true);
    };
    MediaSnapshot track; track.title = L"kurokov - не будем"; track.artist = L"wqq #krv"; track.duration = 200;
    const auto queries = Lyrics::Analyze(track).queries;
    Check(queries.size() == 2 && queries[0] == L"kurokov не будем" && queries[1] == L"не будем");
    int calls = 0;
    LyricsService service([&](const auto& path) {
        ++calls;
        Check(path == (calls == 1 ? L"/api/search?q=kurokov%20%D0%BD%D0%B5%20%D0%B1%D1%83%D0%B4%D0%B5%D0%BC" :
            L"/api/search?q=%D0%BD%D0%B5%20%D0%B1%D1%83%D0%B4%D0%B5%D0%BC"));
        return LyricsReply{200, L"[" + row(L"не будем", L"Different performer") + L"]"};
    });
    service.Request(track); Check(Await(service).find(L"title fixture") != std::wstring::npos); Check(calls == 2);
    service.Request(track); Check(!Await(service).empty()); Check(calls == 2);
    track.title = L"eurooo! (prod.risii+notee1)";
    Check(Lyrics::Analyze(track).queries[1] == L"eurooo");
    Check(!parse(row(L"eurooo", L"3umph"), track).empty());
    track.title = L"3umph - Jesica (prod. Risii)";
    Check(Lyrics::Analyze(track).queries[1] == L"jesica");
    Check(!parse(row(L"Jessica", L"Someone else"), track).empty());
    Check(!parse(row(L"Jessica", L"Someone else", 215), track).empty());
    track.title = L"Turbo (Majestic)";
    Check(Lyrics::Analyze(track).queries[1] == L"turbo");
    track.title = L"Turbo"; track.artist.clear();
    Check(Lyrics::Analyze(track).queries.size() == 1);
    Check(!parse(row(L"Turbo", L"One"), track).empty());
    Check(parse(row(L"Turbo", L"One") + L"," + row(L"Turbo", L"Two"), track).empty());
    Check(parse(row(L"Turbo", L"Two") + L"," + row(L"Turbo", L"One"), track).empty());
    Check(!parse(row(L"Turbo", L"One") + L"," + row(L"Turbo", L"Two", 280), track).empty());
    Check(parse(row(L"Turbo", L"One") + L"," + row(L"Turbo", L"One", 210), track).empty());
    Check(parse(row(L"Unrelated", L"One"), track).empty());
    Check(parse(row(L"Turbo Remix", L"One"), track).empty());
    Check(parse(row(L"Turbo", L"One", 600), track).empty());
    auto synced = JsonObject::Parse(row(L"Turbo", L"One"));
    synced.SetNamedValue(L"syncedLyrics", JsonValue::CreateStringValue(L"[00:01]synced title fixture"));
    Check(parse(row(L"Turbo", L"One") + L"," + std::wstring(synced.Stringify()), track).find(L"synced title fixture") != std::wstring::npos);
    auto unknown = track; unknown.duration = 0;
    Check(!parse(row(L"Turbo", L"One"), unknown).empty());
    Check(parse(row(L"Turbo", L"One") + L"," + row(L"Turbo", L"Two"), unknown).empty());
    calls = 0;
    LyricsService single([&](const auto& path) {
        ++calls; Check(path == L"/api/search?q=turbo");
        return LyricsReply{200, L"[" + row(L"Turbo", L"One") + L"]"};
    });
    single.Request(track); Check(Await(single).find(L"title fixture") != std::wstring::npos); Check(calls == 1);
    std::puts("PASS title-only: uploader mismatch, Cyrillic, single word, spelling, durations, ambiguity, cache, artist-free HTTP query");
}
