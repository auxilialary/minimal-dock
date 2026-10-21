#include "../src/media/MediaService.h"
#include "../src/windows/EdgeTrigger.h"
#include "../src/app/VisibilityPolicy.h"
#include "../src/media/MediaSelection.h"
#include "../src/windows/IslandLayout.h"
#include "../src/windows/WindowMotion.h"
#include <mutex>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <stdexcept>
struct FakeState { std::mutex mutex; MediaSnapshot value; std::function<void()> signal; int reads = 0; };
class FakeSource final : public MediaSource {
    std::shared_ptr<FakeState> state_;
public:
    FakeSource(std::shared_ptr<FakeState> state, std::function<void()> signal) : state_(state) {
        std::lock_guard lock(state_->mutex); state_->signal = signal;
    }
    MediaSnapshot Read() override { std::lock_guard lock(state_->mutex); ++state_->reads; return state_->value; }
};
void Check(bool value) { if (!value) { std::puts("FAIL media/layout"); std::exit(1); } }
int main() {
    Check(Media::SelectPlaying({false,true,true},2)==2);
    Check(Media::SelectPlaying({false,true,true},0)==1);
    Check(Media::SelectPlaying({false,false},1)==-1);
    Check(Media::SelectPlaying({},-1)==-1);
    Check(Media::SelectPlaying({true},7)==0);
    Check(WindowMotionProgress(-1)==0 && WindowMotionProgress(2)==1);
    double previous = 0, previousStep = 1;
    for (int i=1; i<=100; ++i) {
        const double value = WindowMotionProgress(i/100.0), step = value-previous;
        Check(step >= 0 && step <= previousStep); previousStep = step; previous = value;
    }
    for (int center : {-3841, -1920, 0, 1919}) for (int width : {101, 102, 701, 702}) {
        const int parity = center & 1;
        RECT from{(center - (100 + parity)) / 2, 900, (center + 100 + parity) / 2, 956};
        const int targetWidth = (width & ~1) + parity;
        RECT to{(center - targetWidth) / 2, 900, (center + targetWidth) / 2, 956};
        for (int sample = 0; sample <= 280; ++sample) {
            const RECT frame = WindowMotionFrame(from, to, WindowMotionProgress(sample / 280.0));
            Check(frame.left + frame.right == center);
            Check(frame.top == 900 && frame.bottom == 956);
        }
    }
    auto fake = std::make_shared<FakeState>();
    std::atomic<int> starts = 0;
    MediaService media([fake, &starts](auto signal) { ++starts; return std::make_unique<FakeSource>(fake, signal); });
    // Real worker -> Win32 message queue, including the visibility-handler precedence.
    HWND owner = CreateWindowExW(0, L"STATIC", L"media-routing-test", 0, 0, 0, 1, 1,
        HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
    Check(owner != nullptr);
    { std::lock_guard lock(fake->mutex); fake->value = {true, L"Before startup", L"", 10, 200}; }
    media.Start(owner);
    media.Start(owner); // Repeated readiness must not restart the worker.
    auto notification = [&] {
        MSG message{};
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < end) {
            while (PeekMessageW(&message, owner, 0, 0, PM_REMOVE)) {
                // Application::OnMessage checks VisibilityMessage before MediaService::Changed.
                Check(message.message != EdgeTrigger::Enter && message.message != EdgeTrigger::Pointer);
                if (message.message == MediaService::Changed) return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        Check(false);
    };
    notification();
    Check(starts == 1);
    Check(media.Ready() && media.Read().playing && media.Read().title == L"Before startup");
    // Initial snapshots must not become edge input or change visibility policy.
    for (auto mode : {Visibility::Mode::Always, Visibility::Mode::Auto, Visibility::Mode::Smart}) {
        for (bool fullscreen : {false, true}) for (bool obstruction : {false, true}) {
            const bool before = Visibility::Show(mode, fullscreen, obstruction, false);
            PostMessageW(owner, MediaService::Changed, 0, 0); notification();
            Check(Visibility::Show(mode, fullscreen, obstruction, false) == before);
        }
    }

    auto wait = [&](bool playing, const wchar_t* title) {
        for (int i = 0; i < 200; ++i) {
            auto s = media.Read(); if (s.playing == playing && s.title == title) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        Check(false);
    };
    for (;;) { { std::lock_guard lock(fake->mutex); if (fake->signal) break; } std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    for (bool playing : {true, false, true, false}) {
        std::function<void()> signal;
        { std::lock_guard lock(fake->mutex); fake->value = {playing, playing ? L"Track" : L"", L"", 12, 200}; signal = fake->signal; }
        signal(); notification(); wait(playing, playing ? L"Track" : L"");
    }
    // New player selected after startup (metadata and artwork are carried in the snapshot).
    { std::lock_guard lock(fake->mutex); fake->value = {true, L"Other player", L"data:image/png;base64,AA", 5, 100}; }
    fake->signal(); notification(); wait(true, L"Other player");
    Check(media.Read().artwork == L"data:image/png;base64,AA");
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    int reads; { std::lock_guard lock(fake->mutex); reads = fake->reads; }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    { std::lock_guard lock(fake->mutex); Check(reads == fake->reads); }
    media.Stop(); DestroyWindow(owner); fake->signal(); // Late callback must be harmless.
    int cases = 0;
    for (int dpi : {96, 120, 144, 192}) for (int width : {320, 800, 1920}) for (bool running : {false, true}) for (int mw : {0, 220}) {
        auto scale = [dpi](int n) { return MulDiv(n, dpi, 96); };
        RECT monitor{-1920, 0, -1920 + scale(width), 1080 * dpi / 96};
        auto l = CalculateMediaIslands(monitor, scale(56), scale(120), scale(14), scale(16), scale(700), scale(100), running, scale(mw));
        auto inside = [&](RECT r) { Check(r.left >= monitor.left && r.right <= monitor.right && r.top >= monitor.top && r.bottom <= monitor.bottom); };
        inside(l.system); if (running) inside(l.apps); if (mw) inside(l.media);
        auto apart = [](RECT a, RECT b) { RECT overlap{}; return !IntersectRect(&overlap, &a, &b); };
        if (running) Check(apart(l.apps, l.system));
        if (mw) { Check(apart(l.media, l.system)); if (running) Check(apart(l.media, l.apps)); }
        if (running) {
            Check(l.apps.left + l.apps.right == monitor.left + monitor.right);
            auto plain = CalculateIslands(monitor, scale(56), scale(120), scale(14), scale(16), scale(700), scale(100), true);
            Check(EqualRect(&plain.apps, &l.apps) && EqualRect(&plain.system, &l.system));
        }
        if (!mw) { auto old = CalculateIslands(monitor, scale(56), scale(120), scale(14), scale(16), scale(700), scale(100), running); Check(EqualRect(&old.system, &l.system) && EqualRect(&old.apps, &l.apps)); }
        ++cases;
    }
    for (int origin : {-3841, -1920, 0, 1920}) for (int screen : {1919, 1920}) {
        RECT monitor{origin, 0, origin + screen, 1080};
        RECT previousApps{};
        for (int count = 1; count <= 12; ++count) {
            auto l = CalculateMediaIslands(monitor, 56, 120, 14, 16, 20 + count * 54, 100, true, 220);
            Check(l.apps.left + l.apps.right == monitor.left + monitor.right);
            Check(l.system.left - l.apps.right == 16 && l.apps.left - l.media.right == 16);
            Check(l.apps.top == l.media.top && l.apps.top == l.system.top);
            if (count > 1) Check(previousApps.left - l.apps.left == l.apps.right - previousApps.right);
            previousApps = l.apps;
        }
    }
    std::atomic<int> attempts = 0;
    MediaService retry([&](auto signal) -> std::unique_ptr<MediaSource> {
        if (++attempts <= 3) throw std::runtime_error("logon not ready");
        return std::make_unique<FakeSource>(fake, signal);
    });
    retry.Start(nullptr);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!retry.Ready() && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    Check(retry.Ready() && attempts == 4); retry.Stop();
    attempts = 0;
    MediaService cancelled([&](auto) -> std::unique_ptr<MediaSource> { ++attempts; throw std::runtime_error("not ready"); });
    cancelled.Start(nullptr);
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!attempts && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const auto stopStart = std::chrono::steady_clock::now(); cancelled.Stop();
    Check(attempts == 1 && std::chrono::steady_clock::now() - stopStart < std::chrono::seconds(1));
    std::printf("PASS: %d two/three-island DPI/boundary/overflow layouts; Win32 media/edge routing, pre-start playback, idempotent startup, player replacement, pause/resume/removal, idle no-polling, late callback, three logon retries and retry cancellation. Fake source only.\n", cases);
}
