#include "WindowEffects.h"
#include "GaussianBlurEffect.h"
#include "../windows/IslandLayout.h"
#include "../web/Diagnostics.h"
#include <dwmapi.h>
#include <algorithm>
#include <DispatcherQueue.h>
#include <windows.ui.composition.interop.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Composition.Desktop.h>
#include <winrt/Windows.UI.ViewManagement.h>
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "CoreMessaging.lib")
#pragma comment(lib, "windowsapp.lib")

using namespace winrt;
using namespace Windows::UI::Composition;

struct WindowEffects::State {
    Windows::System::DispatcherQueueController queue{ nullptr };
    Compositor compositor{ nullptr };
    Desktop::DesktopWindowTarget target{ nullptr };
    ContainerVisual root{ nullptr };
    ContainerVisual content{ nullptr };
    ContainerVisual web{ nullptr };
    SpriteVisual glass{ nullptr };
    CompositionRoundedRectangleGeometry geometry{ nullptr };
    ShapeVisual outline{ nullptr };
    CompositionRoundedRectangleGeometry strokeGeometry{ nullptr };
    CompositionEffectBrush blur{ nullptr };
    int radius = 0;
};

WindowEffects::WindowEffects() = default;
WindowEffects::~WindowEffects() { Close(); }

bool WindowEffects::Initialize(HWND hwnd, int radiusDip, float borderOpacity) {
    try {
        state_ = std::make_unique<State>();
        state_->radius = radiusDip;
        if (!Windows::System::DispatcherQueue::GetForCurrentThread()) {
            DispatcherQueueOptions options{ sizeof(DispatcherQueueOptions), DQTYPE_THREAD_CURRENT, DQTAT_COM_STA };
            check_hresult(CreateDispatcherQueueController(options,
                reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(put_abi(state_->queue))));
        }
        // Windows 11 build 22000+ documented Win32 host-backdrop opt-in.
        const BOOL enabled = TRUE;
        check_hresult(DwmSetWindowAttribute(hwnd, DWMWA_USE_HOSTBACKDROPBRUSH, &enabled, sizeof(enabled)));
        Refresh(hwnd);
        state_->compositor = Compositor();
        auto interop = state_->compositor.as<ABI::Windows::UI::Composition::Desktop::ICompositorDesktopInterop>();
        check_hresult(interop->CreateDesktopWindowTarget(hwnd, FALSE,
            reinterpret_cast<ABI::Windows::UI::Composition::Desktop::IDesktopWindowTarget**>(put_abi(state_->target))));
        state_->glass = state_->compositor.CreateSpriteVisual();
        auto source = state_->compositor.CreateHostBackdropBrush();
        const bool probe = GetEnvironmentVariableW(L"GLASSSPLIT_BACKDROP_SOURCE_ONLY", nullptr, 0) != 0;
        if (probe) {
            state_->glass.Brush(source);
        } else {
            auto effect = make<GaussianBlurEffect>(CompositionEffectSourceParameter(L"desktop"));
            auto factory = state_->compositor.CreateEffectFactory(effect);
            Diagnostics::Check(factory.ExtendedError(), L"Gaussian effect factory ExtendedError");
            check_hresult(factory.ExtendedError());
            state_->blur = factory.CreateBrush();
            state_->blur.SetSourceParameter(L"desktop", source);
            state_->glass.Brush(state_->blur);
        }
        state_->geometry = state_->compositor.CreateRoundedRectangleGeometry();
        state_->strokeGeometry = state_->compositor.CreateRoundedRectangleGeometry();
        auto stroke = state_->compositor.CreateSpriteShape(state_->strokeGeometry);
        stroke.StrokeBrush(state_->compositor.CreateColorBrush(
            Windows::UI::Color{ static_cast<uint8_t>(std::clamp(borderOpacity, 0.0f, 1.0f) * 255 + 0.5f), 255, 255, 255 }));
        stroke.StrokeThickness(1.0f); // One physical pixel at every DPI.
        state_->outline = state_->compositor.CreateShapeVisual();
        state_->outline.Shapes().Append(stroke);
        state_->root = state_->compositor.CreateContainerVisual();
        state_->web = state_->compositor.CreateContainerVisual();
        state_->content = state_->compositor.CreateContainerVisual();
        state_->content.Clip(state_->compositor.CreateGeometricClip(state_->geometry));
        state_->root.Children().InsertAtBottom(state_->content);
        state_->content.Children().InsertAtBottom(state_->glass);
        state_->content.Children().InsertAtTop(state_->web);
        state_->root.Children().InsertAtTop(state_->outline);
        state_->target.Root(state_->root);
        Resize(hwnd);
        Diagnostics::Log(probe ? L"Backdrop SOURCE-ONLY probe: Gaussian bypassed; inspect live desktop detail"
            : L"Backdrop graph initialized: host source -> Gaussian sigma=18px, hard border -> rounded clip; no color operations");
        Diagnostics::Log(L"Visual order: root -> rounded content (Gaussian + transparent composition WebView) -> native 1px outline; no child HWND surface");
        Diagnostics::Log(L"Rendering UNVERIFIED: API success cannot prove desktop sampling, WebView alpha blending, or visible blur; native high-contrast test required");
        return true;
    } catch (const hresult_error& error) {
        Diagnostics::Check(error.code(), L"Backdrop/effect initialization failed; no fallback");
        Diagnostics::Log(std::wstring(L"Composition error: ") + error.message().c_str());
        Close();
        return false;
    }
}

