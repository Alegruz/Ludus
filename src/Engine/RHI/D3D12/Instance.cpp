#include <Ludus/Engine/RHI/Instance.h>

#if defined(LUDUS_GRAPHICS_D3D12)
#include <Ludus/Engine/Core/Container/HashMap.hpp>
#include <Ludus/Engine/Core/Container/String.hpp>
#include <Ludus/Engine/Core/SmartPtr.hpp>

#include <dxgi1_6.h>
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
    struct InstanceMemberVariablesD3D12 final : public InstanceMemberVariablesBase
    {
        ComPtr<IDXGIFactory6> DxgiFactory = nullptr;
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
        return true;
    }

    template<GraphicsApi GRAPHICS_API>
    void Instance<GRAPHICS_API>::shutdown() noexcept requires(GRAPHICS_API == GraphicsApi::D3D12)
    {
    }

    template class Instance<GraphicsApi::D3D12>;
}   // namespace ludus::rhi 
#endif  // defined(LUDUS_GRAPHICS_D3D12)