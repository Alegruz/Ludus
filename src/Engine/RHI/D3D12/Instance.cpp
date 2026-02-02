#include <Ludus/Engine/RHI/Instance.h>

#if defined(LUDUS_GRAPHICS_D3D12)
#include <Ludus/Engine/Core/Container/HashMap.hpp>
#include <Ludus/Engine/Core/Container/String.hpp>
#include <Ludus/Engine/Core/SmartPtr.hpp>

#include <Ludus/Engine/Platform/Window.hpp>

#define LUDUS_DXGI_VERSION_1_6  (16)
#define LUDUS_DXGI_VERSION_1_5  (15)
#define LUDUS_DXGI_VERSION_1_4  (14)
#define LUDUS_DXGI_VERSION_1_3  (13)
#define LUDUS_DXGI_VERSION_1_2  (12)
#define LUDUS_DXGI_VERSION_1_1  (11)
#define LUDUS_DXGI_VERSION_1_0  (10)

#if __has_include(<dxgi1_6.h>)
    #define LUDUS_DXGI_VERSION  (16)
    #include <dxgi1_6.h>
#elif __has_include(<dxgi1_5.h>)
    #define LUDUS_DXGI_VERSION  (15)
    #include <dxgi1_5.h>
#elif __has_include(<dxgi1_4.h>)
    #define LUDUS_DXGI_VERSION  (14)
    #include <dxgi1_4.h>
#elif __has_include(<dxgi1_3.h>)
    #define LUDUS_DXGI_VERSION  (13)
    #include <dxgi1_3.h>
#elif __has_include(<dxgi1_2.h>)
    #define LUDUS_DXGI_VERSION  (12)
    #include <dxgi1_2.h>
#elif __has_include(<dxgi1_1.h>)
    #define LUDUS_DXGI_VERSION  (11)
    #include <dxgi1_1.h>
#elif __has_include(<dxgi.h>)
    #define LUDUS_DXGI_VERSION  (10)
    #include <dxgi.h>
#else
    #error "DXGI header not found."
#endif  // __has_include(<dxgi1_6.h>)
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

// D3D12 Agility SDK version - Update D3D12SDKVersion to match the installed SDK
// Current version: 1.618.5 → SDK version 618
// Released: 12/5/2025
// See: https://devblogs.microsoft.com/directx/directx12agility/
extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 618; }    // NOLINT(readability-identifier-naming)
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\"; }  // NOLINT(readability-identifier-naming)

namespace ludus::rhi
{
#if LUDUS_DXGI_VERSION >= LUDUS_DXGI_VERSION_1_6
    using LudusDxgiFactory = IDXGIFactory7;
#elif LUDUS_DXGI_VERSION == LUDUS_DXGI_VERSION_1_5
    using LudusDxgiFactory = IDXGIFactory5;
#elif LUDUS_DXGI_VERSION == LUDUS_DXGI_VERSION_1_4
    using LudusDxgiFactory = IDXGIFactory4;
#elif LUDUS_DXGI_VERSION == LUDUS_DXGI_VERSION_1_3
    using LudusDxgiFactory = IDXGIFactory3;
#elif LUDUS_DXGI_VERSION == LUDUS_DXGI_VERSION_1_2
    using LudusDxgiFactory = IDXGIFactory2;
#elif LUDUS_DXGI_VERSION >= LUDUS_DXGI_VERSION_1_0
    using LudusDxgiFactory = IDXGIFactory1;
#endif  // LUDUS_DXGI_VERSION >= LUDUS_DXGI_VERSION_1_6

#if LUDUS_DXGI_VERSION >= LUDUS_DXGI_VERSION_1_3
    using LudusDxgiCreatingFactory = IDXGIFactory2;
#else
    using LudusDxgiCreatingFactory = IDXGIFactory1;
#endif  // LUDUS_DXGI_VERSION < LUDUS_DXGI_VERSION_1_3

    struct InstanceMemberVariablesD3D12 final : public InstanceMemberVariablesBase
    {
        ComPtr<LudusDxgiFactory> DxgiFactory = nullptr;
    };

#define mMemberVariablesD3D12 (*static_cast<InstanceMemberVariablesD3D12*>(mMemberVariables.Get()))   // NOLINT(cppcoreguidelines-pro-type-static-cast-downcast,-warnings-as-errors)

    template<GraphicsApi GRAPHICS_API>
    Instance<GRAPHICS_API>::Instance() noexcept requires(GRAPHICS_API == GraphicsApi::D3D12)
        : mMemberVariables(core::MakeUnique<InstanceMemberVariablesD3D12>())
    {
    }

    template<GraphicsApi GRAPHICS_API>
    bool Instance<GRAPHICS_API>::initialize([[maybe_unused]] const CreateInfo& createInfo) noexcept requires(GRAPHICS_API == GraphicsApi::D3D12)
    {
        HRESULT hr = S_OK;
        {
            ComPtr<LudusDxgiCreatingFactory> dxgiFactory;
            if constexpr (LUDUS_DXGI_VERSION >= LUDUS_DXGI_VERSION_1_3)
            {
#if defined(LUDUS_DEBUG)
                constexpr const uint32_t DXGI_FACTORY_FLAGS = DXGI_CREATE_FACTORY_DEBUG;
#else   // defined(LUDUS_DEBUG)
                constexpr const uint32_t DXGI_FACTORY_FLAGS = 0;
#endif  // defined(LUDUS_DEBUG)

                hr = CreateDXGIFactory2(DXGI_FACTORY_FLAGS, IID_PPV_ARGS(&dxgiFactory));
            }
            
            if(FAILED(hr))
            {
                hr = CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory));
                if (FAILED(hr))
                {
                    LUDUS_ASSERT_MSG(false, "Failed to create DXGI Factory.");
                    return false;
                }
            }

            hr = dxgiFactory.As(&mMemberVariablesD3D12.DxgiFactory);
            if (FAILED(hr))
            {
                LUDUS_ASSERT_MSG(false, "Failed to query IDXGIFactory interface.");
                return false;
            }
        }

        const HWND windowHandle = reinterpret_cast<HWND>(createInfo.Window.GetPlatformHandle());  // NOLINT(performance-no-int-to-ptr)
        LUDUS_ASSERT_MSG(windowHandle != NULL, "Invalid window handle for D3D12 instance initialization.");

        hr = mMemberVariablesD3D12.DxgiFactory->MakeWindowAssociation(windowHandle, DXGI_MWA_NO_ALT_ENTER);
        if (FAILED(hr))
        {
            LUDUS_ASSERT_MSG(false, "Failed to make window association for D3D12.");
            return false;
        }
        
        if constexpr (LUDUS_DXGI_VERSION >= LUDUS_DXGI_VERSION_1_2)
        {
            if(mMemberVariablesD3D12.DxgiFactory != nullptr)
            {
                if(mMemberVariablesD3D12.DxgiFactory->IsWindowedStereoEnabled() == TRUE)
                {
                    // TODO: Handle stereo 3D if needed
                }
            }
        }

        if constexpr (LUDUS_DXGI_VERSION >= LUDUS_DXGI_VERSION_1_5)
        {
            // TODO: Use CheckFeatureSupport method
        }

        return true;
    }

    template<GraphicsApi GRAPHICS_API>
    void Instance<GRAPHICS_API>::shutdown() noexcept requires(GRAPHICS_API == GraphicsApi::D3D12)
    {
    }

    template class Instance<GraphicsApi::D3D12>;
}   // namespace ludus::rhi 
#endif  // defined(LUDUS_GRAPHICS_D3D12)