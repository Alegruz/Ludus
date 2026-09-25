#include <volk.h>
#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <cstring>
#include <string>

#include <ludus/foundation/base/assert.hpp>
#include <ludus/foundation/base/assert_format.hpp>
#include <ludus/foundation/base/types.h>
#include <ludus/foundation/containers/array.hpp>
#include <ludus/foundation/containers/static_array.hpp>
#include <ludus/foundation/logging/log_format.hpp>

#include <ludus/graphics/rhi/rhi.h>

using namespace ludus::foundation;

namespace ludus::graphics::rhi
{
inline constexpr logging::LogCategory LOG_RHI{"RHI"};

struct GpuInfo final
{
    VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties2 Properties2 = {};
    VkPhysicalDeviceVulkan11Properties Vulkan11Properties = {};
    VkPhysicalDeviceVulkan12Properties Vulkan12Properties = {};
    VkPhysicalDeviceVulkan13Properties Vulkan13Properties = {};
    VkPhysicalDeviceVulkan14Properties Vulkan14Properties = {};
    VkPhysicalDeviceAccelerationStructurePropertiesKHR AccelerationStructureProperties = {};
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR RayTracingPipelineProperties = {};
};

struct VulkanInfo final
{
    uint32 ApiVersion = 0;
    VkInstance Instance = VK_NULL_HANDLE;
    Array<GpuInfo> GpuInfos = {};
};

static VulkanInfo gVulkanInfo;

static VkBool32 DebugUtilsMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                            VkDebugUtilsMessageTypeFlagsEXT messageType,
                                            const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                            void* pUserData) noexcept;
[[nodiscard]] static bool initializeInstance(VulkanInfo& inoutVulkanInfo, const ApplicationInfo& appInfo) noexcept;

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
    if (vkEnumerateInstanceVersion != nullptr)
    {
        vr = vkEnumerateInstanceVersion(&vulkanInfo.ApiVersion);
        if (vr != VK_SUCCESS)
        {
            LUDUS_ASSERT(false, "Failed to enumerate Vulkan instance version.");
            return false;
        }
    }
    else
    {
        vulkanInfo.ApiVersion = VK_API_VERSION_1_0;
    }
    LUDUS_LOG_INFO(LOG_RHI,
                   "Vulkan instance version: {}.{}.{}",
                   VK_VERSION_MAJOR(vulkanInfo.ApiVersion),
                   VK_VERSION_MINOR(vulkanInfo.ApiVersion),
                   VK_VERSION_PATCH(vulkanInfo.ApiVersion));

    if (!initializeInstance(vulkanInfo, appInfo))
    {
        return false;
    }
    volkLoadInstance(vulkanInfo.Instance);

    uint32 gpuCount = 0;
    vr = vkEnumeratePhysicalDevices(vulkanInfo.Instance, &gpuCount, nullptr);
    if (vr != VK_SUCCESS)
    {
        LUDUS_ASSERT(false, "Failed to enumerate Vulkan physical devices.");
        return false;
    }

    {
        Array<GpuInfo> gpuInfos(gpuCount);
        {
            Array<VkPhysicalDevice> gpus(gpuCount);
            vr = vkEnumeratePhysicalDevices(vulkanInfo.Instance, &gpuCount, gpus.GetData());
            if (vr != VK_SUCCESS)
            {
                LUDUS_ASSERT(false, "Failed to enumerate Vulkan physical devices.");
                return false;
            }

            for (uint32 i = 0; i < gpuCount; ++i)
            {
                gpuInfos[i] = GpuInfo{ .PhysicalDevice = gpus[i] };
                void* pNext = nullptr;
                if(vulkanInfo.ApiVersion >= VK_API_VERSION_1_2)
                {
                    gpuInfos[i].Vulkan11Properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES;
                    gpuInfos[i].Vulkan11Properties.pNext = pNext;
                    pNext = &gpuInfos[i].Vulkan11Properties;

                    gpuInfos[i].Vulkan12Properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES;
                    gpuInfos[i].Vulkan12Properties.pNext = pNext;
                    pNext = &gpuInfos[i].Vulkan12Properties;
                };
                if(vulkanInfo.ApiVersion >= VK_API_VERSION_1_3)
                {
                    gpuInfos[i].Vulkan13Properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES;
                    gpuInfos[i].Vulkan13Properties.pNext = pNext;
                    pNext = &gpuInfos[i].Vulkan13Properties;
                }
                if(vulkanInfo.ApiVersion >= VK_API_VERSION_1_4)
                {
                    gpuInfos[i].Vulkan14Properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_PROPERTIES;
                    gpuInfos[i].Vulkan14Properties.pNext = pNext;
                    pNext = &gpuInfos[i].Vulkan14Properties;
                }
                gpuInfos[i].AccelerationStructureProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
                gpuInfos[i].AccelerationStructureProperties.pNext = pNext;
                pNext = &gpuInfos[i].AccelerationStructureProperties;
                gpuInfos[i].RayTracingPipelineProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
                gpuInfos[i].RayTracingPipelineProperties.pNext = pNext;
                pNext = &gpuInfos[i].RayTracingPipelineProperties;

                gpuInfos[i].Properties2 =
                {
                    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
                    .pNext = pNext,
                    .properties = {}
                };
                vkGetPhysicalDeviceProperties2(gpus[i], &gpuInfos[i].Properties2);
                LUDUS_LOG_INFO(LOG_RHI,
                               "Found Vulkan physical device: {}",
                               gpuInfos[i].Properties2.properties.deviceName);
            }
        }

        vulkanInfo.GpuInfos = gpuInfos;
    }

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

