#pragma once
#include <windows.h>
#include <objbase.h>
#include <WebView2.h>
#include <wrl/client.h>
#include <functional>
#include <memory>
#include <string>

class WebViewHost {
public:
    ~WebViewHost() { Close(); }
    bool Create(ICoreWebView2Environment* environment, HWND owner, IUnknown* visualTarget,
        const std::wstring& uiDirectory, const std::wstring& role,
        std::function<void()> ready,
        std::function<void(const std::wstring&)> command);
    bool Input(UINT message, WPARAM wp, LPARAM lp);
    LRESULT Accessibility(WPARAM wp, LPARAM lp);
    void Resize();
    void Moved();
    void Post(const std::wstring& json);
    void Displayed(bool visible);
    void Close();
    bool Painted() const { return painted_; }
    bool Ready() const { return ready_; }
private:
    std::shared_ptr<int> lifetime_;
    EventRegistrationToken messageToken_{}, navigationToken_{}, processToken_{}, startingToken_{}, resourceToken_{};
    HWND owner_ = nullptr;
    std::wstring role_;
    bool trackingMouse_ = false;
    bool leftPressed_ = false;
    int displayed_ = -1;
    EventRegistrationToken cursorToken_{};
    Microsoft::WRL::ComPtr<IUnknown> visualTarget_;
    Microsoft::WRL::ComPtr<ICoreWebView2CompositionController> composition_;
    bool ready_ = false, painted_ = false;
    std::function<void()> onReady_;
    std::function<void(const std::wstring&)> onCommand_;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller_;
    Microsoft::WRL::ComPtr<ICoreWebView2> view_;
};
