#include <Ludus/Engine/RHI/CommandQueue.h>

#if defined(LUDUS_GRAPHICS_D3D12)
#include <Ludus/Engine/Core/SmartPtr.hpp>

#include <Ludus/Engine/Platform/Window.hpp>

#include <Ludus/Engine/RHI/D3D12/Common.h>
#include <Ludus/Engine/RHI/D3D12/CommandQueue.h>

#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace ludus::rhi
{
#define mMemberVariablesD3D12 (*static_cast<CommandQueueMemberVariablesD3D12*>(mMemberVariables.Get()))   // NOLINT(cppcoreguidelines-pro-type-static-cast-downcast,-warnings-as-errors)

    template<GraphicsApi GRAPHICS_API>
    CommandQueue<GRAPHICS_API>::CommandQueue(const Device<GRAPHICS_API>& rhiDevice) noexcept requires(GRAPHICS_API == GraphicsApi::D3D12)
        : DeviceChildObject<GRAPHICS_API>(rhiDevice)
        , mMemberVariables(core::MakeUnique<CommandQueueMemberVariablesD3D12>())
    {
    }

    template<GraphicsApi GRAPHICS_API>
    bool CommandQueue<GRAPHICS_API>::initialize([[maybe_unused]] const CreateInfo& createInfo) noexcept requires(GRAPHICS_API == GraphicsApi::D3D12)
    {
        LUDUS_ASSERT_MSG(false, "D3D12 CommandQueue is not implemented yet.");

        return true;
    }

    template<GraphicsApi GRAPHICS_API>
    void CommandQueue<GRAPHICS_API>::shutdown() noexcept requires(GRAPHICS_API == GraphicsApi::D3D12)
    {
        LUDUS_ASSERT_MSG(false, "D3D12 CommandQueue is not implemented yet.");
    }

    template class CommandQueue<GraphicsApi::D3D12>;
}   // namespace ludus::rhi 
#endif  // defined(LUDUS_GRAPHICS_D3D12)