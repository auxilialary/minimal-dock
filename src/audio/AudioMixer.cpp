#include "AudioMixer.h"
#include "AudioNotifications.h"
#include "../windows/WindowTracker.h"
#include "../web/Performance.h"
#include <condition_variable>
#include <mutex>
#include <thread>
#include <map>
#include <filesystem>
#include <deque>
#include <chrono>
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")
using Microsoft::WRL::ComPtr;
struct AudioMixer::State : std::enable_shared_from_this<State> {
    std::mutex mutex;
    std::condition_variable wake;
    std::thread worker;
    HWND target = nullptr;
    DeviceFactory factory;
    bool stop = false, dirty = true, rebind = true;
    struct Command { unsigned id; int percent; bool mute, setMute; };
    std::map<std::pair<unsigned, bool>, Command> commands;
    std::map<unsigned, double> adjustments;
    std::vector<ComPtr<IAudioSessionControl>> incoming;
    Audio::Snapshot snapshot;
    struct Session {
        unsigned id;
        ComPtr<IAudioSessionControl2> control;
        ComPtr<ISimpleAudioVolume> volume;
        Audio::Row metadata;
    };
    std::vector<Session> sessions;
    unsigned nextId = 1;
    ComPtr<IMMDeviceEnumerator> devices;
    ComPtr<IAudioEndpointVolume> endpoint;
    ComPtr<IAudioSessionManager2> manager;
    ComPtr<AudioNotifications> notifications;
    ComPtr<AudioNotifications> deviceNotifications;
    unsigned generation = 0;
    WindowTracker icons;

    ComPtr<AudioNotifications> MakeNotifications(bool scoped) {
        auto sink = Microsoft::WRL::Make<AudioNotifications>();
        std::weak_ptr<State> weak = shared_from_this();
        const unsigned token = generation;
        sink->signal = [weak, scoped, token](bool reset, IAudioSessionControl* session) {
            if (auto self = weak.lock()) {
                std::lock_guard lock(self->mutex);
                if (self->stop || (scoped && token != self->generation)) return;
                self->dirty = true; self->rebind |= reset;
                if (session) self->incoming.emplace_back(session);
                self->wake.notify_one();
            }
        };
        return sink;
    }

