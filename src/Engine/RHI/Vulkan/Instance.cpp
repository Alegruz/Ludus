#include <Ludus/Engine/RHI/Instance.h>

#if defined(LUDUS_GRAPHICS_VULKAN)
#include <Ludus/Engine/Core/Container/String.hpp>
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
    bool Instance<GRAPHICS_API>::initialize(const CreateInfo& createInfo) noexcept requires(GRAPHICS_API == GraphicsApi::VULKAN)
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

        void* pNext = nullptr;

        const VkApplicationInfo appInfo = 
        {
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pNext = nullptr,
            .pApplicationName = createInfo.ApplicationInfo.Name.GetCStr(),
            .applicationVersion = createInfo.ApplicationInfo.Version,
            .pEngineName = createInfo.EngineInfo.Name.GetCStr(),
            .engineVersion = createInfo.EngineInfo.Version,
            .apiVersion = mMemberVariablesVulkan.InstanceVersion,
        };

        core::DynamicArray<const char*> enabledLayers;
        core::DynamicArray<const char*> enabledExtensions;

        const VkInstanceCreateInfo instanceCreateInfo = 
        {
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pNext = pNext,
            .flags = 0,
            .pApplicationInfo = &appInfo,
            .enabledLayerCount = enabledLayers.GetSize(),
            .ppEnabledLayerNames = enabledLayers.GetData(),
            .enabledExtensionCount = enabledExtensions.GetSize(),
            .ppEnabledExtensionNames = enabledExtensions.GetData(),
        };

        return true;
    }

    template class Instance<GraphicsApi::VULKAN>;
}   // namespace ludus::rhi 
#endif  // defined(LUDUS_GRAPHICS_VULKAN)