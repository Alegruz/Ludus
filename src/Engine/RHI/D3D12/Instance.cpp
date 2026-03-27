#include <Ludus/Engine/RHI/Instance.h>

#if defined(LUDUS_GRAPHICS_D3D12)
#include <Ludus/Engine/Core/Container/HashMap.hpp>
#include <Ludus/Engine/Core/Container/String.hpp>
#include <Ludus/Engine/Core/SmartPtr.hpp>

#include <Ludus/Engine/Platform/Window.hpp>

#include <Ludus/Engine/RHI/D3D12/Common.h>
#include <Ludus/Engine/RHI/D3D12/Adapter.h>
#include <Ludus/Engine/RHI/D3D12/SwapChain.h>

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
    struct InstanceMemberVariablesD3D12 final : public InstanceMemberVariablesBase<GraphicsApi::D3D12>
    {
        LUDUS_INLINE explicit InstanceMemberVariablesD3D12(Instance<GraphicsApi::D3D12>& rhiInstance)
            : InstanceMemberVariablesBase<GraphicsApi::D3D12>(rhiInstance)
        {
        }

        ComPtr<LudusDxgiFactory> DxgiFactory = nullptr;
    };

#define mMemberVariablesD3D12 (*static_cast<InstanceMemberVariablesD3D12*>(mMemberVariables.Get()))   // NOLINT(cppcoreguidelines-pro-type-static-cast-downcast,-warnings-as-errors)

    template<GraphicsApi GRAPHICS_API>
    Instance<GRAPHICS_API>::Instance() noexcept requires(GRAPHICS_API == GraphicsApi::D3D12)
        : mMemberVariables(core::MakeUnique<InstanceMemberVariablesD3D12>(*this))
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
    bool Instance<GRAPHICS_API>::initializePostSwapChainInitialization([[maybe_unused]] const CreateInfo& createInfo) noexcept requires(GRAPHICS_API == GraphicsApi::D3D12)
    {
        const HWND windowHandle = reinterpret_cast<HWND>(createInfo.Window.GetWindowHandle());  // NOLINT(performance-no-int-to-ptr)
        LUDUS_ASSERT_MSG(windowHandle != NULL, "Invalid window handle for D3D12 instance initialization.");
        
        HRESULT hr = mMemberVariablesD3D12.DxgiFactory->MakeWindowAssociation(windowHandle, DXGI_MWA_NO_ALT_ENTER);
        if (FAILED(hr))
        {
            LUDUS_ASSERT_MSG(false, "Failed to make window association for D3D12.");
            return false;
        }

        return true;
    }

    template<GraphicsApi GRAPHICS_API>
    bool Instance<GRAPHICS_API>::initializeAdaptersImpl() noexcept requires(GRAPHICS_API == GraphicsApi::D3D12)
    {
        HRESULT hr = S_OK;
        uint32_t adapterIndex = 0;
        while(true)
        {
            ComPtr<IDXGIAdapter> dxgiAdapter;
            if constexpr (LUDUS_DXGI_VERSION >= LUDUS_DXGI_VERSION_1_6  )
            {
                hr = mMemberVariablesD3D12.DxgiFactory->EnumAdapterByGpuPreference(
                    adapterIndex,
                    DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                    IID_PPV_ARGS(dxgiAdapter.GetAddressOf()));
            }
            else if constexpr (LUDUS_DXGI_VERSION >= LUDUS_DXGI_VERSION_1_0)
            {
                ComPtr<IDXGIAdapter1> dxgiAdapter1;
                hr = mMemberVariablesD3D12.DxgiFactory->EnumAdapters1(
                    adapterIndex,
                    dxgiAdapter1.GetAddressOf());
                if (SUCCEEDED(hr))
                {
                    hr = dxgiAdapter1.As(&dxgiAdapter);
                    if( FAILED(hr))
                    {
                        LUDUS_ASSERT_MSG(false, "Failed to query IDXGIAdapter interface for D3D12 adapter.");
                        return false;
                    }
                }
            }
            else
            {
                hr = mMemberVariablesD3D12.DxgiFactory->EnumAdapters(
                    adapterIndex,
                    dxgiAdapter.GetAddressOf());
            }
            if (hr == DXGI_ERROR_NOT_FOUND)
            {
                break;
            }
            
            if (FAILED(hr))
            {
                LUDUS_ASSERT_MSG(false, "Failed to enumerate DXGI adapters by GPU preference.");
                return false;
            }
            adapterIndex++;
            mMemberVariablesD3D12.Adapters.PushBack(Adapter<GraphicsApi::D3D12>(*this));
            
            hr = dxgiAdapter.As(&static_cast<AdapterMemberVariablesD3D12&>(*mMemberVariablesD3D12.Adapters.GetBack().mMemberVariables).DxgiAdapter);   // NOLINT(cppcoreguidelines-pro-type-static-cast-downcast)
            if (FAILED(hr))
            {
                LUDUS_ASSERT_MSG(false, "Failed to query IDXGIAdapter interface for D3D12 adapter.");
                return false;
            }

            // TODO: Select main adapter based on criteria (e.g., dedicated GPU)
            if(mMemberVariablesD3D12.MainAdapterIndex == InstanceMemberVariablesBase<GraphicsApi::D3D12>::INVALID_ADAPTER_INDEX)
            {
                mMemberVariablesD3D12.MainAdapterIndex = static_cast<int32_t>(adapterIndex - 1);
            }
        }

        return true;
    }

    template<GraphicsApi GRAPHICS_API>
    void Instance<GRAPHICS_API>::shutdown() noexcept requires(GRAPHICS_API == GraphicsApi::D3D12)
    {
    }

    template<GraphicsApi GRAPHICS_API>
    bool Instance<GRAPHICS_API>::createSwapChainImpl([[maybe_unused]] const typename SwapChain<GRAPHICS_API>::CreateInfo& createInfo) noexcept requires(GRAPHICS_API == GraphicsApi::D3D12)
    {
        // D3D12 RHI swap chain creation logic (if any) goes here.
        LUDUS_ASSERT_MSG(false, "D3D12 RHI SwapChain creation is not implemented yet.");
        
        HRESULT hr = S_OK;
        const HWND windowHandle = reinterpret_cast<HWND>(createInfo.Window.GetWindowHandle()); // NOLINT(performance-no-int-to-ptr)
        if constexpr (LUDUS_DXGI_VERSION >= LUDUS_DXGI_VERSION_1_2)
        {
            const DXGI_SWAP_CHAIN_DESC1 swapChainDesc1 = 
            {
                .Width = createInfo.Window.GetWidth(),
                .Height = createInfo.Window.GetHeight(),
                .Format = DXGI_FORMAT_R8G8B8A8_UNORM,   // TODO: Make configurable
                .Stereo = FALSE,
                .SampleDesc = DXGI_SAMPLE_DESC{ .Count = 1, .Quality = 0 },
                .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
                .BufferCount = createInfo.BufferCount,  // TODO: Make configurable
                .Scaling = DXGI_SCALING_ASPECT_RATIO_STRETCH,
                .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
                .AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED,       // TODO: Check which alpha mode to use
                .Flags = 0, // TODO: Check which flags to use
            };

            ComPtr<IDXGISwapChain1> dxgiSwapChain1;
            hr = mMemberVariablesD3D12.DxgiFactory->CreateSwapChainForHwnd(nullptr, windowHandle, &swapChainDesc1, nullptr, nullptr, dxgiSwapChain1.GetAddressOf());
            if (FAILED(hr))
            {
                LUDUS_ASSERT_MSG(false, "Failed to create DXGI SwapChain1 for D3D12.");
                return false;
            }

            hr = static_cast<SwapChainMemberVariablesD3D12&>(*mMemberVariablesD3D12.SwapChain.mMemberVariables).DxgiSwapChain.As(&dxgiSwapChain1); // NOLINT(cppcoreguidelines-pro-type-static-cast-downcast)
            if (FAILED(hr))
            {
                LUDUS_ASSERT_MSG(false, "Failed to query IDXGISwapChain3 interface from SwapChain1.");
                return false;
            }
        }
        else
        {
            DXGI_SWAP_CHAIN_DESC swapChainDesc = 
            {
                .BufferDesc = DXGI_MODE_DESC
                {
                    .Width = createInfo.Window.GetWidth(),
                    .Height = createInfo.Window.GetHeight(),
                    // TODO: Make configurable
                    .RefreshRate = DXGI_RATIONAL{ .Numerator = 60, .Denominator = 1 }, // NOLINT(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
                    .Format = DXGI_FORMAT_R8G8B8A8_UNORM,   // TODO: Make configurable
                    .ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED,   // TODO: Check which ordering to use
                    .Scaling = DXGI_SCALING_ASPECT_RATIO_STRETCH,
                },
                .SampleDesc = DXGI_SAMPLE_DESC{ .Count = 1, .Quality = 0 },
                .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
                .BufferCount = createInfo.BufferCount,  // TODO: Make configurable
                .OutputWindow = windowHandle,
                .Windowed = TRUE,
                .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
                .Flags = 0, // TODO: Check which flags to use
            };
            ComPtr<IDXGISwapChain> dxgiSwapChain;
            hr = mMemberVariablesD3D12.DxgiFactory->CreateSwapChain(nullptr, &swapChainDesc, dxgiSwapChain.GetAddressOf());
            if (FAILED(hr))
            {
                LUDUS_ASSERT_MSG(false, "Failed to create DXGI SwapChain for D3D12.");
                return false;
            }
            hr = static_cast<SwapChainMemberVariablesD3D12&>(*mMemberVariablesD3D12.SwapChain.mMemberVariables).DxgiSwapChain.As(&dxgiSwapChain);  // NOLINT(cppcoreguidelines-pro-type-static-cast-downcast)
            if (FAILED(hr))
            {
                LUDUS_ASSERT_MSG(false, "Failed to query IDXGISwapChain3 interface from SwapChain.");
                return false;
            }
        }

        return true;
    }

    template class Instance<GraphicsApi::D3D12>;
}   // namespace ludus::rhi 
#endif  // defined(LUDUS_GRAPHICS_D3D12)