#include <Ludus/Engine/RHI/CommandQueue.h>

#if defined(LUDUS_GRAPHICS_VULKAN)
#include <Ludus/Engine/Core/SmartPtr.hpp>

namespace ludus::rhi
{
    struct CommandQueueMemberVariablesVulkan final : public CommandQueueMemberVariablesBase
    {
    };

#define mMemberVariablesVulkan (*static_cast<CommandQueueMemberVariablesVulkan*>(mMemberVariables.Get()))   // NOLINT(cppcoreguidelines-pro-type-static-cast-downcast,-warnings-as-errors)
    template<GraphicsApi GRAPHICS_API>
    CommandQueue<GRAPHICS_API>::CommandQueue(const Device<GRAPHICS_API>& rhiDevice)  noexcept requires(GRAPHICS_API == GraphicsApi::VULKAN)
        : DeviceChildObject<GRAPHICS_API>(rhiDevice)
        , mMemberVariables(core::MakeUnique<CommandQueueMemberVariablesVulkan>())
    {
    }

    template<GraphicsApi GRAPHICS_API>
    bool CommandQueue<GRAPHICS_API>::initialize([[maybe_unused]] const CreateInfo& createInfo) noexcept requires(GRAPHICS_API == GraphicsApi::VULKAN)
    {
        LUDUS_ASSERT_MSG(false, "Vulkan CommandQueue is not implemented yet.");

        return true;
    }

    template<GraphicsApi GRAPHICS_API>
    void CommandQueue<GRAPHICS_API>::shutdown() noexcept requires(GRAPHICS_API == GraphicsApi::VULKAN)
    {
        LUDUS_ASSERT_MSG(false, "Vulkan CommandQueue is not implemented yet.");
    }

    template class CommandQueue<GraphicsApi::VULKAN>;
}   // namespace ludus::rhi 
#endif  // defined(LUDUS_GRAPHICS_VULKAN)