bool initializeInstance(VulkanInfo& inoutVulkanInfo, const ApplicationInfo& appInfo) noexcept
{
    uint32 extensionCount = 0;
    VkResult vr = vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
    if (vr != VK_SUCCESS)
    {
        LUDUS_ASSERT(false, "Failed to enumerate Vulkan instance extension properties.");
        return false;
    }

    Array<VkExtensionProperties> extensions(extensionCount);
    vr = vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.GetData());
    if (vr != VK_SUCCESS)
    {
        LUDUS_ASSERT(false, "Failed to enumerate Vulkan instance extension properties.");
        return false;
    }

    Array<std::string> listOfExtensionsToEnable;
#if defined(LUDUS_RHI_ENABLE_VALIDATION_LAYER)
    listOfExtensionsToEnable.Add(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif
    if (inoutVulkanInfo.ApiVersion < VK_API_VERSION_1_1)
    {
        listOfExtensionsToEnable.Add(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    }

    Array<const char*> enabledExtensions;
    enabledExtensions.EnsureCapacity(listOfExtensionsToEnable.GetSize());
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
            enabledExtensions.Add(extensionToEnable.c_str());
        }
        else
        {
            LUDUS_ASSERT_F(false,
                           "Vulkan instance extension not supported: {}",
                           diagnostics::DiagnosticText{extensionToEnable.data(), extensionToEnable.size()});
        }
    }

    const void* pNext = nullptr;
#if defined(LUDUS_RHI_ENABLE_VALIDATION_LAYER)
    const VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo =
    {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .pNext = nullptr,
        .flags = 0,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = DebugUtilsMessengerCallback,
        .pUserData = nullptr
    };

    constexpr StaticArray<VkValidationFeatureEnableEXT, 5> enabledValidationFeatures =
    {
        VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT,
        VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT,
        VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT,
        VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT,
        VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
    };

    const VkValidationFeaturesEXT validationFeatures =
    {
        .sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
        .pNext = &debugCreateInfo,
        .enabledValidationFeatureCount = static_cast<uint32>(enabledValidationFeatures.GetSize()),
        .pEnabledValidationFeatures = enabledValidationFeatures.GetData(),
        .disabledValidationFeatureCount = 0,
        .pDisabledValidationFeatures = nullptr
    };
    pNext = &validationFeatures;
#endif

    const VkApplicationInfo applicationInfo =
    {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = nullptr,
        .pApplicationName = appInfo.Name.c_str(),
        .applicationVersion = appInfo.Version,
        .pEngineName = "Ludus",
        .engineVersion = 0,
        .apiVersion = inoutVulkanInfo.ApiVersion
    };

    const VkInstanceCreateInfo instanceCreateInfo =
    {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = pNext,
        .flags = 0,
        .pApplicationInfo = &applicationInfo,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = static_cast<uint32>(enabledExtensions.GetSize()),
        .ppEnabledExtensionNames = enabledExtensions.GetData()}
    ;

    vr = vkCreateInstance(&instanceCreateInfo, nullptr, &inoutVulkanInfo.Instance);
    if (vr != VK_SUCCESS)
    {
        LUDUS_ASSERT(false, "Failed to create Vulkan instance.");
        return false;
    }
    return true;
}
} // namespace ludus::graphics::rhi
