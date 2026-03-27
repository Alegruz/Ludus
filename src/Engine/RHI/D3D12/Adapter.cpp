#include <Ludus/Engine/RHI/Adapter.h>

#if defined(LUDUS_GRAPHICS_D3D12)
#include <Ludus/Engine/Core/SmartPtr.hpp>

#include <Ludus/Engine/RHI/D3D12/Adapter.h>

namespace ludus::rhi
{
#define mMemberVariablesD3D12 (*static_cast<AdapterMemberVariablesD3D12*>(mMemberVariables.Get()))   // NOLINT(cppcoreguidelines-pro-type-static-cast-downcast,-warnings-as-errors)

    template<GraphicsApi GRAPHICS_API>
    Adapter<GRAPHICS_API>::Adapter(const Instance<GRAPHICS_API>& rhiInstance) noexcept requires(GRAPHICS_API == GraphicsApi::D3D12)
        : InstanceChildObject<GRAPHICS_API>(rhiInstance)
        , mMemberVariables(core::MakeUnique<AdapterMemberVariablesD3D12>(*this))
    {
    }

    template<GraphicsApi GRAPHICS_API>
    bool Adapter<GRAPHICS_API>::initialize([[maybe_unused]] const CreateInfo& createInfo) noexcept requires(GRAPHICS_API == GraphicsApi::D3D12)
    {
        // D3D12 RHI adapter initialization logic (if any) goes here.
        LUDUS_ASSERT_MSG(false, "D3D12 RHI Adapter is not implemented yet.");

        return true;
    }

    template<GraphicsApi GRAPHICS_API>
    void Adapter<GRAPHICS_API>::shutdown() noexcept requires(GRAPHICS_API == GraphicsApi::D3D12)
    {
        // D3D12 RHI adapter shutdown logic (if any) goes here.
        LUDUS_ASSERT_MSG(false, "D3D12 RHI Adapter is not implemented yet.");
    }

    template<GraphicsApi GRAPHICS_API>
    bool Adapter<GRAPHICS_API>::createDeviceImpl([[maybe_unused]] const typename Device<GRAPHICS_API>::CreateInfo& createInfo) noexcept requires(GRAPHICS_API == GraphicsApi::D3D12)
    {
        HRESULT hr = S_OK;

        ComPtr<ID3D12Device> d3d12Device;
        hr = D3D12CreateDevice(
            mMemberVariablesD3D12.DxgiAdapter.Get(),
            D3D_FEATURE_LEVEL_12_2, // TODO: Choose feature level based on requirements
            IID_PPV_ARGS(d3d12Device.GetAddressOf())
        );
        if(FAILED(hr))
        {
            LUDUS_ASSERT_MSG(false, "Failed to create D3D12 Device.");
            return false;
        }
        mMemberVariablesD3D12.D3D12Device = d3d12Device;
        return true;
    }

    template class Adapter<GraphicsApi::D3D12>;
}   // namespace ludus::rhi
#endif  // defined(LUDUS_GRAPHICS_D3D12)