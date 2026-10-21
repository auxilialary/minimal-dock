#pragma once
#include <windows.h>
#include <functional>
#include <memory>
#include <string>
struct MediaSnapshot {
    bool playing = false;
    std::wstring title, artwork;
    double elapsed = 0, duration = 0;
    std::wstring artist, album, player;
    ULONGLONG sampledAt = 0;
    bool operator==(const MediaSnapshot&) const = default;
};
class MediaSource {
public:
    virtual ~MediaSource() = default;
    virtual MediaSnapshot Read() = 0;
};
class MediaService {
public:
    static constexpr UINT Changed = WM_APP + 70;
    using Factory = std::function<std::unique_ptr<MediaSource>(std::function<void()>)>;
    explicit MediaService(Factory factory = {});
    ~MediaService();
    void Start(HWND target);
    void Stop();
    MediaSnapshot Read();
    bool Ready();
private:
    struct State;
    std::shared_ptr<State> state_;
    Factory factory_;
};