    void Unbind() {
        for (auto& s : sessions) s.control->UnregisterAudioSessionNotification(notifications.Get());
        sessions.clear();
        if (manager) manager->UnregisterSessionNotification(notifications.Get());
        if (endpoint) endpoint->UnregisterControlChangeNotify(notifications.Get());
        manager.Reset(); endpoint.Reset();
    }
    void Add(IAudioSessionControl* source) {
        Session s{};
        if (!source || FAILED(source->QueryInterface(IID_PPV_ARGS(&s.control))) ||
            FAILED(source->QueryInterface(IID_PPV_ARGS(&s.volume)))) return;
        LPWSTR raw = nullptr;
        if (FAILED(s.control->GetSessionInstanceIdentifier(&raw))) return;
        std::wstring identity = raw ? raw : L""; CoTaskMemFree(raw);
        for (auto& existing : sessions) {
            raw = nullptr; existing.control->GetSessionInstanceIdentifier(&raw);
            bool duplicate = raw && identity == raw; CoTaskMemFree(raw);
            if (duplicate) return;
        }
        s.id = nextId++;
        if (FAILED(s.control->RegisterAudioSessionNotification(notifications.Get()))) return;
        sessions.push_back(std::move(s));
    }
    bool Bind() {
        { std::lock_guard lock(mutex); ++generation; incoming.clear(); }
        Unbind();
        notifications = MakeNotifications(true);
        ComPtr<IMMDevice> device;
        if (!devices || FAILED(devices->GetDefaultAudioEndpoint(eRender, eMultimedia, &device)) ||
            FAILED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, &endpoint)) ||
            FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, &manager))) return false;
        if (FAILED(endpoint->RegisterControlChangeNotify(notifications.Get())) ||
            FAILED(manager->RegisterSessionNotification(notifications.Get()))) return false;
        ComPtr<IAudioSessionEnumerator> enumerator;
        if (FAILED(manager->GetSessionEnumerator(&enumerator))) return false;
        int count = 0;
        if (FAILED(enumerator->GetCount(&count))) return false; // Enables creation notifications.
        for (int i = 0; i < count; ++i) {
            ComPtr<IAudioSessionControl> session;
            if (SUCCEEDED(enumerator->GetSession(i, &session))) Add(session.Get());
        }
        return true;
    }
    void Publish(bool available) {
        Audio::Snapshot result;
        if (!available || !endpoint) result.error = L"No default audio output device";
        else {
            Audio::Row master; master.name = L"Master volume";
            float scalar = 0; BOOL mute = FALSE;
            if (FAILED(endpoint->GetMasterVolumeLevelScalar(&scalar)) || FAILED(endpoint->GetMute(&mute))) {
                result.error = L"Audio device unavailable";
            } else {
                master.volume = Audio::Percent(scalar); master.muted = mute != FALSE;
                result.rows.push_back(master);
            }
            for (auto it = sessions.begin(); it != sessions.end();) {
                AudioSessionState status{};
                if (FAILED(it->control->GetState(&status)) || status == AudioSessionStateExpired) {
                    it->control->UnregisterAudioSessionNotification(notifications.Get());
                    it = sessions.erase(it); continue;
                }
                auto& s = *it++;
                Audio::Row row = s.metadata; row.id = s.id;
                if (row.identity.empty()) {
                    // AUDCLNT_S_NO_SINGLE_PROCESS is deliberately not treated as a single PID.
                    if (s.control->GetProcessId(&row.pid) != S_OK) row.pid = 0;
                    LPWSTR raw = nullptr;
                    s.control->GetSessionInstanceIdentifier(&raw);
                    if (raw) row.identity = raw; CoTaskMemFree(raw); raw = nullptr;
                    if (row.pid) {
                        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, row.pid);
                        wchar_t path[32768]{}; DWORD length = 32768;
                        if (process && QueryFullProcessImageNameW(process, 0, path, &length)) row.path.assign(path, length);
                        if (process) CloseHandle(process);
                    }
                    s.metadata = row;
                    if (!row.path.empty()) {
                        RunningApp app; app.path = row.path;
                        row.icon = icons.IconFor(app); s.metadata.icon = row.icon;
                    }
                }
                LPWSTR raw = nullptr;
                s.control->GetDisplayName(&raw);
                if (raw) row.name = raw; CoTaskMemFree(raw);
                if (!row.name.empty() && row.name.front() == L'@') {
                    wchar_t resolved[1024]{};
                    if (SUCCEEDED(SHLoadIndirectString(row.name.c_str(), resolved, 1024, nullptr))) row.name = resolved;
                }
                if (s.control->IsSystemSoundsSession() == S_OK) row.name = L"System sounds";
                if (row.name.empty() && !row.path.empty()) row.name = std::filesystem::path(row.path).stem().wstring();
                if (row.name.empty()) row.name = row.identity.empty() ? L"Audio session" : row.identity;
                if (FAILED(s.volume->GetMasterVolume(&scalar)) || FAILED(s.volume->GetMute(&mute))) continue;
                row.volume = Audio::Percent(scalar); row.muted = mute != FALSE;
                result.rows.push_back(std::move(row));
            }
        }
        std::lock_guard lock(mutex);
        if (snapshot.rows == result.rows && snapshot.error == result.error) return;
        snapshot = std::move(result);
        if (!stop) PostMessageW(target, AudioMixer::Changed, 0, 0);
    }
    void Run() {
        Performance::Scope startup(L"audio.service.lifetime");
        HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(initialized)) { Publish(false); return; }
        deviceNotifications = MakeNotifications(false);
        if (factory) factory(&devices);
        else CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&devices));
        if (devices) devices->RegisterEndpointNotificationCallback(deviceNotifications.Get());
        bool available = false;
        for (;;) {
            std::map<std::pair<unsigned, bool>, Command> pending;
            std::vector<ComPtr<IAudioSessionControl>> created;
            std::map<unsigned, double> relative;
            bool reset;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [&] { return stop || dirty || !commands.empty() || !adjustments.empty(); });
                // Debounce bursts of slider/device/session events without an idle polling loop.
                wake.wait_for(lock, std::chrono::milliseconds(16), [&] { return stop; });
                if (stop) break;
                reset = rebind; rebind = false; dirty = false;
                pending.swap(commands); relative.swap(adjustments); created.swap(incoming);
            }
            if (reset) {
                Performance::Scope timing(L"audio.bind"); available = Bind(); created.clear(); pending.clear();
                Performance::Mark(available ? L"audio.ready" : L"audio.unavailable");
            }
            for (auto& s : created) Add(s.Get());
            for (const auto& [key, c] : pending) {
                const auto id = key.first;
                HRESULT hr = S_OK;
                if (!id && endpoint) hr = c.setMute ? endpoint->SetMute(c.mute, nullptr) :
                    endpoint->SetMasterVolumeLevelScalar(Audio::Scalar(c.percent), nullptr);
                else for (auto& s : sessions) if (s.id == id) {
                    hr = c.setMute ? s.volume->SetMute(c.mute, nullptr) : s.volume->SetMasterVolume(Audio::Scalar(c.percent), nullptr);
                }
                if (FAILED(hr)) { std::lock_guard lock(mutex); dirty = rebind = true; }
            }
            for (const auto& [id, delta] : relative) {
                float current = 0;
                if (!id && endpoint && SUCCEEDED(endpoint->GetMasterVolumeLevelScalar(&current)))
                    endpoint->SetMasterVolumeLevelScalar(std::clamp(current + static_cast<float>(delta / 100), 0.f, 1.f), nullptr);
                else for (auto& session : sessions) if (session.id == id && SUCCEEDED(session.volume->GetMasterVolume(&current)))
                    session.volume->SetMasterVolume(std::clamp(current + static_cast<float>(delta / 100), 0.f, 1.f), nullptr);
            }
            Publish(available);
        }
        Unbind();
        if (devices) devices->UnregisterEndpointNotificationCallback(deviceNotifications.Get());
        devices.Reset(); notifications.Reset(); deviceNotifications.Reset();
        { std::lock_guard lock(mutex); incoming.clear(); }
        CoUninitialize();
    }
};
AudioMixer::AudioMixer(DeviceFactory factory) : factory_(std::move(factory)) {}
AudioMixer::~AudioMixer() { Close(); }
void AudioMixer::Open(HWND target) {
    if (state_) return;
    Close(); state_ = std::make_shared<State>(); state_->target = target; state_->factory = factory_;
    state_->worker = std::thread([state = state_] { state->Run(); });
}
void AudioMixer::Close() {
    if (!state_) return;
    { std::lock_guard lock(state_->mutex); state_->stop = true; state_->wake.notify_one(); }
    state_->worker.join(); state_.reset();
}
void AudioMixer::Set(unsigned id, int percent, bool mute, bool setMute) {
    if (!state_) return;
    std::lock_guard lock(state_->mutex);
    state_->commands[{id, setMute}] = { id, percent, mute, setMute }; state_->wake.notify_one();
}
Audio::Snapshot AudioMixer::Read() {
    if (!state_) return {};
    std::lock_guard lock(state_->mutex); return state_->snapshot;
}

void AudioMixer::Adjust(unsigned id, double percentagePoints) {
    if (!state_ || !std::isfinite(percentagePoints)) return;
    std::lock_guard lock(state_->mutex);
    state_->adjustments[id] += std::clamp(percentagePoints, -100.0, 100.0);
    state_->wake.notify_one();
}
