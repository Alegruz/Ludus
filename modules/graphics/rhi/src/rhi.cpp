#include <volk.h>
#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include <ludus/foundation/base/assert.hpp>
#include <ludus/foundation/base/assert_format.hpp>
#include <ludus/foundation/base/types.h>
#include <ludus/foundation/logging/log_format.hpp>

#include <ludus/graphics/rhi/rhi.h>

using namespace ludus::foundation;

namespace ludus::graphics::rhi
{
inline constexpr logging::LogCategory LOG_RHI{"RHI"};

struct VulkanInfo final
{
    uint32 ApiVersion = 0;
    VkInstance Instance = VK_NULL_HANDLE;
};

static VulkanInfo gVulkanInfo;

static VkBool32 DebugUtilsMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                            VkDebugUtilsMessageTypeFlagsEXT messageType,
                                            const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                            void* pUserData) noexcept;

static bool gIsInitialized = false;
bool Initialize(const ApplicationInfo& appInfo) noexcept
{
    if (gIsInitialized)
    {
        return true;
    }

    VkResult vr = VK_SUCCESS;
    vr = volkInitialize();
    if (vr != VK_SUCCESS)
    {
        LUDUS_ASSERT(false, "Failed to initialize Volk.");
        return false;
    }

    VulkanInfo vulkanInfo = {};
    vr = vkEnumerateInstanceVersion(&vulkanInfo.ApiVersion);
    if (vr != VK_SUCCESS)
    {
        LUDUS_ASSERT(false, "Failed to enumerate Vulkan instance version.");
        return false;
    }
    LUDUS_LOG_INFO(LOG_RHI,
                   "Vulkan instance version: {}.{}.{}",
                   VK_VERSION_MAJOR(vulkanInfo.ApiVersion),
                   VK_VERSION_MINOR(vulkanInfo.ApiVersion),
                   VK_VERSION_PATCH(vulkanInfo.ApiVersion));

    uint32 extensionCount = 0;
    vr = vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
    if (vr != VK_SUCCESS)
    {
        LUDUS_ASSERT(false, "Failed to enumerate Vulkan instance extension properties.");
        return false;
    }

    std::vector<VkExtensionProperties> extensions(extensionCount);
    vr = vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data());
    if (vr != VK_SUCCESS)
    {
        LUDUS_ASSERT(false, "Failed to enumerate Vulkan instance extension properties.");
        return false;
    }

    const std::vector<std::string> listOfExtensionsToEnable = {
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
    };

    std::vector<const char*> enabledExtensions;
    enabledExtensions.reserve(listOfExtensionsToEnable.size());
    // Info logging is compiled out in Release.
    for ([[maybe_unused]] const auto& extension : extensions)
    {
        LUDUS_LOG_INFO(LOG_RHI, "Vulkan instance extension: {}", extension.extensionName);
    }

    for (const auto& extensionToEnable : listOfExtensionsToEnable)
    {
        if (std::find_if(extensions.begin(), extensions.end(), [&](const VkExtensionProperties& ext) {
                return strcmp(ext.extensionName, extensionToEnable.c_str()) == 0;
            }) != extensions.end())
        {
            LUDUS_LOG_INFO(LOG_RHI, "Enabling Vulkan instance extension: {}", extensionToEnable);
            enabledExtensions.push_back(extensionToEnable.c_str());
        }
        else
        {
            LUDUS_ASSERT_F(false,
                           "Vulkan instance extension not supported: {}",
                           diagnostics::DiagnosticText{extensionToEnable.data(), extensionToEnable.size()});
        }
    }

    void* pNext = nullptr;
    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .pNext = nullptr,
        .flags = 0,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = DebugUtilsMessengerCallback,
        .pUserData = nullptr};
    pNext = &debugCreateInfo;

    const VkApplicationInfo applicationInfo = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                                               .pNext = nullptr,
                                               .pApplicationName = appInfo.Name.c_str(),
                                               .applicationVersion = appInfo.Version,
                                               .pEngineName = "Ludus",
                                               .engineVersion = 0,
                                               .apiVersion = vulkanInfo.ApiVersion};

    const VkInstanceCreateInfo instanceCreateInfo = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                                                     .pNext = pNext,
                                                     .flags = 0,
                                                     .pApplicationInfo = &applicationInfo,
                                                     .enabledLayerCount = 0,
                                                     .ppEnabledLayerNames = nullptr,
                                                     .enabledExtensionCount =
                                                         static_cast<uint32>(enabledExtensions.size()),
                                                     .ppEnabledExtensionNames = enabledExtensions.data()};

    vr = vkCreateInstance(&instanceCreateInfo, nullptr, &vulkanInfo.Instance);
    if (vr != VK_SUCCESS)
    {
        LUDUS_ASSERT(false, "Failed to create Vulkan instance.");
        return false;
    }

    volkLoadInstance(vulkanInfo.Instance);

    gVulkanInfo = vulkanInfo;
    gIsInitialized = true;
    return true;
}

void Shutdown() noexcept
{
    if (gIsInitialized || gVulkanInfo.Instance != VK_NULL_HANDLE)
    {
        LUDUS_ASSERT(gIsInitialized, "RHI is not initialized.");
        LUDUS_ASSERT(gVulkanInfo.Instance != VK_NULL_HANDLE, "Vulkan instance is not valid.");
        vkDestroyInstance(gVulkanInfo.Instance, nullptr);
        gVulkanInfo = {};
        gIsInitialized = false;
    }
}

VkBool32 DebugUtilsMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                     [[maybe_unused]] VkDebugUtilsMessageTypeFlagsEXT messageType,
                                     [[maybe_unused]] const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                     [[maybe_unused]] void* pUserData) noexcept
{
    if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    {
        LUDUS_ASSERT_F(false, "Vulkan validation error: {}", diagnostics::DiagnosticCString(pCallbackData->pMessage));
    }
    else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    {
        LUDUS_ASSERT_F(false, "Vulkan validation warning: {}", diagnostics::DiagnosticCString(pCallbackData->pMessage));
    }
    else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
    {
        LUDUS_LOG_INFO(LOG_RHI, "Vulkan validation info: {}", pCallbackData->pMessage);
    }
    else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT)
    {
        LUDUS_LOG_INFO(LOG_RHI, "Vulkan validation verbose: {}", pCallbackData->pMessage);
    }
    return VK_TRUE;
}
} // namespace ludus::graphics::rhi
