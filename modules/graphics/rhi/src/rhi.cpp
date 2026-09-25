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

struct QueueFamilyInfo final
{
    VkQueueFamilyProperties2 Properties = {};
    VkQueueFamilyGlobalPriorityProperties GlobalPriorityProperties = {};
    Array<VkPerformanceCounterKHR> PerformanceCounters = {};
    Array<VkPerformanceCounterDescriptionKHR> PerformanceCounterDescriptions = {};
};

#if defined(LUDUS_BUILD_DEBUG) || defined(LUDUS_BUILD_DEVELOPMENT) || defined(LUDUS_BUILD_PROFILE)
    #define LUDUS_RHI_DEVICE_DEBUG_FEATURES_PROFILE
#if defined(LUDUS_BUILD_PROFILE) == false
    #define LUDUS_RHI_DEVICE_DEBUG_FEATURES
#endif
#endif

struct DeviceInfo final
{
    VkDevice Device = VK_NULL_HANDLE;
    VkPhysicalDeviceFeatures2 Features2 = {};
    VkPhysicalDeviceVulkan11Features Vulkan11Features = {};
    VkPhysicalDeviceVulkan12Features Vulkan12Features = {};
    VkPhysicalDeviceVulkan13Features Vulkan13Features = {};
    VkPhysicalDeviceVulkan14Features Vulkan14Features = {};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR AccelerationStructureFeatures = {};
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR RayTracingPipelineFeatures = {};
    VkPhysicalDeviceRayQueryFeaturesKHR RayQueryFeatures = {};
    VkPhysicalDeviceMeshShaderFeaturesEXT MeshShaderFeatures = {};
    VkPhysicalDeviceDescriptorBufferFeaturesEXT DescriptorBufferFeatures = {};
    VkPhysicalDeviceFragmentShadingRateFeaturesKHR FragmentShadingRateFeatures = {};
    VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT GraphicsPipelineLibraryFeatures = {};
    VkPhysicalDeviceMemoryPriorityFeaturesEXT MemoryPriorityFeatures = {};
    VkPhysicalDevicePageableDeviceLocalMemoryFeaturesEXT PageableDeviceLocalMemoryFeatures = {};

#if defined(LUDUS_RHI_DEVICE_DEBUG_FEATURES_PROFILE)
    VkPhysicalDevicePerformanceQueryFeaturesKHR PerformanceQueryFeatures = {};
#endif

#if defined(LUDUS_RHI_DEVICE_DEBUG_FEATURES)
    VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR PipelineExecutablePropertiesFeatures = {};
    VkPhysicalDeviceDeviceMemoryReportFeaturesEXT DeviceMemoryReportFeatures = {};
    VkPhysicalDeviceFaultFeaturesEXT FaultFeatures = {};
#endif
};

struct GpuInfo final
{
    float Score = 0.0f;
    VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties2 Properties2 = {};
    VkPhysicalDeviceVulkan11Properties Vulkan11Properties = {};
    VkPhysicalDeviceVulkan12Properties Vulkan12Properties = {};
    VkPhysicalDeviceVulkan13Properties Vulkan13Properties = {};
    VkPhysicalDeviceVulkan14Properties Vulkan14Properties = {};
    VkPhysicalDeviceAccelerationStructurePropertiesKHR AccelerationStructureProperties = {};
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR RayTracingPipelineProperties = {};
    Array<QueueFamilyInfo> QueueFamilyInfo = {};
    VkPhysicalDeviceMemoryProperties2 MemoryProperties2 = {};
    VkPhysicalDeviceMemoryBudgetPropertiesEXT MemoryBudgetProperties = {};
    DeviceInfo DeviceInfo = {};
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
[[nodiscard]] static bool initializeGpuInfos(VulkanInfo& inoutVulkanInfo) noexcept;
[[nodiscard]] static bool initializeGpuInfo(GpuInfo& inoutGpuInfo, const VulkanInfo& vulkanInfo) noexcept;
[[nodiscard]] static bool initializeDeviceInfo(DeviceInfo& inoutDeviceInfo, const GpuInfo& gpuInfo, const VulkanInfo& vulkanInfo) noexcept;

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

    if (!initializeGpuInfos(vulkanInfo))
    {
        return false;
    }

