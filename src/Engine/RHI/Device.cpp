#include <Ludus/Engine/RHI/Device.h>

#include <Ludus/Engine/Core/SmartPtr.hpp>
#include <Ludus/Engine/Core/Container/HashMap.hpp>
#include <Ludus/Engine/Core/Container/String.hpp>

namespace ludus::rhi
{
    Device::Device(const Adapter& rhiAdapter) noexcept
        : AdapterChildObject(rhiAdapter)
        , mMemberVariables(core::MakeUnique<DeviceMemberVariables>())
    {
    }

    bool Device::Initialize(const CreateInfo& createInfo) noexcept
    {
        if( initialize(createInfo) == false )
        {
            LUDUS_ASSERT_MSG(false, "Failed to initialize RHI Device.");
            return false;
        }

        return true;
    }

    bool Device::initialize([[maybe_unused]] const CreateInfo& createInfo) noexcept
    {
        // Vulkan RHI device initialization logic (if any) goes here.
        LUDUS_ASSERT_MSG(false, "Vulkan RHI Device is not implemented yet.");

        return true;
    }

    void Device::shutdown() noexcept
    {
        // Vulkan RHI device shutdown logic (if any) goes here.
        LUDUS_ASSERT_MSG(false, "Vulkan RHI Device is not implemented yet.");
    }

    core::UniquePtr<CommandQueue>& Device::createCommandQueue([[maybe_unused]] const CommandQueue::CreateInfo& createInfo) noexcept
    {
        // Vulkan RHI command queue creation logic (if any) goes here.
        LUDUS_ASSERT_MSG(false, "Vulkan RHI CommandQueue creation is not implemented yet.");

        // Placeholder return to satisfy the compiler; replace with actual implementation.
        static core::UniquePtr<CommandQueue> dummyQueue;
        return dummyQueue;
    }
}   // namespace ludus::rhi
