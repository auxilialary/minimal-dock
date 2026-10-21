#pragma once
#include <windows.h>
#include <unknwn.h>
#include <memory>

// One compositor target per island or shared popup HWND.
class WindowEffects {
public:
    WindowEffects();
    ~WindowEffects();
    bool Initialize(HWND hwnd, int radiusDip, float borderOpacity);
    void Resize(HWND hwnd);
    void Refresh(HWND hwnd);
    void Close();
    void Fade(bool visible, int milliseconds);
    IUnknown* WebRoot() const; // Borrowed; owned until Close().
private:
    struct State;
    std::unique_ptr<State> state_;
};

