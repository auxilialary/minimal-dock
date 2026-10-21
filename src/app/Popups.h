#pragma once
#include "../windows/DockWindow.h"
#include "../windows/WindowTracker.h"
#include "../web/WebViewHost.h"
#include "../audio/AudioMixer.h"
#include "../media/LyricsService.h"
class Popups {
public:
    ~Popups() { Stop(); }
    bool Initialize(ICoreWebView2Environment* environment, const std::wstring& directory,
        HWND owner, HWND apps, int radius, float border);
    bool Command(const std::wstring& command, const std::vector<RunningApp>& apps);
    void Refresh(const std::vector<RunningApp>& apps);
    void Hide(bool immediate = true);
    void Stop();
    void MediaChanged(const MediaSnapshot& track, HWND anchor);
    void ShowMenu(std::wstring state, std::function<void(unsigned)> action);
    void FollowMediaAnchor() { if (mode_ == Mode::Lyrics) Position(380, 420); }
    void AudioChanged() { if (MixerOpen()) Publish(); }
    bool MixerOpen() const { return mode_ == Mode::Mixer; }
    bool MenuOpen() const { return mode_ == Mode::Menu; }
    bool Interactive() const { return mode_ == Mode::Menu || mode_ == Mode::Mixer || mode_ == Mode::Calendar || mode_ == Mode::Lyrics; }
    HWND Handle() const { return window_.Handle(); }
private:
    bool EnsureView();
    void Dismiss() { Hide(mode_ != Mode::Menu); }
    void PublishLyrics();
    void BeginDismiss();
    void EndDismiss();
    static LRESULT CALLBACK OutsideClick(int code, WPARAM wp, LPARAM lp);
    static Popups* hookOwner_;
    HHOOK mouseHook_ = nullptr;
    static constexpr UINT OutsideMessage = WM_APP + 74;
    static constexpr UINT LyricsMessage = WM_APP + 75;
    std::unique_ptr<LyricsService> lyrics_;
    MediaSnapshot track_;
    HWND mediaAnchor_ = nullptr;
    std::wstring lyricsResult_;
    ULONGLONG mediaStamp_ = 0;
    enum class Mode { Hidden, Tooltip, Mixer, Calendar, Lyrics, Menu };
    std::wstring menuState_;
    std::function<void(unsigned)> menuAction_;
    POINT menuPoint_{};
    void Message(UINT message, WPARAM wp, LPARAM lp);
    void Publish();
    void PostContent(std::wstring json);
    void Position(int width, int height);
    void Present();
    DockWindow window_;
    WebViewHost view_;
    AudioMixer audio_;
    HWND owner_ = nullptr, apps_ = nullptr, hovered_ = nullptr;
    Mode mode_ = Mode::Hidden;
    double center_ = 0;
    int width_ = 200, height_ = 36;
    std::wstring title_;
    std::vector<RunningApp> running_;
    unsigned revision_ = 0;
    bool contentReady_ = false;
    bool initialized_ = false;
    Microsoft::WRL::ComPtr<ICoreWebView2Environment> environment_;
    std::wstring directory_;
    int radius_ = 17;
    float border_ = .23f;
    bool fadingOut_ = false;
};
