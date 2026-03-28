#include <Ludus/Engine/RHI/CommandQueue.h>

#if defined(LUDUS_GRAPHICS_CPU)
#include <Ludus/Engine/Core/SmartPtr.hpp>

namespace ludus::rhi
{   
    struct CommandQueueMemberVariablesCpu final : public CommandQueueMemberVariablesBase
    {
    };

    template<GraphicsApi GRAPHICS_API>
    CommandQueue<GRAPHICS_API>::CommandQueue(const Device<GRAPHICS_API>& rhiDevice) noexcept requires(GRAPHICS_API == GraphicsApi::CPU)
        : DeviceChildObject<GRAPHICS_API>(rhiDevice)
        , mMemberVariables(core::MakeUnique<CommandQueueMemberVariablesCpu>())
    {
    }

    template<GraphicsApi GRAPHICS_API>
    bool CommandQueue<GRAPHICS_API>::initialize([[maybe_unused]] const CreateInfo& createInfo) noexcept requires(GRAPHICS_API == GraphicsApi::CPU)
    {
        // CPU RHI command queue initialization logic (if any) goes here.
        LUDUS_ASSERT_MSG(false, "CPU RHI CommandQueue is not implemented yet.");

        return true;
    }

    template<GraphicsApi GRAPHICS_API>
    void CommandQueue<GRAPHICS_API>::shutdown() noexcept requires(GRAPHICS_API == GraphicsApi::CPU)
    {
        // CPU RHI command queue shutdown logic (if any) goes here.
        LUDUS_ASSERT_MSG(false, "CPU RHI CommandQueue is not implemented yet.");
    }

    template class CommandQueue<GraphicsApi::CPU>;
}   // namespace ludus::rhi
#endif  // defined(LUDUS_GRAPHICS_CPU)