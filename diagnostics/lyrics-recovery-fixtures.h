#pragma once
void RecoveryFixtures(const MediaSnapshot& track) {
    {
        std::atomic<unsigned> calls = 0;
        LyricsService service([&](const auto&) {
            ++calls; auto row = Row(); row.replace(row.find(L"hello"), 5, L"temporary network");
            return LyricsReply{200, L"[" + row + L"]"};
        });
        service.Request(track); Check(Await(service).find(L"temporary network") != std::wstring::npos);
        Sleep(1100); Check(calls == 1); Check(service.Read().empty());
    }
    for (unsigned status : {429u, 503u, 0u}) {
        std::atomic<unsigned> calls = 0;
        std::atomic<unsigned> notifications = 0;
        std::atomic<ULONGLONG> retriedAt = 0;
        const auto started = GetTickCount64();
        LyricsService service([&](const auto&) {
            if (++calls == 1) return LyricsReply{status, L"", 1};
            retriedAt = GetTickCount64();
            return LyricsReply{200, L"[" + Row() + L"]"};
        }, [&] { ++notifications; });
        service.Request(track);
        Check(Await(service).find(L"retrySeconds") != std::wstring::npos);
        std::wstring result;
        for (int i = 0; i < 600 && result.find(L"hello") == std::wstring::npos; ++i) {
            auto next = service.Read(); if (!next.empty()) result = next; Sleep(5);
        }
        Check(result.find(L"hello") != std::wstring::npos); Check(calls == 2);
        Check(retriedAt - started >= 1000); Check(notifications >= 3);
    }
    {
        const auto started = GetTickCount64();
        { LyricsService service([](const auto&) { return LyricsReply{429, L"", 60}; });
          service.Request(track); Check(!Await(service).empty()); }
        Check(GetTickCount64() - started < 1000);
    }
    for (bool replace : {false, true}) {
        std::atomic<unsigned> calls = 0;
        LyricsService service([&](const auto&) { ++calls; return LyricsReply{503, L"", 1}; });
        service.Request(track); Check(!Await(service).empty()); service.Invalidate();
        if (replace) {
            auto next = track; next.title.clear(); service.Request(next);
            Check(Await(service).find(L"metadata") != std::wstring::npos);
        }
        Sleep(1150); Check(calls == 1); Check(service.Read().empty());
    }
    {
        std::atomic<unsigned> calls = 0;
        LyricsService service([&](const auto&) { ++calls; return LyricsReply{503, L"", 1}; });
        service.Request(track); std::wstring result;
        for (int i = 0; i < 1800 && result.find(L"exhausted") == std::wstring::npos; ++i) {
            auto next = service.Read(); if (!next.empty()) result = next; Sleep(5);
        }
        Check(result.find(L"exhausted") != std::wstring::npos); Check(calls == 4);
        Sleep(1100); Check(calls == 4);
    }
    std::puts("PASS automatic recovery: 429/503/offline, dismissal/invalidation, replacement and bounded attempts");
}
