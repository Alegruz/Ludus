#include <Ludus/Engine/RHI/Instance.h>

#if defined(LUDUS_GRAPHICS_VULKAN)
#include <Ludus/Engine/Core/SmartPtr.hpp>

#include <volk.h>

namespace ludus::rhi
{
    struct InstanceMemberVariablesVulkan final : public InstanceMemberVariablesBase
    {
        uint32_t InstanceVersion = 0;
        VkInstance Instance = VK_NULL_HANDLE;
    };

#define mMemberVariablesVulkan (*static_cast<InstanceMemberVariablesVulkan*>(mMemberVariables.Get()))   // NOLINT(cppcoreguidelines-pro-type-static-cast-downcast,-warnings-as-errors)

    template<GraphicsApi GRAPHICS_API>
    Instance<GRAPHICS_API>::Instance() requires(GRAPHICS_API == GraphicsApi::VULKAN)
        : mMemberVariables(core::MakeUnique<InstanceMemberVariablesVulkan>())
    {
    }

    template<GraphicsApi GRAPHICS_API>
    bool Instance<GRAPHICS_API>::initialize() noexcept requires(GRAPHICS_API == GraphicsApi::VULKAN)
    {
        VkResult vr = volkInitialize();
        if(vr != VK_SUCCESS)
        {
            LUDUS_ASSERT_MSG(false, "Failed to initialize Volk library.");
            return false;
        }

        vr = vkEnumerateInstanceVersion(&mMemberVariablesVulkan.InstanceVersion);
        if(vr != VK_SUCCESS)
        {
            LUDUS_ASSERT_MSG(false, "Failed to enumerate Vulkan instance version.");
            return false;
        }

        return true;
    }

    template class Instance<GraphicsApi::VULKAN>;
}   // namespace ludus::rhi 
#endif  // defined(LUDUS_GRAPHICS_VULKAN)