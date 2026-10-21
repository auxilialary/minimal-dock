#pragma once
#include <d2d1effects.h>
#include <windows.graphics.effects.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Effects.h>

// SDK-only effect description, following Microsoft's HelloComposition interop
// sample. The compositor executes the D2D Gaussian shader; no D2D device or
// Win2D runtime is needed merely to describe this single-node graph.
struct GaussianBlurEffect : winrt::implements<GaussianBlurEffect,
    winrt::Windows::Graphics::Effects::IGraphicsEffect,
    winrt::Windows::Graphics::Effects::IGraphicsEffectSource,
    ABI::Windows::Graphics::Effects::IGraphicsEffectD2D1Interop> {
    using Source = winrt::Windows::Graphics::Effects::IGraphicsEffectSource;
    using Mapping = ABI::Windows::Graphics::Effects::GRAPHICS_EFFECT_PROPERTY_MAPPING;
    explicit GaussianBlurEffect(Source source) : source_(source) {}
    winrt::hstring Name() const { return L"GaussianBlur"; }
    void Name(winrt::hstring const&) {}
    HRESULT __stdcall GetEffectId(GUID* id) noexcept override {
        if (!id) return E_POINTER;
        *id = CLSID_D2D1GaussianBlur;
        return S_OK;
    }
    HRESULT __stdcall GetNamedPropertyMapping(LPCWSTR, UINT*, Mapping*) noexcept override {
        return E_INVALIDARG; // immutable properties; nothing is animated
    }
    HRESULT __stdcall GetPropertyCount(UINT* count) noexcept override {
        if (!count) return E_POINTER;
        *count = 3;
        return S_OK;
    }
    HRESULT __stdcall GetProperty(UINT index,
        ABI::Windows::Foundation::IPropertyValue** value) noexcept override {
        if (!value) return E_POINTER;
        *value = nullptr;
        try {
            using winrt::Windows::Foundation::PropertyValue;
            winrt::Windows::Foundation::IInspectable property{ nullptr };
            switch (index) {
            case D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION:
                property = PropertyValue::CreateSingle(18.0f); break;
            case D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION:
                property = PropertyValue::CreateUInt32(D2D1_GAUSSIANBLUR_OPTIMIZATION_BALANCED); break;
            case D2D1_GAUSSIANBLUR_PROP_BORDER_MODE:
                property = PropertyValue::CreateUInt32(D2D1_BORDER_MODE_HARD); break;
            default: return E_INVALIDARG;
            }
            property.as<ABI::Windows::Foundation::IPropertyValue>().copy_to(value);
            return S_OK;
        } catch (...) { return winrt::to_hresult(); }
    }
    HRESULT __stdcall GetSourceCount(UINT* count) noexcept override {
        if (!count) return E_POINTER;
        *count = 1;
        return S_OK;
    }
    HRESULT __stdcall GetSource(UINT index,
        ABI::Windows::Graphics::Effects::IGraphicsEffectSource** source) noexcept override {
        if (!source) return E_POINTER;
        *source = nullptr;
        if (index != 0) return E_INVALIDARG;
        source_.as<ABI::Windows::Graphics::Effects::IGraphicsEffectSource>().copy_to(source);
        return S_OK;
    }
private:
    Source source_;
};


