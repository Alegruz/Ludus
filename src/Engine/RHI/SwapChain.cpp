#include <Ludus/Engine/RHI/SwapChain.h>

#include <Ludus/Engine/Core/SmartPtr.hpp>

namespace ludus::rhi
{
    SwapChain::SwapChain(const Instance& rhiInstance)  noexcept
        : InstanceChildObject(rhiInstance)
        , mMemberVariables(core::MakeUnique<SwapChainMemberVariables>())
    {
    }

    bool SwapChain::initialize([[maybe_unused]] const CreateInfo& createInfo) noexcept
    {
        LUDUS_ASSERT_MSG(false, "Vulkan SwapChain is not implemented yet.");

        return true;
    }

    void SwapChain::shutdown() noexcept
    {
        LUDUS_ASSERT_MSG(false, "Vulkan SwapChain is not implemented yet.");
    }
}   // namespace ludus::rhi 
