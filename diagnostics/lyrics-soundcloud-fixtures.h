#pragma once
// Deterministic fixtures: no LRCLIB availability or listening session required.
inline void SoundCloudFixtures() {
    using namespace winrt::Windows::Data::Json;
    auto row = [](const wchar_t* title, const wchar_t* artist, double duration = 200, bool synced = false) {
        JsonObject item;
        item.SetNamedValue(L"trackName", JsonValue::CreateStringValue(title));
        item.SetNamedValue(L"artistName", JsonValue::CreateStringValue(artist));
        item.SetNamedValue(L"duration", JsonValue::CreateNumberValue(duration));
        item.SetNamedValue(L"plainLyrics", JsonValue::CreateStringValue(L"fixture"));
        item.SetNamedValue(L"syncedLyrics", JsonValue::CreateStringValue(synced ? L"[00:01]synced fixture" : L""));
        return std::wstring(item.Stringify());
    };
    auto lookup = [](const std::wstring& body, const MediaSnapshot& track) { return Lyrics::Parse(L"[" + body + L"]", track, true); };
    MediaSnapshot track; track.title = L"3umph - Jesica (prod. Sonshitsuuu, Risii)"; track.artist = L"n1zamu"; track.duration = 215;
    auto metadata = Lyrics::Analyze(track);
    Check(metadata.queries.size() == 2);
    Check(metadata.queries[0] == L"3umph jesica" && metadata.queries[1] == L"jesica");
    Check(!lookup(row(L"Jessica", L"3umph"), track).empty());
    Check(lookup(row(L"Jessica", L"Unrelated"), track).empty());
    // A misspelling need not be limited to exactly one edit when the name is long enough.
    track.title = L"Beautifull Nitee"; track.artist = L"3umph";
    Check(!lookup(row(L"Beautiful Nite", L"3umph", 220), track).empty());
    track.title = L"eurooo! (prod.risii+notee1)";
    Check(!lookup(row(L"eurooo", L"3umph", 240), track).empty());
    track.title = L"ＥＵＲＯＯＯ！！！ 🎵 [official audio]";
    Check(Lyrics::Analyze(track).queries[0] == L"3umph eurooo");
    Check(!lookup(row(L"eurooo", L"3umph"), track).empty());
    track.title = L"Turbo (Majestic)"; track.artist = L"Big Baby Tape"; track.duration = 200;
    metadata = Lyrics::Analyze(track);
    Check(metadata.queries[0] == L"big baby tape turbo" && metadata.queries[1] == L"turbo");
    Check(!lookup(row(L"Turbo (Majestic)", L"Big Baby Tape"), track).empty());
    Check(!lookup(row(L"Turbo", L"Big Baby Tape"), track).empty());
    // Two similarly plausible identities must not depend on response order.
    auto ambiguous = row(L"Turbo (Majestic)", L"Big Baby Tape") + L"," + row(L"Turbo", L"Big Baby Tape");
    Check(lookup(ambiguous, track).empty());
    Check(lookup(row(L"Turbo", L"Big Baby Tape") + L"," + row(L"Turbo (Majestic)", L"Big Baby Tape"), track).empty());
    track.title = L"Turbo";
    Check(!lookup(row(L"Turbo", L"Other Performer") + L"," + row(L"Turbo", L"Big Baby Tape"), track).empty());
    Check(!lookup(row(L"Turbo", L"Big Baby Tape", 170), track).empty());
    Check(!lookup(row(L"Turbo", L"Big Baby Tape", 235), track).empty());
    Check(lookup(row(L"Unrelated", L"Big Baby Tape", 200), track).empty());
    Check(lookup(row(L"Turbo", L"Other Performer", 200), track).empty());
    Check(lookup(row(L"Turbo", L"Big Baby Tape", 600), track).empty());
    Check(lookup(row(L"Turbo Remix", L"Big Baby Tape"), track).empty());
    Check(lookup(row(L"Turbo", L"Big Baby Tape") + L"," + row(L"Turbo", L"Big Baby Tape", 201, true), track).find(L"synced fixture") != std::wstring::npos);
    track.artist = L"Alpha Beta";
    Check(lookup(row(L"Turbo", L"Alpha Beta One") + L"," + row(L"Turbo", L"Alpha Beta Two"), track).empty());
    Check(!lookup(row(L"Turbo", L"Alpha"), track).empty()); // Medium tier, clear unique lead.
    // Fallback spelling query, positive cache, and immutable visible metadata.
    track.title = L"3umph - Jesica (prod. Sonshitsuuu, Risii)"; track.artist = L"n1zamu";
    int calls = 0;
    LyricsService service([&](const auto& path) {
        ++calls;
        Check(path == (calls == 1 ? L"/api/search?q=3umph%20jesica" : L"/api/search?q=jesica"));
        return LyricsReply{200, calls == 1 ? L"[]" : L"[" + row(L"Jessica", L"3umph") + L"]"};
    });
    service.Request(track); Check(Await(service).find(L"fixture") != std::wstring::npos); Check(calls == 2);
    service.Request(track); Check(Await(service).find(L"fixture") != std::wstring::npos); Check(calls == 2);
    Check(track.artist == L"n1zamu" && track.title == L"3umph - Jesica (prod. Sonshitsuuu, Risii)");
    // Fallback cannot erase an earlier competing candidate.
    calls = 0; track.title = L"Turbo (Majestic)"; track.artist = L"Big Baby Tape";
    LyricsService ambiguousService([&](const auto&) {
        ++calls;
        return LyricsReply{200, calls == 1 ? L"[" + ambiguous + L"]" : L"[" + row(L"Turbo (Majestic)", L"Big Baby Tape") + L"]"};
    });
    ambiguousService.Request(track); Check(Await(ambiguousService).find(L"not-found") != std::wstring::npos); Check(calls == 2);
    // The second query remains visible even when the first contains 100 irrelevant rows.
    calls = 0; track.title = L"Turbo";
    LyricsService crowded([&](const auto&) {
        ++calls; std::wstring body = L"[";
        const int count = calls == 1 ? 100 : 1;
        for (int i = 0; i < count; ++i) {
            if (i) body += L",";
            body += row(calls == 1 ? L"Unrelated" : L"Turbo", L"Big Baby Tape");
        }
        return LyricsReply{200, body + L"]"};
    });
    crowded.Request(track); Check(Await(crowded).find(L"fixture") != std::wstring::npos); Check(calls == 2);
    Check(Lyrics::Clean(L"Song [produced by One, Two] official audio HD reupload") == L"song");
    std::puts("PASS SoundCloud fixtures: inferred performer, credits, spelling, Unicode, subtitles, confidence tiers, ambiguity, durations, fallback/cache");
}
