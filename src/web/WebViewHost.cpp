#include "WebViewHost.h"
#include <wrl/event.h>
#include "Diagnostics.h"
#include <filesystem>
#include "MouseInput.h"
#include <UIAutomation.h>
#pragma comment(lib, "uiautomationcore.lib")

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

bool WebViewHost::Create(ICoreWebView2Environment* environment,
    HWND owner, IUnknown* visualTarget, const std::wstring& uiDirectory, const std::wstring& role,
    std::function<void()> ready,
    std::function<void(const std::wstring&)> command) {
    Close();
    if (Performance::enabled) Performance::Mark(L"controller.request:" + role);
    Diagnostics::Log(L"Create role=" + role + L" assets=" + uiDirectory);
    for (const auto* asset : { L"index.html", L"theme.css", L"motion.css", L"app.js", L"dock.js", L"system.js", L"magnification.js", L"magnification-math.js" }) {
        if (!std::filesystem::is_regular_file(std::filesystem::path(uiDirectory) / asset)) {
            Diagnostics::Log(std::wstring(L"Missing UI asset: ") + asset);
            return false;
        }
    }
    if (!environment || !visualTarget || !IsWindow(owner)) return false;
    lifetime_ = std::make_shared<int>(0);
    std::weak_ptr<int> lifetime = lifetime_;
    owner_ = owner; role_ = role;
    visualTarget_ = visualTarget;
    onReady_ = std::move(ready);
    onCommand_ = std::move(command);
    ready_ = false;
    painted_ = false;

    ComPtr<ICoreWebView2Environment3> compositionEnvironment;
    if (!Diagnostics::Check(environment->QueryInterface(IID_PPV_ARGS(&compositionEnvironment)),
        L"Composition environment required")) return false;
    HRESULT hr = compositionEnvironment->CreateCoreWebView2CompositionController(owner_,
        Callback<ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler>(
            [this, lifetime, uiDirectory, role](HRESULT result,
                ICoreWebView2CompositionController* controller) -> HRESULT {
                if (lifetime.expired()) {
                    ComPtr<ICoreWebView2Controller> abandoned;
                    if (controller && SUCCEEDED(controller->QueryInterface(IID_PPV_ARGS(&abandoned)))) abandoned->Close();
                    return S_OK;
                }
                auto fail = [](HRESULT error, const wchar_t* operation) -> HRESULT {
                    Diagnostics::Check(error, operation); PostQuitMessage(1); return S_OK;
                };
                if (FAILED(result) || !controller) {
                    Diagnostics::Check(FAILED(result) ? result : E_POINTER, L"Create controller");
                    MessageBoxW(nullptr, L"WebView2 controller failed.",
                        L"Dock", MB_ICONERROR);
                    PostQuitMessage(1);
                    return S_OK;
                }

                composition_ = controller;
                if (Performance::enabled) Performance::Mark(L"controller.created:" + role);
                if (HRESULT status = composition_.As(&controller_); FAILED(status)) return fail(status, L"Composition base controller");
                if (HRESULT status = composition_->add_CursorChanged(
                    Callback<ICoreWebView2CursorChangedEventHandler>(
                        [this, lifetime](ICoreWebView2CompositionController*, IUnknown*) -> HRESULT {
                            if (!lifetime.expired()) {
                                HCURSOR cursor = nullptr;
                                if (SUCCEEDED(composition_->get_Cursor(&cursor))) SetCursor(cursor);
                            }
                            return S_OK;
                        }).Get(), &cursorToken_); FAILED(status)) return fail(status, L"WebView cursor");
                if (!Diagnostics::Check(controller_->get_CoreWebView2(&view_), L"get_CoreWebView2")) return fail(E_FAIL, L"Core view unavailable");
                if (!view_) {
                    PostQuitMessage(1);
                    return S_OK;
                }

                ComPtr<ICoreWebView2Controller2> transparent;
                if (FAILED(controller_.As(&transparent))) return fail(E_NOINTERFACE, L"Transparent controller required");
                {
                    COREWEBVIEW2_COLOR color{ 0, 0, 0, 0 };
                    if (!Diagnostics::Check(transparent->put_DefaultBackgroundColor(color), L"Transparent background")) return fail(E_FAIL, L"Transparency required");
                    COREWEBVIEW2_COLOR actual{};
                    if (!Diagnostics::Check(transparent->get_DefaultBackgroundColor(&actual), L"Read WebView alpha")) return fail(E_FAIL, L"Transparency readback required");
                    Diagnostics::Log(L"WebView background alpha=" + std::to_wstring(actual.A) +
                        L" (configured value; native blending still requires visual verification)");
                    if (actual.A != 0) return fail(E_FAIL, L"WebView must remain transparent");
                    // Attach only after alpha is configured: no opaque first surface.
    if (HRESULT status = composition_->put_RootVisualTarget(visualTarget_.Get()); FAILED(status)) return fail(status, L"Attach WebView above Gaussian");
                    Diagnostics::Log(L"Composition WebView attached above backdrop; no windowed controller");
                    const bool backdropOnly = GetEnvironmentVariableW(L"GLASSSPLIT_BACKDROP_ONLY", nullptr, 0) != 0;
                    if (!Diagnostics::Check(controller_->put_IsVisible(!backdropOnly), L"Controller visibility")) return fail(E_FAIL, L"Visibility required");
                    if (backdropOnly) Diagnostics::Log(L"Backdrop isolation probe: WebView hidden; native source/effect remains visible");
                }

                ComPtr<ICoreWebView2Settings> settings;
                if (SUCCEEDED(view_->get_Settings(&settings))) {
                    Diagnostics::Check(settings->put_AreDefaultContextMenusEnabled(FALSE), L"Disable context menus");
                    Diagnostics::Check(settings->put_AreDevToolsEnabled(
                        GetEnvironmentVariableW(L"GLASSSPLIT_DIAGNOSTICS", nullptr, 0) ? TRUE : FALSE), L"Configure devtools");
                    Diagnostics::Check(settings->put_IsStatusBarEnabled(FALSE), L"Disable status bar");
                    if (!Diagnostics::Check(settings->put_IsWebMessageEnabled(TRUE), L"Enable web messages")) return fail(E_FAIL, L"Web messaging required");
                    if (!Diagnostics::Check(settings->put_IsScriptEnabled(TRUE), L"Enable JavaScript")) return fail(E_FAIL, L"JavaScript required");
                } else return fail(E_FAIL, L"WebView settings unavailable");

                ComPtr<ICoreWebView2_3> view3;
                if (HRESULT status = view_.As(&view3); FAILED(status)) return fail(status, L"Virtual host interface");
                {
                    if (HRESULT status = view3->SetVirtualHostNameToFolderMapping(
                        L"dock.local", uiDirectory.c_str(),
                        COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS); FAILED(status)) return fail(status, L"Map dock.local to UI directory");
                }


                if (HRESULT status = view_->add_WebMessageReceived(
                    Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                        [this, lifetime](ICoreWebView2*,
                            ICoreWebView2WebMessageReceivedEventArgs* args)
                            -> HRESULT {
                            if (lifetime.expired()) return S_OK;
                            LPWSTR raw = nullptr;
                            if (SUCCEEDED(args->TryGetWebMessageAsString(&raw))
                                && raw) {
                                std::wstring message(raw);
                                if (message.rfind(L"ui:error:", 0) == 0) {
                                    Diagnostics::Log(role_ + L":" + message); PostQuitMessage(1);
                                } else if (Performance::enabled && message.rfind(L"perf:", 0) == 0) {
                                    Performance::Mark(role_ + L":" + message);
                                } else if (message == L"ui:boot" || message == L"ui:painted") {
                                    if (message == L"ui:boot") ready_ = true;
                                    if (message == L"ui:painted") painted_ = true;
                                    Diagnostics::Log(role_ + L":" + message);
                                    if (Performance::enabled) Performance::Mark(role_ + L":" + message);
                                    if (onReady_) onReady_();
                                } else if (ready_) {
                                    auto callback = onCommand_;
                                    if (callback) callback(raw);
                                }
                                CoTaskMemFree(raw);
                            }
                            return S_OK;
                        }).Get(), &messageToken_); FAILED(status)) return fail(status, L"Subscribe WebMessageReceived (HRESULT)");

                if (HRESULT status = view_->add_NavigationCompleted(
                    Callback<ICoreWebView2NavigationCompletedEventHandler>(
                        [this, lifetime](ICoreWebView2*,
                            ICoreWebView2NavigationCompletedEventArgs* args)
                            -> HRESULT {
                            if (lifetime.expired()) return S_OK;
                            BOOL succeeded = FALSE;
                            args->get_IsSuccess(&succeeded);
                            if (succeeded) {
                                if (Performance::enabled) Performance::Mark(L"navigation.completed:" + role_);
                                Diagnostics::Log(L"Navigation completed; awaiting rendered UI handshake");
                            } else {
                                COREWEBVIEW2_WEB_ERROR_STATUS error{};
                                args->get_WebErrorStatus(&error);
                                Diagnostics::Log(L"Navigation failed, WebErrorStatus=" + std::to_wstring(error));
                                MessageBoxW(nullptr, L"Dock UI did not load.",
                                    L"Dock", MB_ICONERROR);
                                PostQuitMessage(1);
                            }
                            return S_OK;
                        }).Get(), &navigationToken_); FAILED(status)) return fail(status, L"Subscribe NavigationCompleted (HRESULT)");

                if (HRESULT status = view_->add_ProcessFailed(
                    Callback<ICoreWebView2ProcessFailedEventHandler>(
                        [lifetime](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs*) -> HRESULT {
                            if (!lifetime.expired()) { Diagnostics::Log(L"WebView process failed"); PostQuitMessage(1); }
                            return S_OK;
                        }).Get(), &processToken_); FAILED(status)) return fail(status, L"Subscribe ProcessFailed (HRESULT)");
                if (HRESULT status = view_->add_NavigationStarting(
                    Callback<ICoreWebView2NavigationStartingEventHandler>(
                        [lifetime](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                            if (lifetime.expired()) return S_OK;
                            LPWSTR uri = nullptr;
                            if (SUCCEEDED(args->get_Uri(&uri)) && uri) {
                                Diagnostics::Log(std::wstring(L"Navigation: ") + uri);
                                if (std::wstring(uri).rfind(L"https://dock.local/", 0) != 0)
                                    args->put_Cancel(TRUE);
                                CoTaskMemFree(uri);
                            }
                            return S_OK;
                        }).Get(), &startingToken_); FAILED(status)) return fail(status, L"Subscribe NavigationStarting (HRESULT)");
                if (Diagnostics::Enabled()) {
                ComPtr<ICoreWebView2_2> resources;
                if (HRESULT status = view_.As(&resources); FAILED(status)) return fail(status, L"Resource diagnostics interface");
                if (HRESULT status = resources->add_WebResourceResponseReceived(
                    Callback<ICoreWebView2WebResourceResponseReceivedEventHandler>(
                        [this, lifetime](ICoreWebView2*, ICoreWebView2WebResourceResponseReceivedEventArgs* args) -> HRESULT {
                            if (lifetime.expired()) return S_OK;
                            ComPtr<ICoreWebView2WebResourceRequest> request;
                            ComPtr<ICoreWebView2WebResourceResponseView> response;
                            if (FAILED(args->get_Request(&request)) || FAILED(args->get_Response(&response))) return S_OK;
                            LPWSTR uri = nullptr;
                            int status = 0;
                            response->get_StatusCode(&status);
                            if (SUCCEEDED(request->get_Uri(&uri)) && uri) {
                                // Data URI icons can contain large payloads; log only mapped UI requests.
                                if (std::wstring(uri).rfind(L"https://dock.local/", 0) == 0)
                                    Diagnostics::Log(role_ + L":Resource " + std::to_wstring(status) + L" " + uri);
                                CoTaskMemFree(uri);
                            }
                            return S_OK;
                        }).Get(), &resourceToken_); FAILED(status)) return fail(status, L"Resource response diagnostics");
                }
                Resize();
                if (HRESULT status = view_->Navigate((L"https://dock.local/index.html?role="
                    + role + (Performance::enabled ? L"&perf=1" : L"")).c_str()); FAILED(status)) return fail(status, L"Navigate dock.local index");
                return S_OK;
            }).Get());

    return Diagnostics::Check(hr, L"CreateCoreWebView2CompositionController");
}

