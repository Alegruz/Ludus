#include <Ludus/Engine/RHI/SwapChain.h>

#if defined(LUDUS_GRAPHICS_VULKAN)
#include <Ludus/Engine/Core/SmartPtr.hpp>

namespace ludus::rhi
{
    struct SwapChainMemberVariablesVulkan final : public SwapChainMemberVariablesBase
    {
    };

#define mMemberVariablesVulkan (*static_cast<SwapChainMemberVariablesVulkan*>(mMemberVariables.Get()))   // NOLINT(cppcoreguidelines-pro-type-static-cast-downcast,-warnings-as-errors)

    template<GraphicsApi GRAPHICS_API>
    SwapChain<GRAPHICS_API>::SwapChain(const Instance<GRAPHICS_API>& rhiInstance)  noexcept requires(GRAPHICS_API == GraphicsApi::VULKAN)
        : InstanceChildObject<GRAPHICS_API>(rhiInstance)
        , mMemberVariables(core::MakeUnique<SwapChainMemberVariablesVulkan>())
    {
    }

    template<GraphicsApi GRAPHICS_API>
    bool SwapChain<GRAPHICS_API>::initialize([[maybe_unused]] const CreateInfo& createInfo) noexcept requires(GRAPHICS_API == GraphicsApi::VULKAN)
    {
        LUDUS_ASSERT_MSG(false, "Vulkan SwapChain is not implemented yet.");

        return true;
    }

    template<GraphicsApi GRAPHICS_API>
    void SwapChain<GRAPHICS_API>::shutdown() noexcept requires(GRAPHICS_API == GraphicsApi::VULKAN)
    {
        LUDUS_ASSERT_MSG(false, "Vulkan SwapChain is not implemented yet.");
    }

    template class SwapChain<GraphicsApi::VULKAN>;
}   // namespace ludus::rhi 
#endif  // defined(LUDUS_GRAPHICS_VULKAN)