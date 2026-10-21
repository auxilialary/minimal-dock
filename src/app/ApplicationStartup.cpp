#include "Application.h"
#include "StartupPolicy.h"
#include "../web/Diagnostics.h"
#include <wrl/event.h>

void Application::RequestEnvironment() {
    if (stopping_) return;
    ++environmentAttempts_;
    const std::weak_ptr<int> lifetime = lifetime_;
    Performance::Mark(L"environment.request");
    const auto hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, webData_.c_str(), nullptr,
        Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this, lifetime](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (!lifetime.expired()) OnEnvironment(result, env);
                return S_OK;
            }).Get());
    if (FAILED(hr)) RetryEnvironment();
}
void Application::RetryEnvironment() {
    Diagnostics::Log(L"WebView environment unavailable; bounded asynchronous retry");
    const auto delay = StartupPolicy::RetryDelay(environmentAttempts_);
    if (!delay || !SetTimer(systemWindow_.Handle(), 0x448, delay, nullptr)) PostQuitMessage(1);
}
