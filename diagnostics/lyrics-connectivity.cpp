// Optional single-request probe. Never run automatically from regressions.
#include "../src/media/LyricsService.cpp"
#include <cstdio>
int main() {
    winrt::init_apartment();
    const auto started = std::chrono::steady_clock::now();
    const auto reply = Fetch(L"/api/search?q=Rick%20Astley%20Never%20Gonna%20Give%20You%20Up");
    Lyrics::Evaluation evaluation; MediaSnapshot track;
    if (reply.status == 200) Lyrics::Parse(reply.body, track, true, &evaluation);
    std::printf("http=%u requests=%u elapsed_ms=%lld category=%s\n", reply.status,
        reply.failure == LyricsFailure::Configuration ? 0u : 1u,
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count(),
        reply.failure == LyricsFailure::Configuration ? "configuration" : reply.status == 0 ? "transport" :
        reply.status != 200 ? "http-error" : evaluation.malformed ? "malformed" : evaluation.candidates ? "candidates" : "empty");
    winrt::uninit_apartment();
}
