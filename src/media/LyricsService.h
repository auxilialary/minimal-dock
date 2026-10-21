#pragma once
#include "MediaService.h"
#include <functional>
#include <memory>
enum class LyricsFailure { None, Network, Malformed, Configuration };
struct LyricsReply { unsigned status = 0; std::wstring body; unsigned retrySeconds = 60; LyricsFailure failure = LyricsFailure::None; };
class LyricsService {
public:
    using Transport = std::function<LyricsReply(const std::wstring&)>;
    explicit LyricsService(Transport transport = {});
    ~LyricsService();
    void Request(const MediaSnapshot& track);
    void Invalidate();
    std::wstring Read();
private:
    struct State;
    std::unique_ptr<State> state_;
};