    GpuInfo& mainGpuInfo = vulkanInfo.GpuInfos[0];
    if (!initializeDeviceInfo(mainGpuInfo.DeviceInfo, mainGpuInfo, vulkanInfo))
    {
        return false;
    }
    volkLoadDevice(mainGpuInfo.DeviceInfo.Device);

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

bool initializeGpuInfos(VulkanInfo& inoutVulkanInfo) noexcept
{
    VkResult vr = VK_SUCCESS;
    if(inoutVulkanInfo.Instance == VK_NULL_HANDLE)
    {
        LUDUS_ASSERT(false, "Vulkan instance is not initialized.");
        return false;
    }

    uint32 gpuCount = 0;
    vr = vkEnumeratePhysicalDevices(inoutVulkanInfo.Instance, &gpuCount, nullptr);
    if (vr != VK_SUCCESS)
    {
        LUDUS_ASSERT(false, "Failed to enumerate Vulkan physical devices.");
        return false;
    }

    Array<VkPhysicalDevice> gpus(gpuCount);
    vr = vkEnumeratePhysicalDevices(inoutVulkanInfo.Instance, &gpuCount, gpus.GetData());
    if (vr != VK_SUCCESS)
    {
        LUDUS_ASSERT(false, "Failed to enumerate Vulkan physical devices.");
        return false;
    }

    Array<GpuInfo> gpuInfos;
    gpuInfos.EnsureCapacity(gpuCount);
    for (uint32 i = 0; i < gpuCount; ++i)
    {
        GpuInfo gpuInfo{ .PhysicalDevice = gpus[i] };
        const bool isInitialized = initializeGpuInfo(gpuInfo, inoutVulkanInfo);
        if(isInitialized == true)
        {
            gpuInfos.Add(gpuInfo);
        }
    }

    // TODO: Sort GPU infos based on its score

    inoutVulkanInfo.GpuInfos = gpuInfos;
    return true;
}

bool initializeGpuInfo(GpuInfo& inoutGpuInfo, const VulkanInfo& vulkanInfo) noexcept
{
    void* pNext = nullptr;
    if(vulkanInfo.ApiVersion >= VK_API_VERSION_1_2)
    {
        inoutGpuInfo.Vulkan11Properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES;
        inoutGpuInfo.Vulkan11Properties.pNext = pNext;
        pNext = &inoutGpuInfo.Vulkan11Properties;

        inoutGpuInfo.Vulkan12Properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES;
        inoutGpuInfo.Vulkan12Properties.pNext = pNext;
        pNext = &inoutGpuInfo.Vulkan12Properties;
    };
    if(vulkanInfo.ApiVersion >= VK_API_VERSION_1_3)
    {
        inoutGpuInfo.Vulkan13Properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES;
        inoutGpuInfo.Vulkan13Properties.pNext = pNext;
        pNext = &inoutGpuInfo.Vulkan13Properties;
    }
    if(vulkanInfo.ApiVersion >= VK_API_VERSION_1_4)
    {
        inoutGpuInfo.Vulkan14Properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_PROPERTIES;
        inoutGpuInfo.Vulkan14Properties.pNext = pNext;
        pNext = &inoutGpuInfo.Vulkan14Properties;
    }
    inoutGpuInfo.AccelerationStructureProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
    inoutGpuInfo.AccelerationStructureProperties.pNext = pNext;
    pNext = &inoutGpuInfo.AccelerationStructureProperties;
    inoutGpuInfo.RayTracingPipelineProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
    inoutGpuInfo.RayTracingPipelineProperties.pNext = pNext;
    pNext = &inoutGpuInfo.RayTracingPipelineProperties;

    inoutGpuInfo.Properties2 =
    {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = pNext,
        .properties = {}
    };
    vkGetPhysicalDeviceProperties2(inoutGpuInfo.PhysicalDevice, &inoutGpuInfo.Properties2);

    LUDUS_LOG_INFO(LOG_RHI,
                    "Found Vulkan physical device: {}",
                    inoutGpuInfo.Properties2.properties.deviceName);

    uint32 queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties2(inoutGpuInfo.PhysicalDevice, &queueFamilyCount, nullptr);
    Array<VkQueueFamilyProperties2> queueFamilyProperties(queueFamilyCount, VkQueueFamilyProperties2{ .sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2, .pNext = nullptr });
    Array<VkQueueFamilyGlobalPriorityProperties> queueFamilyGlobalPriorityProperties(queueFamilyCount, VkQueueFamilyGlobalPriorityProperties{ .sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_GLOBAL_PRIORITY_PROPERTIES_KHR, .pNext = nullptr });
    if(vulkanInfo.ApiVersion >= VK_API_VERSION_1_4)
    {
        for(uint32 i = 0; i < queueFamilyCount; ++i)
        {
            queueFamilyProperties[i].pNext = &queueFamilyGlobalPriorityProperties[i];
        }
    }
    vkGetPhysicalDeviceQueueFamilyProperties2(inoutGpuInfo.PhysicalDevice, &queueFamilyCount, queueFamilyProperties.GetData());

    inoutGpuInfo.QueueFamilyInfo.EnsureCapacity(queueFamilyCount);
    for(uint32 i = 0; i < queueFamilyCount; ++i)
    {
        QueueFamilyInfo queueFamilyInfo
        {
            .Properties = queueFamilyProperties[i],
            .GlobalPriorityProperties = queueFamilyGlobalPriorityProperties[i]
        };

        inoutGpuInfo.QueueFamilyInfo.Add(queueFamilyInfo);
    }

    // Requirement Check
    bool hasGraphicsQueue = false;
    bool hasComputeQueue = false;
    bool hasTransferQueue = false;
    for(QueueFamilyInfo &queueFamilyInfo : inoutGpuInfo.QueueFamilyInfo)
    {
        if(queueFamilyInfo.Properties.queueFamilyProperties.queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            hasGraphicsQueue = true;
        }
        if(queueFamilyInfo.Properties.queueFamilyProperties.queueFlags & VK_QUEUE_COMPUTE_BIT)
        {
            hasComputeQueue = true;
        }
        if(queueFamilyInfo.Properties.queueFamilyProperties.queueFlags & VK_QUEUE_TRANSFER_BIT)
        {
            hasTransferQueue = true;
        }
    }

    if(!hasGraphicsQueue)
    {
        LUDUS_LOG_WARN(LOG_RHI, "No graphics queue found on the GPU {}. Skipping.", inoutGpuInfo.Properties2.properties.deviceName);
        return false;
    }
    if(!hasComputeQueue)
    {
        LUDUS_LOG_WARN(LOG_RHI, "No compute queue found on the GPU {}. Skipping.", inoutGpuInfo.Properties2.properties.deviceName);
        return false;
    }
    if(!hasTransferQueue)
    {
        LUDUS_LOG_WARN(LOG_RHI, "No transfer queue found on the GPU {}. Skipping.", inoutGpuInfo.Properties2.properties.deviceName);
        return false;
    }

    float deviceTypeScore = 0.0f;
    switch(inoutGpuInfo.Properties2.properties.deviceType)
    {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
            deviceTypeScore = 1.0f;
            break;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
            deviceTypeScore = 0.5f;
            break;
        default:
            LUDUS_LOG_WARN(LOG_RHI, "The GPU {} is not a discrete or integrated GPU. Skipping.", inoutGpuInfo.Properties2.properties.deviceName);
            return false;
    }

    pNext = nullptr;
    inoutGpuInfo.MemoryBudgetProperties =
    {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT,
        .pNext = nullptr
    };
    pNext = &inoutGpuInfo.MemoryBudgetProperties;
    inoutGpuInfo.MemoryProperties2 =
    {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2,
        .pNext = pNext
    };
    vkGetPhysicalDeviceMemoryProperties2(inoutGpuInfo.PhysicalDevice, &inoutGpuInfo.MemoryProperties2);

    float memoryScore = 0.0f;
    VkDeviceSize totalMemoryBudget = 0;
    for(uint32 i = 0; i < inoutGpuInfo.MemoryProperties2.memoryProperties.memoryHeapCount; ++i)
    {
        totalMemoryBudget += inoutGpuInfo.MemoryBudgetProperties.heapBudget[i];
    }

    if(totalMemoryBudget == 0)
    {
        LUDUS_LOG_WARN(LOG_RHI, "The GPU {} has no memory budget. Skipping.", inoutGpuInfo.Properties2.properties.deviceName);
        return false;
    }
    else if(totalMemoryBudget < (1024ull * 1024 * 1024)) // Less than 1GB
    {
        memoryScore = 0.1f;
    }
    else if(totalMemoryBudget < (4ull * 1024 * 1024 * 1024)) // Less than 4GB
    {
        memoryScore = 0.3f;
    }
    else if(totalMemoryBudget < (6ull * 1024 * 1024 * 1024)) // Less than 6GB
    {
        memoryScore = 0.5f;
    }
    else if(totalMemoryBudget < (8ull * 1024 * 1024 * 1024)) // Less than 8GB
    {
        memoryScore = 0.8f;
    }
    else if(totalMemoryBudget < (12ull * 1024 * 1024 * 1024)) // Less than 12GB
    {
        memoryScore = 0.9f;
    }
    else if(totalMemoryBudget < (16ull * 1024 * 1024 * 1024)) // Less than 16GB
    {
        memoryScore = 1.0f;
    }
    else // 16GB or more
    {
        memoryScore = 1.0f;
    }

    float queueTopologyScore = 0.0f;
    if(queueFamilyCount < 3)
    {
        queueTopologyScore = 0.0f; // Penalize if there are less than 3 queue families
    }
    else
    {
        Array<bool> graphicsQueueFamilies(queueFamilyCount);
        Array<bool> computeQueueFamilies(queueFamilyCount);
        Array<bool> transferQueueFamilies(queueFamilyCount);
        for(uint32 i = 0; i < queueFamilyCount; ++i)
        {
            QueueFamilyInfo &queueFamilyInfo = inoutGpuInfo.QueueFamilyInfo[i];
            if(queueFamilyInfo.Properties.queueFamilyProperties.queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                graphicsQueueFamilies[i] = true;
            }

            if(queueFamilyInfo.Properties.queueFamilyProperties.queueFlags & VK_QUEUE_COMPUTE_BIT)
            {
                computeQueueFamilies[i] = true;
            }

            if(queueFamilyInfo.Properties.queueFamilyProperties.queueFlags & VK_QUEUE_TRANSFER_BIT)
            {
                transferQueueFamilies[i] = true;
            }
        }

        uint32 dedicatedGraphicsQueueFamilyIndex = UINT32_MAX;
        uint32 dedicatedComputeQueueFamilyIndex = UINT32_MAX;
        uint32 dedicatedTransferQueueFamilyIndex = UINT32_MAX;
        for(uint32 i = 0; i < queueFamilyCount; ++i)
        {
            if(graphicsQueueFamilies[i] && !computeQueueFamilies[i] && !transferQueueFamilies[i])
            {
                dedicatedGraphicsQueueFamilyIndex = i;
            }

            if(computeQueueFamilies[i] && !graphicsQueueFamilies[i] && !transferQueueFamilies[i])
            {
                dedicatedComputeQueueFamilyIndex = i;
            }

            if(transferQueueFamilies[i] && !graphicsQueueFamilies[i] && !computeQueueFamilies[i])
            {
                dedicatedTransferQueueFamilyIndex = i;
            }
        }

        for(uint32 i = 0; i < queueFamilyCount; ++i)
        {
            if(dedicatedGraphicsQueueFamilyIndex == i
                || dedicatedComputeQueueFamilyIndex == i
                || dedicatedTransferQueueFamilyIndex == i)
            {
                continue; // Skip this iteration if the queue family is already dedicated
            }

            if(dedicatedGraphicsQueueFamilyIndex == UINT32_MAX && graphicsQueueFamilies[i])
            {
                dedicatedGraphicsQueueFamilyIndex = i;
            }

            if(dedicatedComputeQueueFamilyIndex == UINT32_MAX && computeQueueFamilies[i])
            {
                dedicatedComputeQueueFamilyIndex = i;
            }

            if(dedicatedTransferQueueFamilyIndex == UINT32_MAX && transferQueueFamilies[i])
            {
                dedicatedTransferQueueFamilyIndex = i;
            }
        }

        if(dedicatedGraphicsQueueFamilyIndex != UINT32_MAX)
        {
            queueTopologyScore += 1.0f; // Assign a score for having a dedicated graphics queue
        }

        if(dedicatedComputeQueueFamilyIndex != UINT32_MAX)
        {
            queueTopologyScore += 1.0f; // Assign a score for having a dedicated compute queue
        }

        if(dedicatedTransferQueueFamilyIndex != UINT32_MAX)
        {
            queueTopologyScore += 1.0f; // Assign a score for having a dedicated transfer queue
        }
        queueTopologyScore /= 3.0f; // Normalize the queue topology score to a maximum of 1.0f
    }

    inoutGpuInfo.Score = deviceTypeScore + memoryScore + queueTopologyScore;

    for(uint32 i = 0; i < queueFamilyCount; ++i)
    {
        QueueFamilyInfo &queueFamilyInfo = inoutGpuInfo.QueueFamilyInfo[i];

        if(vkEnumeratePhysicalDeviceQueueFamilyPerformanceQueryCountersKHR != nullptr)
        {
            uint32 performanceQueryCounterCount = 0;
            vkEnumeratePhysicalDeviceQueueFamilyPerformanceQueryCountersKHR(inoutGpuInfo.PhysicalDevice, i, &performanceQueryCounterCount, nullptr, nullptr);
            queueFamilyInfo.PerformanceCounters.Resize(performanceQueryCounterCount);
            queueFamilyInfo.PerformanceCounterDescriptions.Resize(performanceQueryCounterCount);
            vkEnumeratePhysicalDeviceQueueFamilyPerformanceQueryCountersKHR(inoutGpuInfo.PhysicalDevice, i, &performanceQueryCounterCount, queueFamilyInfo.PerformanceCounters.GetData(), queueFamilyInfo.PerformanceCounterDescriptions.GetData());
        }
    }

    return true;
}

bool initializeDeviceInfo(DeviceInfo& inoutDeviceInfo, const GpuInfo& gpuInfo, const VulkanInfo& vulkanInfo) noexcept
{
    VkResult vr = VK_SUCCESS;

    uint32 extensionPropertyCount = 0;
    vr = vkEnumerateDeviceExtensionProperties(gpuInfo.PhysicalDevice, nullptr, &extensionPropertyCount, nullptr);
    Array<VkExtensionProperties> extensionProperties(extensionPropertyCount);
    vr = vkEnumerateDeviceExtensionProperties(gpuInfo.PhysicalDevice, nullptr, &extensionPropertyCount, extensionProperties.GetData());

    Array<VkExtensionProperties> extensionsToRequest;
    if(vulkanInfo.ApiVersion < VK_API_VERSION_1_4)
    {
        extensionsToRequest.Add({ .extensionName = VK_KHR_GLOBAL_PRIORITY_EXTENSION_NAME });
    }
    extensionsToRequest.Add({ .extensionName = VK_KHR_PERFORMANCE_QUERY_EXTENSION_NAME });
    extensionsToRequest.Add({ .extensionName = VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME });
    extensionsToRequest.Add({ .extensionName = VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME });
    extensionsToRequest.Add({ .extensionName = VK_KHR_RAY_QUERY_EXTENSION_NAME });
    extensionsToRequest.Add({ .extensionName = VK_EXT_MEMORY_BUDGET_EXTENSION_NAME });
    extensionsToRequest.Add({ .extensionName = VK_EXT_MESH_SHADER_EXTENSION_NAME });
    extensionsToRequest.Add({ .extensionName = VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME });
    extensionsToRequest.Add({ .extensionName = VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME });
    extensionsToRequest.Add({ .extensionName = VK_EXT_GRAPHICS_PIPELINE_LIBRARY_EXTENSION_NAME });
    extensionsToRequest.Add({ .extensionName = VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME });
    extensionsToRequest.Add({ .extensionName = VK_EXT_PAGEABLE_DEVICE_LOCAL_MEMORY_EXTENSION_NAME });

    for(uint32 i = 0; i < extensionPropertyCount; ++i)
    {
        LUDUS_LOG_INFO(LOG_RHI, "Found device extension: {}", extensionProperties[i].extensionName);
    }

    uint32 extensionsToRequestCount = static_cast<uint32>(extensionsToRequest.GetSize());
    Array<const char*> requestedExtensions;
    for(uint32 i = 0; i < extensionsToRequestCount; ++i)
    {
        if(std::find_if(extensionProperties.GetData(), extensionProperties.GetData() + extensionPropertyCount, [&](const VkExtensionProperties& ext) { return strcmp(ext.extensionName, extensionsToRequest[i].extensionName) == 0; }) != extensionProperties.GetData() + extensionPropertyCount)
        {
            LUDUS_LOG_INFO(LOG_RHI, "Requesting device extension: {}", extensionsToRequest[i].extensionName);
            requestedExtensions.Add(extensionsToRequest[i].extensionName);
        }
        else
        {
            LUDUS_LOG_WARN(LOG_RHI, "Device extension not found: {}", extensionsToRequest[i].extensionName);
        }
    }

    void* pNext = nullptr;
    inoutDeviceInfo.Features2 =
    {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = nullptr,
        .features = {}
    };
    pNext = &inoutDeviceInfo.Features2;
    if(vulkanInfo.ApiVersion >= VK_API_VERSION_1_1)
    {
        inoutDeviceInfo.Vulkan11Features =
        {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
            .pNext = pNext,
        };
        pNext = &inoutDeviceInfo.Vulkan11Features;
    }
    if(vulkanInfo.ApiVersion >= VK_API_VERSION_1_2)
    {
        inoutDeviceInfo.Vulkan12Features =
        {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
            .pNext = pNext,
        };
        pNext = &inoutDeviceInfo.Vulkan12Features;
    }
    if(vulkanInfo.ApiVersion >= VK_API_VERSION_1_3)
    {
        inoutDeviceInfo.Vulkan13Features =
        {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
            .pNext = pNext,
        };
        pNext = &inoutDeviceInfo.Vulkan13Features;
    }
    if(vulkanInfo.ApiVersion >= VK_API_VERSION_1_4)
    {
        inoutDeviceInfo.Vulkan14Features =
        {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
            .pNext = pNext,
        };
        pNext = &inoutDeviceInfo.Vulkan14Features;
    }
    inoutDeviceInfo.AccelerationStructureFeatures =
    {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
        .pNext = pNext,
    };
    pNext = &inoutDeviceInfo.AccelerationStructureFeatures;
    inoutDeviceInfo.RayTracingPipelineFeatures =
    {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR,
        .pNext = pNext,
    };
    pNext = &inoutDeviceInfo.RayTracingPipelineFeatures;
    inoutDeviceInfo.RayQueryFeatures =
    {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR,
        .pNext = pNext,
    };
    pNext = &inoutDeviceInfo.RayQueryFeatures;
    inoutDeviceInfo.MeshShaderFeatures =
    {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT,
        .pNext = pNext,
    };
    pNext = &inoutDeviceInfo.MeshShaderFeatures;
    inoutDeviceInfo.DescriptorBufferFeatures =
    {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT,
        .pNext = pNext,
    };
    pNext = &inoutDeviceInfo.DescriptorBufferFeatures;
    inoutDeviceInfo.FragmentShadingRateFeatures =
    {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR,
        .pNext = pNext,
    };
    pNext = &inoutDeviceInfo.FragmentShadingRateFeatures;

    inoutDeviceInfo.GraphicsPipelineLibraryFeatures =
    {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GRAPHICS_PIPELINE_LIBRARY_FEATURES_EXT,
        .pNext = pNext,
    };
    pNext = &inoutDeviceInfo.GraphicsPipelineLibraryFeatures;

    inoutDeviceInfo.MemoryPriorityFeatures =
    {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PRIORITY_FEATURES_EXT,
        .pNext = pNext,
    };
    pNext = &inoutDeviceInfo.MemoryPriorityFeatures;

    inoutDeviceInfo.PageableDeviceLocalMemoryFeatures =
    {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PAGEABLE_DEVICE_LOCAL_MEMORY_FEATURES_EXT,
        .pNext = pNext,
    };
    pNext = &inoutDeviceInfo.PageableDeviceLocalMemoryFeatures;

#if defined(LUDUS_RHI_DEVICE_DEBUG_FEATURES_PROFILE)
    inoutDeviceInfo.PerformanceQueryFeatures =
    {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PERFORMANCE_QUERY_FEATURES_KHR,
        .pNext = pNext,
    };
    pNext = &inoutDeviceInfo.PerformanceQueryFeatures;
#endif

#if defined(LUDUS_RHI_DEVICE_DEBUG_FEATURES)
    inoutDeviceInfo.PipelineExecutablePropertiesFeatures =
    {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR,
        .pNext = pNext,
    };
    pNext = &inoutDeviceInfo.PipelineExecutablePropertiesFeatures;

    inoutDeviceInfo.DeviceMemoryReportFeatures =
    {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_MEMORY_REPORT_FEATURES_EXT,
        .pNext = pNext,
    };
    pNext = &inoutDeviceInfo.DeviceMemoryReportFeatures;

    inoutDeviceInfo.FaultFeatures =
    {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT,
        .pNext = pNext,
    };
    pNext = &inoutDeviceInfo.FaultFeatures;
#endif

    const VkDeviceCreateInfo deviceCreateInfo
    {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = pNext,
        .flags = 0,
        .queueCreateInfoCount = 0,
        .pQueueCreateInfos = nullptr,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = static_cast<uint32>(requestedExtensions.GetSize()),
        .ppEnabledExtensionNames = requestedExtensions.GetData(),
        .pEnabledFeatures = nullptr
    };
    vr = vkCreateDevice(gpuInfo.PhysicalDevice, &deviceCreateInfo, nullptr, &inoutDeviceInfo.Device);
    if(vr != VK_SUCCESS)
    {
        LUDUS_ASSERT(vr == VK_SUCCESS, "Failed to create Vulkan device");
        return false;
    }
    return true;
}
} // namespace ludus::graphics::rhi
