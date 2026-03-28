#include <Ludus/Engine/RHI/CommandQueue.h>

#include <Ludus/Engine/Core/SmartPtr.hpp>

namespace ludus::rhi
{
    CommandQueue::CommandQueue(const Device& rhiDevice)  noexcept
        : DeviceChildObject(rhiDevice)
        , mMemberVariables(core::MakeUnique<CommandQueueMemberVariables>())
    {
    }

    bool CommandQueue::initialize([[maybe_unused]] const CreateInfo& createInfo) noexcept
    {
        LUDUS_ASSERT_MSG(false, "Vulkan CommandQueue is not implemented yet.");

        return true;
    }

    void CommandQueue::shutdown() noexcept
    {
        LUDUS_ASSERT_MSG(false, "Vulkan CommandQueue is not implemented yet.");
    }
}   // namespace ludus::rhi 