void WebViewHost::Resize() {
    if (!controller_ || !owner_) return;
    RECT area{};
    GetClientRect(owner_, &area);
    Diagnostics::Check(controller_->put_Bounds(area), L"Controller bounds");
}

void WebViewHost::Moved() {
    if (controller_) controller_->NotifyParentWindowPositionChanged();
}

void WebViewHost::Post(const std::wstring& json) {
    if (ready_ && view_) Diagnostics::Check(view_->PostWebMessageAsJson(json.c_str()), L"Post UI data");
}
void WebViewHost::Displayed(bool visible) {
    if (!ready_ || displayed_ == static_cast<int>(visible)) return;
    displayed_ = visible;
    if (!visible) {
        Input(WM_MOUSELEAVE, 0, 0);
        if (GetCapture() == owner_) ReleaseCapture();
    }
    if (role_ != L"media") return;
    Post(visible ? L"{\"type\":\"media:visibility\",\"visible\":true}" :
        L"{\"type\":\"media:visibility\",\"visible\":false}");
}

void WebViewHost::Close() {
    if (controller_ && Performance::enabled) Performance::Mark(L"controller.closed:" + role_);
    lifetime_.reset();
    ready_ = false;
    painted_ = false;
    displayed_ = -1;
    onReady_ = {};
    onCommand_ = {};
    if (view_) {
        ComPtr<ICoreWebView2_2> resources;
        if (SUCCEEDED(view_.As(&resources))) resources->remove_WebResourceResponseReceived(resourceToken_);
        view_->remove_WebMessageReceived(messageToken_);
        view_->remove_NavigationCompleted(navigationToken_);
        view_->remove_ProcessFailed(processToken_);
        view_->remove_NavigationStarting(startingToken_);
    }
    if (owner_ && GetCapture() == owner_) ReleaseCapture();
    if (composition_) {
        composition_->remove_CursorChanged(cursorToken_);
        composition_->put_RootVisualTarget(nullptr);
    }
    trackingMouse_ = false;
    owner_ = nullptr;
    if (controller_) controller_->Close();
    view_.Reset();
    controller_.Reset();
    composition_.Reset();
    visualTarget_.Reset();
}

