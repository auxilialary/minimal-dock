#include "LyricsService.h"
#include "LyricsModel.h"
#include "LyricsHttp.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <chrono>
#pragma comment(lib, "winhttp.lib")
namespace {
struct Internet {
    HINTERNET value;
    ~Internet() { if (value) WinHttpCloseHandle(value); }
    operator HINTERNET() const { return value; }
};
std::wstring Encode(const std::wstring& text) {
    auto utf8 = winrt::to_string(text); std::wstring out;
    constexpr wchar_t hex[] = L"0123456789ABCDEF";
    for (unsigned char c : utf8) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_') out += c;
        else { out += L'%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}
LyricsReply Fetch(const std::wstring& path) {
    const auto agent = LyricsHttp::ConfiguredUserAgent();
    if (agent.empty()) return {0, L"", 60, LyricsFailure::Configuration};
    Internet session{WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0)};
    if (!session.value) return {};
    WinHttpSetTimeouts(session, 2000, 2000, 2000, 2000);
    Internet connection{WinHttpConnect(session, L"lrclib.net", INTERNET_DEFAULT_HTTPS_PORT, 0)};
    if (!connection.value) return {};
    Internet request{WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr, nullptr, nullptr, WINHTTP_FLAG_SECURE)};
    if (!request.value) return {};
    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));
    if (!WinHttpSendRequest(request, L"Accept: application/json\r\n", static_cast<DWORD>(-1), nullptr, 0, 0, 0) || !WinHttpReceiveResponse(request, nullptr)) return {};
    DWORD status = 0, length = sizeof(status);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &length, nullptr);
    LyricsReply reply; reply.status = status;
    if (status == 429 || status == 503) {
        reply.retrySeconds = status == 503 ? 5u : 60u;
        wchar_t retry[128]{}; length = sizeof(retry);
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_RETRY_AFTER, nullptr, retry, &length, nullptr)) {
            reply.retrySeconds = LyricsHttp::RetrySeconds(retry, status == 503 ? 5u : 60u);
        }
        return reply;
    }
    if (status != 200) return reply;
    std::string bytes; char buffer[8192]; DWORD read = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
    while (WinHttpReadData(request, buffer, sizeof(buffer), &read)) {
        if (!read) { try { reply.body = winrt::to_hstring(bytes); } catch (...) { reply.failure = LyricsFailure::Malformed; } return reply; }
        if (bytes.size() + read > 256 * 1024) { reply.failure = LyricsFailure::Malformed; return reply; }
        if (std::chrono::steady_clock::now() > deadline) { reply.failure = LyricsFailure::Network; return reply; }
        bytes.append(buffer, read);
    }
    reply.failure = LyricsFailure::Network; return reply;
}
std::wstring Cooldown(const std::wstring& state, std::chrono::steady_clock::time_point until) {
    const auto remaining = std::max<long long>(1, std::chrono::duration_cast<std::chrono::seconds>(until - std::chrono::steady_clock::now()).count() + 1);
    return L"{\"state\":\"" + state + L"\",\"retrySeconds\":" + std::to_wstring(remaining) + L"}";
}
}
struct LyricsService::State {
    std::mutex mutex; std::condition_variable wake; std::thread worker;
    bool stop = false, pending = false; unsigned generation = 0;
    MediaSnapshot track; std::wstring result;
    Transport transport;
    struct Cached { std::wstring key, result; std::chrono::steady_clock::time_point expires; };
    std::deque<Cached> cache;
    std::chrono::steady_clock::time_point nextRequest{};
    std::wstring cooldownState;
    std::chrono::steady_clock::time_point retryAfter{};
};
LyricsService::LyricsService(Transport transport) : state_(std::make_unique<State>()) {
    state_->transport = transport ? std::move(transport) : Fetch;
    auto* s = state_.get();
    s->worker = std::thread([s] {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        for (;;) {
            MediaSnapshot track; unsigned generation;
            { std::unique_lock lock(s->mutex); s->wake.wait(lock, [&] { return s->stop || s->pending; });
              if (s->stop) break; s->pending = false; track = s->track; generation = s->generation; }
            auto key = track.title + L"\n" + track.artist + L"\n" + track.album + L"\n" + std::to_wstring(track.duration);
            std::wstring result; unsigned requests = 0;
            const auto lookupStarted = std::chrono::steady_clock::now();
            try {
                auto cached = std::find_if(s->cache.begin(), s->cache.end(), [&](const auto& row) { return row.key == key && std::chrono::steady_clock::now() < row.expires; });
                if (cached != s->cache.end()) result = cached->result;
                else if (Lyrics::Clean(track.title).empty()) result = L"{\"state\":\"metadata\"}";
                else if (std::chrono::steady_clock::now() < s->retryAfter) result = Cooldown(s->cooldownState, s->retryAfter);
                else {
                    const auto metadata = Lyrics::Analyze(track);
                    std::vector<std::wstring> paths;
                    winrt::Windows::Data::Json::JsonArray retrieved;
                    for (const auto& query : metadata.queries) paths.push_back(L"/api/search?q=" + Encode(query));
                    for (size_t strategy = 0; strategy < paths.size() && requests < 2; ++strategy) {
                        {
                            std::unique_lock lock(s->mutex);
                            if (s->wake.wait_until(lock, s->nextRequest, [&] { return s->stop || generation != s->generation; })) break;
                        }
                        const auto started = std::chrono::steady_clock::now();
                        auto reply = s->transport(paths[strategy]);
                        if (reply.failure != LyricsFailure::Configuration) ++requests;
                        s->nextRequest = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
                        Lyrics::Evaluation evaluation;
                        if (reply.failure == LyricsFailure::None && reply.status == 200 && reply.body.size() <= 256 * 1024)
                        {
                            try {
                                auto rows = winrt::Windows::Data::Json::JsonArray::Parse(reply.body);
                                for (unsigned i = 0; i < std::min(rows.Size(), 100u); ++i) retrieved.Append(rows.GetAt(i));
                                const bool titleOnly = Lyrics::Clean(track.artist).empty() || strategy > 0;
                                result = Lyrics::Parse(std::wstring(retrieved.Stringify()), track, true, &evaluation, titleOnly);
                            } catch (...) { evaluation.malformed = true; }
                        }
                        const auto category = reply.failure == LyricsFailure::Configuration ? L"configuration" :
                            reply.failure == LyricsFailure::Network ? L"transport" : reply.failure == LyricsFailure::Malformed ? L"malformed" : reply.status == 429 ? L"rate-limited" : reply.status == 0 ? L"transport" :
                            reply.status != 200 ? L"http-error" : evaluation.malformed || reply.body.size() > 256 * 1024 ? L"malformed" :
                            !result.empty() ? L"found" : evaluation.candidates == 0 ? L"empty" : L"no-match";
                        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
                        OutputDebugStringW((L"lyrics http=" + std::to_wstring(reply.status) + L" requests=" + std::to_wstring(requests) +
                            L" elapsed_ms=" + std::to_wstring(elapsed) + L" category=" + category + L"\n").c_str());
                        if (reply.failure == LyricsFailure::Configuration) { result = L"{\"state\":\"configuration\"}"; break; }
                        if (reply.failure == LyricsFailure::Network) { result = L"{\"state\":\"network\"}"; break; }
                        if (reply.failure == LyricsFailure::Malformed) { result = L"{\"state\":\"malformed\"}"; break; }
                        // Preserve server cooldown even when the response belongs to a cancelled track.
                        if (reply.status == 429 || reply.status == 503) {
                            s->retryAfter = std::chrono::steady_clock::now() + std::chrono::seconds(std::max(1u, reply.retrySeconds));
                            s->cooldownState = reply.status == 429 ? L"rate-limited" : L"temporary";
                            result = Cooldown(s->cooldownState, s->retryAfter); break;
                        }
                        { std::lock_guard lock(s->mutex); if (s->stop || generation != s->generation) break; }
                        if (reply.status == 0) { result = L"{\"state\":\"network\"}"; break; }
                        if (reply.status != 200 && reply.status != 404 && reply.status != 400) { result = L"{\"state\":\"temporary\"}"; break; }
                        if (evaluation.malformed || reply.body.size() > 256 * 1024) { result = L"{\"state\":\"malformed\"}"; break; }
                        if (!result.empty()) break;
                    }
                    { std::lock_guard lock(s->mutex);
                      if (!s->stop && generation == s->generation && (result.empty() || result.find(L"\"state\"") == std::wstring::npos)) {
                        if (result.empty()) result = L"{\"state\":\"not-found\"}";
                        s->cache.push_back({key, result, std::chrono::steady_clock::now() + std::chrono::seconds(result.find(L"not-found") != std::wstring::npos ? 30 : 600)});
                        if (s->cache.size() > 12) s->cache.pop_front();
                      }
                    }

                }
            } catch (...) { result = L"{\"state\":\"malformed\"}"; }
            if (result.empty()) result = L"{\"state\":\"not-found\"}";
            OutputDebugStringW((L"lyrics requests=" + std::to_wstring(requests) + L" elapsed_ms=" +
                std::to_wstring(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - lookupStarted).count()) + L" final=" + (result.find(L"\"state\":") == std::wstring::npos ? std::wstring(L"found") : std::wstring(Lyrics::String(Lyrics::JsonObject::Parse(result), L"state"))) + L"\n").c_str());
            { std::lock_guard lock(s->mutex); if (!s->stop && generation == s->generation) s->result = std::move(result); }
        }
        winrt::uninit_apartment();
    });
}
LyricsService::~LyricsService() {
    { std::lock_guard lock(state_->mutex); state_->stop = true; state_->wake.notify_one(); }
    state_->worker.join();
}
void LyricsService::Request(const MediaSnapshot& track) {
    std::lock_guard lock(state_->mutex); ++state_->generation; state_->track = track;
    state_->result.clear(); state_->pending = true; state_->wake.notify_one();
}
void LyricsService::Invalidate() {
    std::lock_guard lock(state_->mutex); ++state_->generation; state_->pending = false; state_->result.clear(); state_->wake.notify_one();
}
std::wstring LyricsService::Read() { std::lock_guard lock(state_->mutex); return std::exchange(state_->result, {}); }
