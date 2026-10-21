#include "MediaService.h"
#include "MediaSelection.h"
#include "../web/Diagnostics.h"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <wincrypt.h>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <vector>
#include <atomic>
#pragma comment(lib, "windowsapp.lib")
#pragma comment(lib, "crypt32.lib")
using namespace winrt::Windows::Media::Control;
using Session = GlobalSystemMediaTransportControlsSession;
namespace {
template<class Operation> auto Finish(Operation operation) {
    if (operation.wait_for(std::chrono::seconds(3)) != winrt::Windows::Foundation::AsyncStatus::Completed) {
        operation.Cancel(); winrt::throw_hresult(HRESULT_FROM_WIN32(ERROR_TIMEOUT));
    }
    return operation.GetResults();
}
class WindowsMediaSource final : public MediaSource {
    GlobalSystemMediaTransportControlsSessionManager manager_{nullptr};
    GlobalSystemMediaTransportControlsSessionManager::SessionsChanged_revoker sessions_;
    GlobalSystemMediaTransportControlsSessionManager::CurrentSessionChanged_revoker current_;
    struct Watch {
        Session session{nullptr};
        Session::PlaybackInfoChanged_revoker playback;
        Session::MediaPropertiesChanged_revoker metadata;
        Session::TimelinePropertiesChanged_revoker timeline;
    };
    std::vector<Watch> watches_;
    std::function<void()> signal_;
    Session last_{nullptr};
    std::wstring title_, art_, artist_, album_;
    struct Flags { std::atomic<bool> metadata{true}, sessions{true}; };
    std::shared_ptr<Flags> flags_ = std::make_shared<Flags>();
    std::wstring Artwork(const winrt::Windows::Storage::Streams::IRandomAccessStreamReference& ref) {
        if (!ref) return {};
        auto stream = Finish(ref.OpenReadAsync());
        const auto length = stream.Size();
        if (!length || length > 4 * 1024 * 1024) return {};
        auto mime = stream.ContentType();
        if (mime != L"image/png" && mime != L"image/jpeg" && mime != L"image/webp") return {};
        winrt::Windows::Storage::Streams::DataReader reader(stream);
        if (Finish(reader.LoadAsync(static_cast<unsigned>(length))) != length) return {};
        std::vector<BYTE> bytes(static_cast<size_t>(length)); reader.ReadBytes(bytes);
        DWORD count = 0;
        if (!CryptBinaryToStringW(bytes.data(), static_cast<DWORD>(bytes.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &count)) return {};
        std::wstring encoded(count, 0);
        if (!CryptBinaryToStringW(bytes.data(), static_cast<DWORD>(bytes.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, encoded.data(), &count)) return {};
        encoded.resize(count);
        return L"data:" + std::wstring(mime) + L";base64," + encoded;
    }
public:
    explicit WindowsMediaSource(std::function<void()> signal) : signal_(std::move(signal)) {
        Diagnostics::Log(L"media.gsmtc.request");
        manager_ = Finish(GlobalSystemMediaTransportControlsSessionManager::RequestAsync());
        sessions_ = manager_.SessionsChanged(winrt::auto_revoke, [flags = flags_, signal = signal_](auto&&, auto&&) { flags->sessions = true; signal(); });
        current_ = manager_.CurrentSessionChanged(winrt::auto_revoke, [signal = signal_](auto&&, auto&&) { signal(); });
        Diagnostics::Log(L"media.gsmtc.manager.subscribed");
    }
    MediaSnapshot Read() override {
        if (flags_->sessions.exchange(false)) {
            watches_.clear();
            for (auto session : manager_.GetSessions()) {
                Watch watch; watch.session = session;
                watch.playback = session.PlaybackInfoChanged(winrt::auto_revoke, [signal = signal_](auto&&, auto&&) { signal(); });
                watch.metadata = session.MediaPropertiesChanged(winrt::auto_revoke, [flags = flags_, signal = signal_](auto&&, auto&&) { flags->metadata = true; signal(); });
                watch.timeline = session.TimelinePropertiesChanged(winrt::auto_revoke, [signal = signal_](auto&&, auto&&) { signal(); });
                watches_.push_back(std::move(watch));
            }
        }
        Diagnostics::Log(L"media.gsmtc.sessions.subscribed=" + std::to_wstring(watches_.size()));
        auto playing = [](const Session& s) {
            try { return s && s.GetPlaybackInfo().PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing; }
            catch (...) { return false; }
        };
        Session selected = manager_.GetCurrentSession();
        std::vector<bool> states; int current = -1;
        for (auto& watch : watches_) {
            if (selected == watch.session) current = static_cast<int>(states.size());
            states.push_back(playing(watch.session));
        }
        const int index = Media::SelectPlaying(states, current);
        Diagnostics::Log(L"media.gsmtc.selection index=" + std::to_wstring(index));
        selected = index < 0 ? Session{nullptr} : watches_[index].session;
        if (!selected) { last_ = nullptr; return {}; }
        if (selected != last_ || flags_->metadata.exchange(false)) {
            last_ = selected; title_.clear(); art_.clear(); artist_.clear(); album_.clear();
            try {
                auto properties = Finish(selected.TryGetMediaPropertiesAsync());
                title_ = properties.Title(); artist_ = properties.Artist(); album_ = properties.AlbumTitle();
                try { art_ = Artwork(properties.Thumbnail()); } catch (...) {}
            } catch (...) {}
        }
        MediaSnapshot result; result.playing = true; result.title = title_; result.artwork = art_;
        result.artist = artist_; result.album = album_; result.player = selected.SourceAppUserModelId();
        try {
            auto t = selected.GetTimelineProperties();
            result.elapsed = std::max(0.0, (t.Position() - t.StartTime()).count() / 1e7);
            const auto age = winrt::clock::now() - t.LastUpdatedTime();
            result.elapsed += std::max(0.0, age.count() / 1e7);
            result.duration = std::max(0.0, (t.EndTime() - t.StartTime()).count() / 1e7);
        } catch (...) {}
        if (result.duration > 0) result.elapsed = std::min(result.elapsed, result.duration);
        return result;
    }
};
}
struct MediaService::State {
    std::mutex mutex;
    std::condition_variable wake;
    std::thread worker;
    bool stop = false, dirty = true, ready = false;
    HWND target = nullptr;
    MediaSnapshot snapshot;
};
MediaService::MediaService(Factory factory) : factory_(std::move(factory)) {}
MediaService::~MediaService() { Stop(); }
void MediaService::Start(HWND target) {
    if (state_) return; // One worker per lifecycle; late callbacks only hold a weak state.
    Diagnostics::Log(L"media.worker.start");
    state_ = std::make_shared<State>(); state_->target = target;
    auto state = state_; auto factory = factory_;
    state->worker = std::thread([state, factory] {
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            struct Apartment { ~Apartment() { winrt::uninit_apartment(); } } apartment;
            std::weak_ptr<State> weak = state;
            auto signal = [weak] { if (auto s = weak.lock()) { std::lock_guard lock(s->mutex); s->dirty = true; s->wake.notify_one(); } };
            std::unique_ptr<MediaSource> source;
            for (unsigned attempt = 0; attempt < 5 && !source; ++attempt) {
                { std::lock_guard lock(state->mutex); if (state->stop) return; }
                try {
                    Performance::Scope timing(L"media.initialize");
                    source = factory ? factory(signal) : std::make_unique<WindowsMediaSource>(signal);
                } catch (...) {
                    Performance::Mark(L"media.initialize.failed");
                    Diagnostics::Log(L"media.initialize.failed attempt=" + std::to_wstring(attempt + 1));
                    if (attempt == 4) throw;
                    std::unique_lock lock(state->mutex);
                    if (state->wake.wait_for(lock, std::chrono::milliseconds(250u << attempt), [&] { return state->stop; })) return;
                }
            }
            if (!source) throw std::runtime_error("Media source unavailable");
            for (;;) {
                { std::unique_lock lock(state->mutex); state->wake.wait(lock, [&] { return state->stop || state->dirty; });
                  if (state->stop) break; state->dirty = false; }
                MediaSnapshot snapshot;
                try { snapshot = source->Read(); } catch (...) { Diagnostics::Log(L"media.snapshot.read.failed"); }
                { std::lock_guard lock(state->mutex);
                  if (state->ready && state->snapshot == snapshot) continue;
                  if (!state->ready) Performance::Mark(L"media.initial.snapshot");
                  Diagnostics::Log(std::wstring(state->ready ? L"media.snapshot" : L"media.initial.snapshot") +
                      L" playing=" + std::to_wstring(snapshot.playing) +
                      L" title.length=" + std::to_wstring(snapshot.title.size()) +
                      L" artwork=" + std::to_wstring(!snapshot.artwork.empty()));
                  if (!state->ready || state->snapshot.playing != snapshot.playing)
                      Diagnostics::Log(snapshot.playing ? L"media.playback.playing" : L"media.playback.inactive");
                  snapshot.sampledAt = GetTickCount64();
                  state->ready = true; state->snapshot = std::move(snapshot);
                  if (!state->stop && state->target) Diagnostics::Check(
                      PostMessageW(state->target, Changed, 0, 0) ? S_OK : HRESULT_FROM_WIN32(GetLastError()), L"media.notify"); }
            }
            source.reset();
        } catch (...) {
            std::lock_guard lock(state->mutex);
            state->ready = true; // Degraded readiness: optional media must not hold the dock hostage.
            Performance::Mark(L"media.unavailable");
            Diagnostics::Log(L"media.unavailable retries exhausted");
            if (!state->stop && state->target) Diagnostics::Check(
                      PostMessageW(state->target, Changed, 0, 0) ? S_OK : HRESULT_FROM_WIN32(GetLastError()), L"media.notify");
        }
    });
}
void MediaService::Stop() {
    if (!state_) return;
    { std::lock_guard lock(state_->mutex); state_->stop = true; state_->wake.notify_one(); }
    state_->worker.join(); state_.reset();
}
MediaSnapshot MediaService::Read() {
    if (!state_) return {};
    std::lock_guard lock(state_->mutex); return state_->snapshot;
}
bool MediaService::Ready() {
    if (!state_) return false;
    std::lock_guard lock(state_->mutex); return state_->ready;
}
