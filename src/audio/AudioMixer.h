#pragma once
#include "AudioModel.h"
#include <memory>
#include <functional>
#include <mmdeviceapi.h>
class AudioMixer {
public:
    static constexpr UINT Changed = WM_APP + 60;
    using DeviceFactory = std::function<HRESULT(IMMDeviceEnumerator**)>;
    explicit AudioMixer(DeviceFactory factory = {});
    ~AudioMixer();
    void Open(HWND target);
    void Close();
    void Set(unsigned id, int percent, bool mute, bool setMute);
    void Adjust(unsigned id, double percentagePoints);
    Audio::Snapshot Read();
private:
    struct State;
    std::shared_ptr<State> state_;
    DeviceFactory factory_;
};