// Windowless WebView input is forwarded only on demand: no polling or render loop.
LRESULT WebViewHost::Accessibility(WPARAM wp, LPARAM lp) {
    if (static_cast<LONG>(lp) != UiaRootObjectId || !composition_) return 0;
    ComPtr<ICoreWebView2CompositionController2> accessible;
    ComPtr<IUnknown> unknown;
    ComPtr<IRawElementProviderSimple> provider;
    if (FAILED(composition_.As(&accessible)) || FAILED(accessible->get_AutomationProvider(&unknown)) ||
        !unknown || FAILED(unknown.As(&provider))) return 0;
    return UiaReturnRawElementProvider(owner_, wp, lp, provider.Get());
}

bool WebViewHost::Input(UINT message, WPARAM wp, LPARAM lp) {
    if (!composition_) return false;
    if (message == WM_CAPTURECHANGED || message == WM_CANCELMODE) {
        if (role_ == L"apps" && leftPressed_) Post(L"{\"type\":\"native:capture-lost\"}");
        leftPressed_ = false;
        return false;
    }
    if (message == WM_SETCURSOR && LOWORD(lp) == HTCLIENT) {
        HCURSOR cursor = nullptr;
        if (SUCCEEDED(composition_->get_Cursor(&cursor))) SetCursor(cursor);
        return true;
    }
    if (message == WM_SETFOCUS) {
        controller_->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
        return false;
    }
    if (message < WM_MOUSEFIRST || message > WM_MOUSELAST) {
        if (message != WM_MOUSELEAVE) return false;
    }
    if (message == WM_LBUTTONDOWN) leftPressed_ = true;
    if (message == WM_LBUTTONUP) leftPressed_ = false;
    POINT point = MouseInput::Position(owner_, message, lp);
    const UINT32 data = MouseInput::Data(message, wp);
    // Preserve the Win32 120-units-per-notch contract independently of OS line settings.
    if (message == WM_MOUSEWHEEL && (role_ == L"system" || role_ == L"popup")) {
        const double scale = GetDpiForWindow(owner_) / 96.0;
        Post(L"{\"type\":\"native:wheel\",\"delta\":" + std::to_wstring(static_cast<short>(HIWORD(wp))) +
            L",\"x\":" + std::to_wstring(point.x / scale) + L",\"y\":" + std::to_wstring(point.y / scale) + L"}");
        return true;
    }
    if (message == WM_MOUSEMOVE && !trackingMouse_) {
        TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE, owner_, 0 };
        trackingMouse_ = TrackMouseEvent(&track) != FALSE;
    }
    if (message == WM_MOUSELEAVE) { trackingMouse_ = false; point = {}; }
    if (message == WM_LBUTTONDOWN || message == WM_MBUTTONDOWN ||
        message == WM_RBUTTONDOWN || message == WM_XBUTTONDOWN) SetCapture(owner_);
    // These documented enum values intentionally match the Win32 mouse messages.
    const auto keys = static_cast<COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS>(
        message == WM_MOUSELEAVE ? 0 : LOWORD(wp));
    Diagnostics::Check(composition_->SendMouseInput(
        static_cast<COREWEBVIEW2_MOUSE_EVENT_KIND>(message), keys, data, point), L"WebView mouse input");
    if ((message == WM_LBUTTONUP || message == WM_MBUTTONUP ||
        message == WM_RBUTTONUP || message == WM_XBUTTONUP) &&
        !(LOWORD(wp) & (MK_LBUTTON | MK_MBUTTON | MK_RBUTTON | MK_XBUTTON1 | MK_XBUTTON2)) &&
        GetCapture() == owner_) ReleaseCapture();
    return true;
}
