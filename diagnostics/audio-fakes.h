#pragma once
#include "../src/audio/AudioNotifications.h"
#include <wrl.h>
#include <audioclient.h>
#include <mutex>
#include <vector>
#include <atomic>
#include <string>
#include <cstring>
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Make;
template<class... I> using Fake = Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, I...>;
inline std::recursive_mutex audioLock;
inline HRESULT CopyText(const std::wstring& text, LPWSTR* value) {
    *value = static_cast<LPWSTR>(CoTaskMemAlloc((text.size()+1)*sizeof(wchar_t)));
    if (!*value) return E_OUTOFMEMORY;
    memcpy(*value, text.c_str(), (text.size()+1)*sizeof(wchar_t)); return S_OK;
}
class FakeSession final : public Fake<Microsoft::WRL::ChainInterfaces<IAudioSessionControl2, IAudioSessionControl>, ISimpleAudioVolume> {
public:
    std::wstring identity = L"session-A", name = L"Actual helper session";
    DWORD pid = 0;
    HRESULT processResult = S_OK;
    float level = .42f;
    BOOL mute = FALSE;
    bool systemSounds = false;
    AudioSessionState state = AudioSessionStateActive;
    std::atomic<int> writes{0};
    ComPtr<IAudioSessionEvents> events;
    HRESULT STDMETHODCALLTYPE GetState(AudioSessionState* out) override { std::lock_guard l(audioLock); *out=state; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetDisplayName(LPWSTR* out) override { std::lock_guard l(audioLock); return CopyText(name,out); }
    HRESULT STDMETHODCALLTYPE SetDisplayName(LPCWSTR, LPCGUID) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetIconPath(LPWSTR* out) override { return CopyText(L"",out); }
    HRESULT STDMETHODCALLTYPE SetIconPath(LPCWSTR,LPCGUID) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetGroupingParam(GUID* out) override { *out=GUID_NULL; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetGroupingParam(LPCGUID,LPCGUID) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE RegisterAudioSessionNotification(IAudioSessionEvents* e) override { std::lock_guard l(audioLock); events=e; return S_OK; }
    HRESULT STDMETHODCALLTYPE UnregisterAudioSessionNotification(IAudioSessionEvents*) override { std::lock_guard l(audioLock); events.Reset(); return S_OK; }
    HRESULT STDMETHODCALLTYPE GetSessionIdentifier(LPWSTR* out) override { return CopyText(L"shared executable",out); }
    HRESULT STDMETHODCALLTYPE GetSessionInstanceIdentifier(LPWSTR* out) override { return CopyText(identity,out); }
    HRESULT STDMETHODCALLTYPE GetProcessId(DWORD* out) override { *out=pid; return processResult; }
    HRESULT STDMETHODCALLTYPE IsSystemSoundsSession() override { return systemSounds ? S_OK : S_FALSE; }
    HRESULT STDMETHODCALLTYPE SetDuckingPreference(BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetMasterVolume(float v,LPCGUID) override {
        std::lock_guard l(audioLock); ++writes; level=v; Notify(); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetMasterVolume(float* out) override { std::lock_guard l(audioLock); *out=level; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetMute(BOOL v,LPCGUID) override { std::lock_guard l(audioLock); ++writes; mute=v; Notify(); return S_OK; }
    HRESULT STDMETHODCALLTYPE GetMute(BOOL* out) override { std::lock_guard l(audioLock); *out=mute; return S_OK; }
    void Notify() { if(events) events->OnSimpleVolumeChanged(level,mute,nullptr); }
};
class FakeEndpoint final : public Fake<IAudioEndpointVolume> {
public:
    float level = .7f; BOOL mute = FALSE;
    std::atomic<int> writes{0};
    ComPtr<IAudioEndpointVolumeCallback> events;
    HRESULT STDMETHODCALLTYPE RegisterControlChangeNotify(IAudioEndpointVolumeCallback* e) override { std::lock_guard l(audioLock); events=e; return S_OK; }
    HRESULT STDMETHODCALLTYPE UnregisterControlChangeNotify(IAudioEndpointVolumeCallback*) override { std::lock_guard l(audioLock); events.Reset(); return S_OK; }
    HRESULT STDMETHODCALLTYPE GetChannelCount(UINT* n) override { *n=2; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetMasterVolumeLevel(float,LPCGUID) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetMasterVolumeLevelScalar(float v,LPCGUID) override { std::lock_guard l(audioLock); ++writes; level=v; if(events) events->OnNotify(nullptr); return S_OK; }
    HRESULT STDMETHODCALLTYPE GetMasterVolumeLevel(float*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetMasterVolumeLevelScalar(float* v) override { std::lock_guard l(audioLock); *v=level; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetChannelVolumeLevel(UINT,float,LPCGUID) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetChannelVolumeLevelScalar(UINT,float,LPCGUID) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetChannelVolumeLevel(UINT,float*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetChannelVolumeLevelScalar(UINT,float*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetMute(BOOL v,LPCGUID) override { std::lock_guard l(audioLock); ++writes; mute=v; if(events) events->OnNotify(nullptr); return S_OK; }
    HRESULT STDMETHODCALLTYPE GetMute(BOOL* v) override { std::lock_guard l(audioLock); *v=mute; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetVolumeStepInfo(UINT*,UINT*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE VolumeStepUp(LPCGUID) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE VolumeStepDown(LPCGUID) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE QueryHardwareSupport(DWORD*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetVolumeRange(float*,float*,float*) override { return E_NOTIMPL; }
};
class FakeSessions final : public Fake<IAudioSessionEnumerator> {
public:
    std::vector<ComPtr<FakeSession>> items;
    HRESULT STDMETHODCALLTYPE GetCount(int* out) override { *out=static_cast<int>(items.size()); return S_OK; }
    HRESULT STDMETHODCALLTYPE GetSession(int i,IAudioSessionControl** out) override { return items.at(i).CopyTo(out); }
};
class FakeManager final : public Fake<IAudioSessionManager2> {
public:
    ComPtr<FakeSessions> list = Make<FakeSessions>();
    ComPtr<IAudioSessionNotification> events;
    HRESULT STDMETHODCALLTYPE GetAudioSessionControl(LPCGUID,DWORD,IAudioSessionControl**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetSimpleAudioVolume(LPCGUID,DWORD,ISimpleAudioVolume**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetSessionEnumerator(IAudioSessionEnumerator** out) override { std::lock_guard l(audioLock); return list.CopyTo(out); }
    HRESULT STDMETHODCALLTYPE RegisterSessionNotification(IAudioSessionNotification* e) override { std::lock_guard l(audioLock); events=e; return S_OK; }
    HRESULT STDMETHODCALLTYPE UnregisterSessionNotification(IAudioSessionNotification*) override { std::lock_guard l(audioLock); events.Reset(); return S_OK; }
    HRESULT STDMETHODCALLTYPE RegisterDuckNotification(LPCWSTR,IAudioVolumeDuckNotification*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE UnregisterDuckNotification(IAudioVolumeDuckNotification*) override { return E_NOTIMPL; }
};
class FakeDevice final : public Fake<IMMDevice> {
public:
    ComPtr<FakeEndpoint> endpoint = Make<FakeEndpoint>();
    ComPtr<FakeManager> manager = Make<FakeManager>();
    HRESULT STDMETHODCALLTYPE Activate(REFIID id,DWORD,PROPVARIANT*,void** out) override {
        if(id==__uuidof(IAudioEndpointVolume)) return endpoint->QueryInterface(id,out);
        return manager->QueryInterface(id,out);
    }
    HRESULT STDMETHODCALLTYPE OpenPropertyStore(DWORD,IPropertyStore**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetId(LPWSTR* out) override { return CopyText(L"mock-device",out); }
    HRESULT STDMETHODCALLTYPE GetState(DWORD* out) override { *out=DEVICE_STATE_ACTIVE; return S_OK; }
};
class FakeDevices final : public Fake<IMMDeviceEnumerator> {
public:
    ComPtr<FakeDevice> current = Make<FakeDevice>();
    ComPtr<IMMNotificationClient> events;
    HRESULT STDMETHODCALLTYPE EnumAudioEndpoints(EDataFlow,DWORD,IMMDeviceCollection**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetDefaultAudioEndpoint(EDataFlow flow,ERole role,IMMDevice** out) override {
        std::lock_guard l(audioLock);
        if(flow!=eRender || role!=eMultimedia || !current) return E_NOTFOUND;
        return current.CopyTo(out);
    }
    HRESULT STDMETHODCALLTYPE GetDevice(LPCWSTR,IMMDevice**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE RegisterEndpointNotificationCallback(IMMNotificationClient* e) override { std::lock_guard l(audioLock); events=e; return S_OK; }
    HRESULT STDMETHODCALLTYPE UnregisterEndpointNotificationCallback(IMMNotificationClient*) override { std::lock_guard l(audioLock); events.Reset(); return S_OK; }
};