void WindowEffects::Refresh(HWND hwnd) {
    const BOOL enabled = TRUE;
    Diagnostics::Check(DwmSetWindowAttribute(hwnd, DWMWA_USE_HOSTBACKDROPBRUSH,
        &enabled, sizeof(enabled)), L"DWM host backdrop opt-in");
    BOOL composition = FALSE, optedIn = FALSE;
    Diagnostics::Check(DwmIsCompositionEnabled(&composition), L"DWM composition query");
    Diagnostics::Check(DwmGetWindowAttribute(hwnd, DWMWA_USE_HOSTBACKDROPBRUSH,
        &optedIn, sizeof(optedIn)), L"DWM host backdrop readback");
    Diagnostics::Log(L"Backdrop prerequisites: DWM=" + std::to_wstring(composition) +
        L" hostOptIn=" + std::to_wstring(optedIn) + L" remoteSession=" +
        std::to_wstring(GetSystemMetrics(SM_REMOTESESSION)));
    try {
        Windows::UI::ViewManagement::UISettings settings;
        Diagnostics::Log(L"System AdvancedEffectsEnabled=" + std::to_wstring(settings.AdvancedEffectsEnabled()));
    } catch (const hresult_error& error) {
        Diagnostics::Check(error.code(), L"Advanced effects policy query unavailable");
    }
    const int none = 1; // DWMSBT_NONE: never silently fall back to tinted acrylic.
    Diagnostics::Check(DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &none, sizeof(none)),
        L"Disable system material (optional on Windows 11 22000)");
    const COLORREF noBorder = DWMWA_COLOR_NONE;
    DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &noBorder, sizeof(noBorder));
    const int noCorner = DWMWCP_DONOTROUND; // Composition owns the rounded edge
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &noCorner, sizeof(noCorner));
    MARGINS margins{ -1, -1, -1, -1 };
    Diagnostics::Check(DwmExtendFrameIntoClientArea(hwnd, &margins), L"DWM transparent frame");
}

void WindowEffects::Resize(HWND hwnd) {
    if (!state_ || !state_->glass) return;
    RECT r{};
    GetClientRect(hwnd, &r);
    const float width = static_cast<float>(r.right);
    const float height = static_cast<float>(r.bottom);
    const auto contour = CalculateContour(state_->radius, GetDpiForWindow(hwnd), r.right, r.bottom);
    const float radius = contour.radius;
    state_->root.Size({ width, height });
    state_->content.Size({ width, height });
    state_->web.Size({ width, height });
    state_->glass.Size({ width, height });
    state_->geometry.Size({ width, height });
    state_->geometry.CornerRadius({ radius, radius });
    state_->outline.Size({ width, height });
    // Center the stroke half a pixel inside the shared outer contour.
    state_->strokeGeometry.Offset({ contour.strokeInset, contour.strokeInset });
    state_->strokeGeometry.Size({ std::max(0.0f, width - 1), std::max(0.0f, height - 1) });
    const float innerRadius = contour.strokeRadius;
    state_->strokeGeometry.CornerRadius({ innerRadius, innerRadius });
}

void WindowEffects::Close() {
    if (state_ && state_->target) state_->target.Root(nullptr);
    state_.reset();
}

void WindowEffects::Fade(bool visible, int milliseconds) {
    if (!state_ || !state_->root) return;
    if (!milliseconds) { state_->root.StopAnimation(L"Opacity"); state_->root.Opacity(visible ? 1.0f : 0.0f); return; }
    auto animation = state_->compositor.CreateScalarKeyFrameAnimation();
    auto easing = state_->compositor.CreateCubicBezierEasingFunction({1.0f / 3, 1}, {2.0f / 3, 1});
    animation.InsertKeyFrame(1.0f, visible ? 1.0f : 0.0f, easing);
    animation.Duration(std::chrono::milliseconds(milliseconds));
    state_->root.StartAnimation(L"Opacity", animation);
}

IUnknown* WindowEffects::WebRoot() const {
    return state_ ? reinterpret_cast<IUnknown*>(get_abi(state_->web)) : nullptr;
}
