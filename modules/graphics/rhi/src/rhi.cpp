#include <volk.h>

#include "internal/vulkan_diagnostics.h"

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
#include <ludus/platform/native_window.h>
#include <vulkan/vulkan_core.h>

#if defined(LUDUS_BUILD_DEBUG) || defined(LUDUS_BUILD_DEVELOPMENT)
#    define LUDUS_RHI_ENABLE_VALIDATION_LAYER
#endif

using namespace ludus::foundation;

namespace ludus::graphics::rhi
{
inline constexpr logging::LogCategory LOG_RHI{"RHI"};

struct FenceInfo final
{
    VkFence Fence = VK_NULL_HANDLE;
};

struct SemaphoreInfo final
{
    VkSemaphore Semaphore = VK_NULL_HANDLE;
};

struct QueueInfo final
{
    VkQueue Queue = VK_NULL_HANDLE;
    uint32 WaitSemaphoreIndex = UINT32_MAX;
};

struct QueueFamilyInfo final
{
    uint32 Index = UINT32_MAX;
    VkQueueFamilyProperties2 Properties = {};
    VkQueueFamilyGlobalPriorityProperties GlobalPriorityProperties = {};
    Array<VkPerformanceCounterKHR> PerformanceCounters;
    Array<VkPerformanceCounterDescriptionKHR> PerformanceCounterDescriptions;
    Array<QueueInfo> QueueInfos;
};

#if defined(LUDUS_BUILD_DEBUG) || defined(LUDUS_BUILD_DEVELOPMENT) || defined(LUDUS_BUILD_PROFILE)
#    define LUDUS_RHI_DEVICE_DEBUG_FEATURES_PROFILE
#    if defined(LUDUS_BUILD_PROFILE) == false
#        define LUDUS_RHI_DEVICE_DEBUG_FEATURES
#    endif
#endif

struct CommandBufferInfo final
{
    VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
};

struct CommandPoolInfo final
{
    VkCommandPool CommandPool = VK_NULL_HANDLE;
    Array<CommandBufferInfo> CommandBufferInfos;
};

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

    Array<CommandPoolInfo> CommandPoolInfos;
    Array<FenceInfo> FenceInfos;
    Array<SemaphoreInfo> SemaphoreInfos;

#if defined(LUDUS_RHI_DEVICE_DEBUG_FEATURES_PROFILE)
    VkPhysicalDevicePerformanceQueryFeaturesKHR PerformanceQueryFeatures = {};
#endif

#if defined(LUDUS_RHI_DEVICE_DEBUG_FEATURES)
    VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR PipelineExecutablePropertiesFeatures = {};
    VkPhysicalDeviceDeviceMemoryReportFeaturesEXT DeviceMemoryReportFeatures = {};
    VkPhysicalDeviceFaultFeaturesEXT FaultFeatures = {};
#endif

    [[nodiscard]] LUDUS_INLINE const SemaphoreInfo& GetSemaphoreInfo(uint32 index) const
    {
        LUDUS_ASSERT(index < SemaphoreInfos.GetSize(), "Index out of bounds");
        return SemaphoreInfos[index];
    }
};

struct GpuInfo final
{
    float32 Score = 0.0f;
    VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties2 Properties2 = {};
    VkPhysicalDeviceVulkan11Properties Vulkan11Properties = {};
    VkPhysicalDeviceVulkan12Properties Vulkan12Properties = {};
    VkPhysicalDeviceVulkan13Properties Vulkan13Properties = {};
    VkPhysicalDeviceVulkan14Properties Vulkan14Properties = {};
    VkPhysicalDeviceAccelerationStructurePropertiesKHR AccelerationStructureProperties = {};
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR RayTracingPipelineProperties = {};
    Array<QueueFamilyInfo> QueueFamilyInfo;
    VkPhysicalDeviceMemoryProperties2 MemoryProperties2 = {};
    VkPhysicalDeviceMemoryBudgetPropertiesEXT MemoryBudgetProperties = {};
    DeviceInfo DeviceInfo = {};
    uint32 GraphicsQueueFamilyIndex = UINT32_MAX;
    uint32 PresentQueueFamilyIndex = UINT32_MAX;
};

struct SurfaceInfo final
{
    VkSurfaceKHR Surface = VK_NULL_HANDLE;
};

struct ImageInfo final
{
    VkImage Image = VK_NULL_HANDLE;
    VkFormat ImageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D ImageExtent =
    {
        .width = 0,
        .height = 0,
    };
};

struct WindowSystemIntegrationInfo final
{
    QueueFamilyInfo* PresentQueueFamilyInfoOrNull = nullptr;
    WindowInfo WindowInfo = {};
    SurfaceInfo SurfaceInfo = {};
    VkSwapchainKHR Swapchain = VK_NULL_HANDLE;
    Array<ImageInfo> ImageInfos;
    Array<uint32> RenderFinishedSemaphoreIndices;
};

struct VulkanInfo final
{
    uint32 ApiVersion = 0;
    VkInstance Instance = VK_NULL_HANDLE;
    Array<GpuInfo> GpuInfos;
    WindowSystemIntegrationInfo WindowSystemIntegrationInfo = {};

    [[nodiscard]] LUDUS_INLINE const GpuInfo& GetPrimaryGpuInfo() const noexcept
    {
        LUDUS_ASSERT(!GpuInfos.IsEmpty(), "GpuInfos is empty.");
        return GpuInfos[0];
    }
};

struct FrameContextInfo final
{
    uint32 Index = 0;
    uint32 ImageIndex = 0;
    uint32 ImageAvailableSemaphoreIndex = 0;
    uint32 FenceIndex = UINT32_MAX;

    uint32 CommandPoolIndex = 0;
    uint32 CommandBufferIndex = 0;
};

struct RenderContextInfo final
{
    Array<FrameContextInfo> FrameContextInfos;
    Array<uint32> FrameToUpdateIndices;
    Array<uint32> PresentingImageIndices;

    [[nodiscard]] LUDUS_INLINE uint32 GetFrameToUpdateIndex() const noexcept
    {
        LUDUS_ASSERT(!FrameToUpdateIndices.IsEmpty(), "FrameToUpdateIndices is empty.");
        return FrameToUpdateIndices[0];
    }
    [[nodiscard]] LUDUS_INLINE FrameContextInfo& GetFrameContextInfoToUpdate() noexcept
    {
        LUDUS_ASSERT(!FrameContextInfos.IsEmpty(), "FrameContextInfos is empty.");
        return FrameContextInfos[GetFrameToUpdateIndex()];
    }
};

static VulkanInfo gVulkanInfo;
static RenderContextInfo gRenderContextInfo;

#if defined(LUDUS_RHI_ENABLE_VALIDATION_LAYER)
static VkBool32 DebugUtilsMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                            VkDebugUtilsMessageTypeFlagsEXT messageType,
                                            const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                            void* pUserData) noexcept;
#endif
[[nodiscard]] static bool initializeInstance(VulkanInfo& inoutVulkanInfo, const ApplicationInfo& appInfo) noexcept;
[[nodiscard]] static bool initializeGpuInfos(VulkanInfo& inoutVulkanInfo) noexcept;
[[nodiscard]] static bool initializeGpuInfo(GpuInfo& inoutGpuInfo, const VulkanInfo& vulkanInfo) noexcept;
[[nodiscard]] static bool
initializeDeviceInfo(DeviceInfo& inoutDeviceInfo, const GpuInfo& gpuInfo, const VulkanInfo& vulkanInfo) noexcept;
static void shutdownDevice(DeviceInfo& inoutDeviceInfo) noexcept;
static void initializeQueues(QueueFamilyInfo& inoutQueueFamilyInfo, const DeviceInfo& deviceInfo) noexcept;
[[nodiscard]] static bool initializeWindowSystemIntegration(WindowSystemIntegrationInfo& inoutWsiInfo,
                                                            const VulkanInfo& vulkanInfo) noexcept;
static void shutdownWindowSystemIntegration(WindowSystemIntegrationInfo& inoutWsiInfo,
                                            const VulkanInfo& vulkanInfo) noexcept;
[[nodiscard]] static bool initializeSwapchain(WindowSystemIntegrationInfo& inoutWsiInfo,
                                              DeviceInfo& deviceInfo) noexcept;
static void shutdownSwapchain(WindowSystemIntegrationInfo& inoutWsiInfo, const DeviceInfo& deviceInfo) noexcept;
[[nodiscard]] static bool initializeCommands(DeviceInfo& inoutDeviceInfo, const GpuInfo& gpuInfo) noexcept;
static void shutdownCommands(DeviceInfo& inoutDeviceInfo) noexcept;
[[nodiscard]] static bool initializeSemaphore(uint32& outSemaphoreIndex, DeviceInfo& inoutDeviceInfo) noexcept;
static void shutdownSemaphore(SemaphoreInfo& inoutSemaphoreInfo, DeviceInfo& inoutDeviceInfo) noexcept;
[[nodiscard]] static bool initializeFence(uint32& outFenceIndex, DeviceInfo& inoutDeviceInfo) noexcept;
static void shutdownFence(FenceInfo& inoutFenceInfo, DeviceInfo& inoutDeviceInfo) noexcept;

[[nodiscard]] static bool createCommandBufferInfo(uint32& outCommandBufferInfoIndex,
                                                  CommandPoolInfo& inoutCommandPoolInfo,
                                                  const DeviceInfo& deviceInfo) noexcept;

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
        LUDUS_LOG_ERROR(LOG_RHI, "Failed to initialize Volk.");
        return false;
    }

    VulkanInfo vulkanInfo = {};
    if (vkEnumerateInstanceVersion != nullptr)
    {
        vr = vkEnumerateInstanceVersion(&vulkanInfo.ApiVersion);
        if (vr != VK_SUCCESS)
        {
            LUDUS_LOG_ERROR(LOG_RHI, "Failed to enumerate Vulkan instance version.");
            volkFinalize();
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
        volkFinalize();
        return false;
    }
    volkLoadInstance(vulkanInfo.Instance);

    gVulkanInfo = Move(vulkanInfo);
    gIsInitialized = true;
    return true;
}

bool ConnectWindow(const WindowInfo& windowInfo) noexcept
{
    if (!gIsInitialized)
    {
        LUDUS_LOG_ERROR(LOG_RHI, "RHI is not initialized.");
        return false;
    }

    auto& wsi = gVulkanInfo.WindowSystemIntegrationInfo;
    if (wsi.SurfaceInfo.Surface != VK_NULL_HANDLE)
    {
        return wsi.WindowInfo.System == windowInfo.System && wsi.WindowInfo.Display == windowInfo.Display &&
               wsi.WindowInfo.Surface == windowInfo.Surface;
    }

    WindowSystemIntegrationInfo candidate =
    {
        .WindowInfo = windowInfo,
        .ImageInfos =
            {
                {
                    .ImageFormat = VK_FORMAT_B8G8R8A8_UNORM,
                    .ImageExtent =
                    {
                        .width = windowInfo.Width,
                        .height = windowInfo.Height,
                    },
                },
                {
                    .ImageFormat = VK_FORMAT_B8G8R8A8_UNORM,
                    .ImageExtent =
                    {
                        .width = windowInfo.Width,
                        .height = windowInfo.Height,
                    },
                },
            },
    };
    if (!initializeWindowSystemIntegration(candidate, gVulkanInfo))
    {
        return false;
    }
    if (!initializeGpuInfos(gVulkanInfo))
    {
        vkDestroySurfaceKHR(gVulkanInfo.Instance, candidate.SurfaceInfo.Surface, nullptr);
        return false;
    }

    for (GpuInfo& gpuInfo : gVulkanInfo.GpuInfos)
    {
        for (const QueueFamilyInfo& family : gpuInfo.QueueFamilyInfo)
        {
            if (family.Properties.queueFamilyProperties.queueCount == 0)
            {
                continue;
            }
            VkBool32 presentSupported = VK_FALSE;
            if (vkGetPhysicalDeviceSurfaceSupportKHR(gpuInfo.PhysicalDevice,
                                                     family.Index,
                                                     candidate.SurfaceInfo.Surface,
                                                     &presentSupported) != VK_SUCCESS)
            {
                continue;
            }
            const bool graphicsSupported =
                (family.Properties.queueFamilyProperties.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
            if (graphicsSupported)
            {
                gpuInfo.GraphicsQueueFamilyIndex = family.Index;
            }
            if (presentSupported == VK_TRUE)
            {
                gpuInfo.PresentQueueFamilyIndex = family.Index;
            }
            if (graphicsSupported && presentSupported == VK_TRUE)
            {
                break;
            }
        }
        if (gpuInfo.GraphicsQueueFamilyIndex == UINT32_MAX || gpuInfo.PresentQueueFamilyIndex == UINT32_MAX)
        {
            continue;
        }
        if (!initializeDeviceInfo(gpuInfo.DeviceInfo, gpuInfo, gVulkanInfo))
        {
            continue;
        }
        volkLoadDevice(gpuInfo.DeviceInfo.Device);
        for (QueueFamilyInfo& family : gpuInfo.QueueFamilyInfo)
        {
            initializeQueues(family, gpuInfo.DeviceInfo);
        }
        wsi = candidate;
        if (!initializeSwapchain(wsi, gpuInfo.DeviceInfo))
        {
            shutdownSwapchain(gVulkanInfo.WindowSystemIntegrationInfo, gpuInfo.DeviceInfo);
            continue;
        }

        const uint32 backBuffersCount = gVulkanInfo.WindowSystemIntegrationInfo.ImageInfos.GetSize();
        gRenderContextInfo.FrameContextInfos.EnsureCapacity(backBuffersCount);
        for (uint32 i = 0; i < backBuffersCount; ++i)
        {
            FrameContextInfo frameContextInfo =
            {
                .Index = i,
            };
            if (!initializeSemaphore(frameContextInfo.ImageAvailableSemaphoreIndex, gVulkanInfo.GpuInfos[0].DeviceInfo))
            {
                LUDUS_ASSERT_F(false, "Failed to initialize image available semaphore for frame index {}.", i);
                return false;
            }
            if (!initializeFence(frameContextInfo.FenceIndex, gVulkanInfo.GpuInfos[0].DeviceInfo))
            {
                LUDUS_ASSERT_F(false, "Failed to initialize fence for frame index {}.", i);
                return false;
            }
            gRenderContextInfo.FrameContextInfos.Add(frameContextInfo);
            gRenderContextInfo.FrameToUpdateIndices.Add(i);
        }
        return true;
    }

    shutdownWindowSystemIntegration(gVulkanInfo.WindowSystemIntegrationInfo, gVulkanInfo);
    LUDUS_LOG_ERROR(LOG_RHI, "No suitable Vulkan device supports presentation to this window.");
    return false;
}

bool InitializeRendering() noexcept
{
    if (!gIsInitialized)
    {
        LUDUS_LOG_ERROR(LOG_RHI, "RHI is not initialized.");
        return false;
    }

    // TODO: magic number for gpu info
    if (!initializeCommands(gVulkanInfo.GpuInfos[0].DeviceInfo, gVulkanInfo.GetPrimaryGpuInfo()))
    {
        LUDUS_LOG_ERROR(LOG_RHI, "Failed to initialize rendering commands.");
        return false;
    }

    DeviceInfo& deviceInfo = gVulkanInfo.GpuInfos[0].DeviceInfo;
    const uint32 swapchainImagesCount =
        static_cast<uint32>(gVulkanInfo.WindowSystemIntegrationInfo.ImageInfos.GetSize());
    constexpr uint32 COMMAND_POOL_INDEX = 0;
    for (uint32 i = 0; i < swapchainImagesCount; ++i)
    {
        if (!createCommandBufferInfo(gRenderContextInfo.FrameContextInfos[i].CommandBufferIndex,
                                     deviceInfo.CommandPoolInfos[COMMAND_POOL_INDEX],
                                     deviceInfo))
        {
            LUDUS_LOG_ERROR(LOG_RHI, "Failed to create command buffer info for frame index {}.", i);
            return false;
        }
        gRenderContextInfo.FrameContextInfos[i].CommandPoolIndex = COMMAND_POOL_INDEX;
    }

    return true;
}

void ShutdownRendering() noexcept
{
    if (gIsInitialized)
    {
        shutdownCommands(gVulkanInfo.GpuInfos[0].DeviceInfo);
    }
}

void Shutdown() noexcept
{
    if (gIsInitialized || gVulkanInfo.Instance != VK_NULL_HANDLE)
    {
        LUDUS_ASSERT(gIsInitialized, "RHI is not initialized.");
        LUDUS_ASSERT(gVulkanInfo.Instance != VK_NULL_HANDLE, "Vulkan instance is not valid.");

        shutdownSwapchain(gVulkanInfo.WindowSystemIntegrationInfo, gVulkanInfo.GetPrimaryGpuInfo().DeviceInfo);
        shutdownWindowSystemIntegration(gVulkanInfo.WindowSystemIntegrationInfo, gVulkanInfo);

        for (GpuInfo& gpuInfo : gVulkanInfo.GpuInfos)
        {
            shutdownDevice(gpuInfo.DeviceInfo);
        }

        if (gVulkanInfo.WindowSystemIntegrationInfo.SurfaceInfo.Surface != VK_NULL_HANDLE)
        {
            vkDestroySurfaceKHR(gVulkanInfo.Instance,
                                gVulkanInfo.WindowSystemIntegrationInfo.SurfaceInfo.Surface,
                                nullptr);
        }
        vkDestroyInstance(gVulkanInfo.Instance, nullptr);
        volkFinalize();
        gVulkanInfo = {};
        gIsInitialized = false;
    }
}

bool BeginFrame() noexcept
{
    VkResult vr = VK_SUCCESS;

    FrameContextInfo& frameContextInfo = gRenderContextInfo.GetFrameContextInfoToUpdate();

    vr = vkWaitForFences(gVulkanInfo.GetPrimaryGpuInfo().DeviceInfo.Device,
                         1,
                         &gVulkanInfo.GetPrimaryGpuInfo().DeviceInfo.FenceInfos[frameContextInfo.FenceIndex].Fence,
                         VK_TRUE,
                         UINT64_MAX);
    if (vr != VK_SUCCESS)
    {
        LUDUS_ASSERT_F(false, "Failed to wait for fence: {}", vr);
        return false;
    }
    vr = vkResetFences(gVulkanInfo.GetPrimaryGpuInfo().DeviceInfo.Device,
                       1,
                       &gVulkanInfo.GetPrimaryGpuInfo().DeviceInfo.FenceInfos[frameContextInfo.FenceIndex].Fence);
    if (vr != VK_SUCCESS)
    {
        LUDUS_ASSERT_F(false, "Failed to reset fence: {}", vr);
        return false;
    }

    const DeviceInfo& deviceInfo = gVulkanInfo.GetPrimaryGpuInfo().DeviceInfo;
    WindowSystemIntegrationInfo& wsiInfo = gVulkanInfo.WindowSystemIntegrationInfo;
    const SemaphoreInfo& imageAvailableSemaphoreInfo =
        deviceInfo.GetSemaphoreInfo(frameContextInfo.ImageAvailableSemaphoreIndex);
    if (gVulkanInfo.ApiVersion >= VK_API_VERSION_1_1)
    {
        const VkAcquireNextImageInfoKHR acquireNextImageInfo =
        {
            .sType = VK_STRUCTURE_TYPE_ACQUIRE_NEXT_IMAGE_INFO_KHR,
            .pNext = nullptr,
            .swapchain = wsiInfo.Swapchain,
            .timeout = UINT64_MAX,
            .semaphore = imageAvailableSemaphoreInfo.Semaphore,
            .fence = VK_NULL_HANDLE,
            .deviceMask = 0,
        };
        vr = vkAcquireNextImage2KHR(deviceInfo.Device, &acquireNextImageInfo, &frameContextInfo.ImageIndex);
    }
    else
    {
        vr = vkAcquireNextImageKHR(deviceInfo.Device,
                                   wsiInfo.Swapchain,
                                   UINT64_MAX,
                                   imageAvailableSemaphoreInfo.Semaphore,
                                   VK_NULL_HANDLE,
                                   &frameContextInfo.ImageIndex);
    }

    const CommandBufferInfo& commandBufferInfo = gVulkanInfo.GetPrimaryGpuInfo()
                                                     .DeviceInfo.CommandPoolInfos[frameContextInfo.CommandPoolIndex]
                                                     .CommandBufferInfos[frameContextInfo.CommandBufferIndex];
    vr = vkResetCommandBuffer(commandBufferInfo.CommandBuffer, 0);
    if (vr != VK_SUCCESS)
    {
        LUDUS_ASSERT_F(false, "Failed to reset command buffer: {}", vr);
        return false;
    }

    const VkCommandBufferBeginInfo commandBufferBeginInfo =
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr,
    };
    vr = vkBeginCommandBuffer(commandBufferInfo.CommandBuffer, &commandBufferBeginInfo);
    if (vr != VK_SUCCESS)
    {
        LUDUS_ASSERT_F(false, "Failed to begin command buffer: {}", vr);
        return false;
    }

    return vr == VK_SUCCESS;
}

bool EndFrame() noexcept
{
    VkResult vr = VK_SUCCESS;

    FrameContextInfo& frameContextInfo = gRenderContextInfo.GetFrameContextInfoToUpdate();

    const CommandBufferInfo& commandBufferInfo =
        gVulkanInfo.GetPrimaryGpuInfo().DeviceInfo.CommandPoolInfos[0].CommandBufferInfos[0];
    vr = vkEndCommandBuffer(commandBufferInfo.CommandBuffer);
    if (vr != VK_SUCCESS)
    {
        LUDUS_ASSERT_F(false, "Failed to end command buffer: {}", vr);
        return false;
    }

    const QueueFamilyInfo& graphicsQueueFamilyInfo =
        gVulkanInfo.GetPrimaryGpuInfo().QueueFamilyInfo[gVulkanInfo.GetPrimaryGpuInfo().GraphicsQueueFamilyIndex];
    const VkCommandBufferSubmitInfo commandBufferSubmitInfo =
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .pNext = nullptr,
        .commandBuffer = commandBufferInfo.CommandBuffer,
        .deviceMask = 0,
    };
    const DeviceInfo& deviceInfo = gVulkanInfo.GetPrimaryGpuInfo().DeviceInfo;
    const SemaphoreInfo& imageAvailableSemaphoreInfo =
        deviceInfo.GetSemaphoreInfo(frameContextInfo.ImageAvailableSemaphoreIndex);
    const VkSemaphoreSubmitInfo waitSemaphoreSubmitInfo =
    {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .pNext = nullptr,
        .semaphore = imageAvailableSemaphoreInfo.Semaphore,
        .value = 0,
        .stageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    };
    WindowSystemIntegrationInfo& wsiInfo = gVulkanInfo.WindowSystemIntegrationInfo;
    const VkSemaphore renderFinishedSemaphore =
        deviceInfo.GetSemaphoreInfo(wsiInfo.RenderFinishedSemaphoreIndices[frameContextInfo.ImageIndex]).Semaphore;
    const VkSemaphoreSubmitInfo signalSemaphoreSubmitInfo =
    {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .pNext = nullptr,
        .semaphore = renderFinishedSemaphore,
        .value = 0,
        .stageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    };
    const VkSubmitInfo2 submitInfo2 =
    {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .pNext = nullptr,
        .flags = 0,
        .waitSemaphoreInfoCount = 1,
        .pWaitSemaphoreInfos = &waitSemaphoreSubmitInfo,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &commandBufferSubmitInfo,
        .signalSemaphoreInfoCount = 1,
        .pSignalSemaphoreInfos = &signalSemaphoreSubmitInfo,
    };
    const FenceInfo& fenceInfo = deviceInfo.FenceInfos[frameContextInfo.FenceIndex];
    vr = vkQueueSubmit2(graphicsQueueFamilyInfo.QueueInfos[0].Queue, 1, &submitInfo2, fenceInfo.Fence);
    if (vr != VK_SUCCESS)
    {
        LUDUS_ASSERT_F(false, "Failed to submit to graphics queue: {}", vr);
        return false;
    }

    const VkPresentInfoKHR presentInfo =
    {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext = nullptr,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &renderFinishedSemaphore,
        .swapchainCount = 1,
        .pSwapchains = &gVulkanInfo.WindowSystemIntegrationInfo.Swapchain,
        .pImageIndices = &frameContextInfo.ImageIndex,
        .pResults = nullptr,
    };
    const QueueFamilyInfo& presentQueueFamilyInfo =
        gVulkanInfo.GetPrimaryGpuInfo().QueueFamilyInfo[gVulkanInfo.GetPrimaryGpuInfo().PresentQueueFamilyIndex];
    vr = vkQueuePresentKHR(presentQueueFamilyInfo.QueueInfos[0].Queue, &presentInfo);
    if (vr != VK_SUCCESS)
    {
        LUDUS_ASSERT_F(false, "Failed to present to present queue: {}", vr);
        return false;
    }
    return vr == VK_SUCCESS;
}

#if defined(LUDUS_RHI_ENABLE_VALIDATION_LAYER)
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
#endif

bool initializeInstance(VulkanInfo& inoutVulkanInfo, const ApplicationInfo& appInfo) noexcept
{
    VkResult vr = VK_SUCCESS;
    uint32 layersCount = 0;
    vr = vkEnumerateInstanceLayerProperties(&layersCount, nullptr);
    if (vr != VK_SUCCESS)
    {
        LUDUS_ASSERT(false, "Failed to enumerate Vulkan instance layer properties.");
        return false;
    }

    Array<VkLayerProperties> layers(layersCount);
    vr = vkEnumerateInstanceLayerProperties(&layersCount, layers.GetData());
    if (vr != VK_SUCCESS)
    {
        LUDUS_ASSERT(false, "Failed to enumerate Vulkan instance layer properties.");
        return false;
    }

    Array<std::string> listOfLayersToEnable;
    // #if defined(LUDUS_RHI_ENABLE_VALIDATION_LAYER)
    //     listOfLayersToEnable.Add("VK_LAYER_KHRONOS_validation");
    // #endif

    Array<const char*> enabledLayers;
    enabledLayers.EnsureCapacity(listOfLayersToEnable.GetSize());
    // Info logging is compiled out in Release.
    for ([[maybe_unused]] const auto& layer : layers)
    {
        LUDUS_LOG_INFO(LOG_RHI, "Vulkan instance layer: {}", layer.layerName);
    }

    for (const auto& layerToEnable : listOfLayersToEnable)
    {
        if (std::find_if(layers.begin(), layers.end(), [&](const VkLayerProperties& lyr) {
                return strcmp(lyr.layerName, layerToEnable.c_str()) == 0;
            }) != layers.end())
        {
            LUDUS_LOG_INFO(LOG_RHI, "Enabling Vulkan instance layer: {}", layerToEnable);
            enabledLayers.Add(layerToEnable.c_str());
        }
        else
        {
            LUDUS_ASSERT_F(false,
                           "Vulkan instance layer not supported: {}",
                           diagnostics::DiagnosticText{layerToEnable.data(), layerToEnable.size()});
        }
    }

    uint32 extensionCount = 0;
    vr = vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
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
#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
    listOfExtensionsToEnable.Add(VK_KHR_SURFACE_EXTENSION_NAME);
    listOfExtensionsToEnable.Add(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME);
#endif

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
            LUDUS_LOG_ERROR(LOG_RHI, "Required Vulkan instance extension not supported: {}", extensionToEnable);
            return false;
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
        .pUserData = nullptr,
    };

    constexpr StaticArray<VkValidationFeatureEnableEXT, 5> enabledValidationFeatures = {
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
        .pDisabledValidationFeatures = nullptr,
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
        .apiVersion = inoutVulkanInfo.ApiVersion,
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
        .ppEnabledExtensionNames = enabledExtensions.GetData(),
    };

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
    if (inoutVulkanInfo.Instance == VK_NULL_HANDLE)
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
        GpuInfo gpuInfo
        {
            .PhysicalDevice = gpus[i],
        };
        const bool isInitialized = initializeGpuInfo(gpuInfo, inoutVulkanInfo);
        if (isInitialized)
        {
            gpuInfos.Add(gpuInfo);
        }
    }

    // TODO: Sort GPU infos based on its score

    inoutVulkanInfo.GpuInfos = Move(gpuInfos);
    return !inoutVulkanInfo.GpuInfos.IsEmpty();
}

bool initializeGpuInfo(GpuInfo& inoutGpuInfo, const VulkanInfo& vulkanInfo) noexcept
{
    void* pNext = nullptr;
    if (vulkanInfo.ApiVersion >= VK_API_VERSION_1_2)
    {
        inoutGpuInfo.Vulkan11Properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES;
        inoutGpuInfo.Vulkan11Properties.pNext = pNext;
        pNext = &inoutGpuInfo.Vulkan11Properties;

        inoutGpuInfo.Vulkan12Properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES;
        inoutGpuInfo.Vulkan12Properties.pNext = pNext;
        pNext = &inoutGpuInfo.Vulkan12Properties;
    };
    if (vulkanInfo.ApiVersion >= VK_API_VERSION_1_3)
    {
        inoutGpuInfo.Vulkan13Properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES;
        inoutGpuInfo.Vulkan13Properties.pNext = pNext;
        pNext = &inoutGpuInfo.Vulkan13Properties;
    }
    if (vulkanInfo.ApiVersion >= VK_API_VERSION_1_4)
    {
        inoutGpuInfo.Vulkan14Properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_PROPERTIES;
        inoutGpuInfo.Vulkan14Properties.pNext = pNext;
        pNext = &inoutGpuInfo.Vulkan14Properties;
    }
    inoutGpuInfo.AccelerationStructureProperties.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
    inoutGpuInfo.AccelerationStructureProperties.pNext = pNext;
    pNext = &inoutGpuInfo.AccelerationStructureProperties;
    inoutGpuInfo.RayTracingPipelineProperties.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
    inoutGpuInfo.RayTracingPipelineProperties.pNext = pNext;
    pNext = &inoutGpuInfo.RayTracingPipelineProperties;

    inoutGpuInfo.Properties2 =
    {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = pNext,
        .properties = {},
    };
    vkGetPhysicalDeviceProperties2(inoutGpuInfo.PhysicalDevice, &inoutGpuInfo.Properties2);

    LUDUS_LOG_INFO(LOG_RHI, "Found Vulkan physical device: {}", inoutGpuInfo.Properties2.properties.deviceName);

    uint32 queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties2(inoutGpuInfo.PhysicalDevice, &queueFamilyCount, nullptr);
    Array<VkQueueFamilyProperties2> queueFamilyProperties(queueFamilyCount,
                                                          VkQueueFamilyProperties2
                                                          {
                                                              .sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2,
                                                              .pNext = nullptr,
                                                          });
    Array<VkQueueFamilyGlobalPriorityProperties> queueFamilyGlobalPriorityProperties(
        queueFamilyCount,
        VkQueueFamilyGlobalPriorityProperties
        {
            .sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_GLOBAL_PRIORITY_PROPERTIES_KHR,
            .pNext = nullptr,
        });
    if (vulkanInfo.ApiVersion >= VK_API_VERSION_1_4)
    {
        for (uint32 i = 0; i < queueFamilyCount; ++i)
        {
            queueFamilyProperties[i].pNext = &queueFamilyGlobalPriorityProperties[i];
        }
    }
    vkGetPhysicalDeviceQueueFamilyProperties2(inoutGpuInfo.PhysicalDevice,
                                              &queueFamilyCount,
                                              queueFamilyProperties.GetData());

    inoutGpuInfo.QueueFamilyInfo.EnsureCapacity(queueFamilyCount);
    for (uint32 i = 0; i < queueFamilyCount; ++i)
    {
        QueueFamilyInfo queueFamilyInfo
        {
            .Index = i,
            .Properties = queueFamilyProperties[i],
            .GlobalPriorityProperties = queueFamilyGlobalPriorityProperties[i],
        };

        inoutGpuInfo.QueueFamilyInfo.Add(queueFamilyInfo);
    }

    // Requirement Check
    bool hasGraphicsQueue = false;
    bool hasComputeQueue = false;
    bool hasTransferQueue = false;
    for (QueueFamilyInfo& queueFamilyInfo : inoutGpuInfo.QueueFamilyInfo)
    {
        if (queueFamilyInfo.Properties.queueFamilyProperties.queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            hasGraphicsQueue = true;
        }
        if (queueFamilyInfo.Properties.queueFamilyProperties.queueFlags & VK_QUEUE_COMPUTE_BIT)
        {
            hasComputeQueue = true;
        }
        if (queueFamilyInfo.Properties.queueFamilyProperties.queueFlags & VK_QUEUE_TRANSFER_BIT)
        {
            hasTransferQueue = true;
        }
    }

    if (!hasGraphicsQueue)
    {
        LUDUS_LOG_WARN(LOG_RHI,
                       "No graphics queue found on the GPU {}. Skipping.",
                       inoutGpuInfo.Properties2.properties.deviceName);
        return false;
    }
    if (!hasComputeQueue)
    {
        LUDUS_LOG_WARN(LOG_RHI,
                       "No compute queue found on the GPU {}. Skipping.",
                       inoutGpuInfo.Properties2.properties.deviceName);
        return false;
    }
    if (!hasTransferQueue)
    {
        LUDUS_LOG_WARN(LOG_RHI,
                       "No transfer queue found on the GPU {}. Skipping.",
                       inoutGpuInfo.Properties2.properties.deviceName);
        return false;
    }

    float32 deviceTypeScore = 0.0f;
    switch (inoutGpuInfo.Properties2.properties.deviceType)
    {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
            deviceTypeScore = 1.0f;
            break;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
            deviceTypeScore = 0.5f;
            break;
        default:
            LUDUS_LOG_WARN(LOG_RHI,
                           "The GPU {} is not a discrete or integrated GPU. Skipping.",
                           inoutGpuInfo.Properties2.properties.deviceName);
            return false;
    }

    pNext = nullptr;
    inoutGpuInfo.MemoryBudgetProperties =
    {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT,
        .pNext = nullptr,
    };
    pNext = &inoutGpuInfo.MemoryBudgetProperties;
    inoutGpuInfo.MemoryProperties2 =
    {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2,
        .pNext = pNext,
    };
    vkGetPhysicalDeviceMemoryProperties2(inoutGpuInfo.PhysicalDevice, &inoutGpuInfo.MemoryProperties2);

    float32 memoryScore = 0.0f;
    VkDeviceSize totalMemoryBudget = 0;
    for (uint32 i = 0; i < inoutGpuInfo.MemoryProperties2.memoryProperties.memoryHeapCount; ++i)
    {
        totalMemoryBudget += inoutGpuInfo.MemoryBudgetProperties.heapBudget[i];
    }

    if (totalMemoryBudget == 0)
    {
        LUDUS_LOG_WARN(LOG_RHI,
                       "The GPU {} has no memory budget. Skipping.",
                       inoutGpuInfo.Properties2.properties.deviceName);
        return false;
    }
    else if (totalMemoryBudget < (1024ull * 1024 * 1024)) // Less than 1GB
    {
        memoryScore = 0.1f;
    }
    else if (totalMemoryBudget < (4ull * 1024 * 1024 * 1024)) // Less than 4GB
    {
        memoryScore = 0.3f;
    }
    else if (totalMemoryBudget < (6ull * 1024 * 1024 * 1024)) // Less than 6GB
    {
        memoryScore = 0.5f;
    }
    else if (totalMemoryBudget < (8ull * 1024 * 1024 * 1024)) // Less than 8GB
    {
        memoryScore = 0.8f;
    }
    else if (totalMemoryBudget < (12ull * 1024 * 1024 * 1024)) // Less than 12GB
    {
        memoryScore = 0.9f;
    }
    else // 12GB or more
    {
        memoryScore = 1.0f;
    }

    float32 queueTopologyScore = 0.0f;
    if (queueFamilyCount < 3)
    {
        queueTopologyScore = 0.0f; // Penalize if there are less than 3 queue families
    }
    else
    {
        Array<bool> graphicsQueueFamilies(queueFamilyCount);
        Array<bool> computeQueueFamilies(queueFamilyCount);
        Array<bool> transferQueueFamilies(queueFamilyCount);
        for (uint32 i = 0; i < queueFamilyCount; ++i)
        {
            QueueFamilyInfo& queueFamilyInfo = inoutGpuInfo.QueueFamilyInfo[i];
            if (queueFamilyInfo.Properties.queueFamilyProperties.queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                graphicsQueueFamilies[i] = true;
            }

            if (queueFamilyInfo.Properties.queueFamilyProperties.queueFlags & VK_QUEUE_COMPUTE_BIT)
            {
                computeQueueFamilies[i] = true;
            }

            if (queueFamilyInfo.Properties.queueFamilyProperties.queueFlags & VK_QUEUE_TRANSFER_BIT)
            {
                transferQueueFamilies[i] = true;
            }
        }

        uint32 dedicatedGraphicsQueueFamilyIndex = UINT32_MAX;
        uint32 dedicatedComputeQueueFamilyIndex = UINT32_MAX;
        uint32 dedicatedTransferQueueFamilyIndex = UINT32_MAX;
        for (uint32 i = 0; i < queueFamilyCount; ++i)
        {
            if (graphicsQueueFamilies[i] && !computeQueueFamilies[i] && !transferQueueFamilies[i])
            {
                dedicatedGraphicsQueueFamilyIndex = i;
            }

            if (computeQueueFamilies[i] && !graphicsQueueFamilies[i] && !transferQueueFamilies[i])
            {
                dedicatedComputeQueueFamilyIndex = i;
            }

            if (transferQueueFamilies[i] && !graphicsQueueFamilies[i] && !computeQueueFamilies[i])
            {
                dedicatedTransferQueueFamilyIndex = i;
            }
        }

        for (uint32 i = 0; i < queueFamilyCount; ++i)
        {
            if (dedicatedGraphicsQueueFamilyIndex == i || dedicatedComputeQueueFamilyIndex == i ||
                dedicatedTransferQueueFamilyIndex == i)
            {
                continue; // Skip this iteration if the queue family is already dedicated
            }

            if (dedicatedGraphicsQueueFamilyIndex == UINT32_MAX && graphicsQueueFamilies[i])
            {
                dedicatedGraphicsQueueFamilyIndex = i;
            }

            if (dedicatedComputeQueueFamilyIndex == UINT32_MAX && computeQueueFamilies[i])
            {
                dedicatedComputeQueueFamilyIndex = i;
            }

            if (dedicatedTransferQueueFamilyIndex == UINT32_MAX && transferQueueFamilies[i])
            {
                dedicatedTransferQueueFamilyIndex = i;
            }
        }

        if (dedicatedGraphicsQueueFamilyIndex != UINT32_MAX)
        {
            queueTopologyScore += 1.0f; // Assign a score for having a dedicated graphics queue
        }

        if (dedicatedComputeQueueFamilyIndex != UINT32_MAX)
        {
            queueTopologyScore += 1.0f; // Assign a score for having a dedicated compute queue
        }

        if (dedicatedTransferQueueFamilyIndex != UINT32_MAX)
        {
            queueTopologyScore += 1.0f; // Assign a score for having a dedicated transfer queue
        }
        queueTopologyScore /= 3.0f; // Normalize the queue topology score to a maximum of 1.0f
    }

    inoutGpuInfo.Score = deviceTypeScore + memoryScore + queueTopologyScore;

    for (uint32 i = 0; i < queueFamilyCount; ++i)
    {
        QueueFamilyInfo& queueFamilyInfo = inoutGpuInfo.QueueFamilyInfo[i];

        if (vkEnumeratePhysicalDeviceQueueFamilyPerformanceQueryCountersKHR != nullptr)
        {
            uint32 performanceQueryCounterCount = 0;
            vkEnumeratePhysicalDeviceQueueFamilyPerformanceQueryCountersKHR(inoutGpuInfo.PhysicalDevice,
                                                                            i,
                                                                            &performanceQueryCounterCount,
                                                                            nullptr,
                                                                            nullptr);
            queueFamilyInfo.PerformanceCounters.Resize(performanceQueryCounterCount);
            queueFamilyInfo.PerformanceCounterDescriptions.Resize(performanceQueryCounterCount);
            vkEnumeratePhysicalDeviceQueueFamilyPerformanceQueryCountersKHR(
                inoutGpuInfo.PhysicalDevice,
                i,
                &performanceQueryCounterCount,
                queueFamilyInfo.PerformanceCounters.GetData(),
                queueFamilyInfo.PerformanceCounterDescriptions.GetData());
        }
    }

    return true;
}

bool initializeDeviceInfo(DeviceInfo& inoutDeviceInfo, const GpuInfo& gpuInfo, const VulkanInfo& vulkanInfo) noexcept
{
    VkResult vr = VK_SUCCESS;

    uint32 extensionPropertyCount = 0;
    vr = vkEnumerateDeviceExtensionProperties(gpuInfo.PhysicalDevice, nullptr, &extensionPropertyCount, nullptr);
    if (vr != VK_SUCCESS)
    {
        return false;
    }
    Array<VkExtensionProperties> extensionProperties(extensionPropertyCount);
    vr = vkEnumerateDeviceExtensionProperties(gpuInfo.PhysicalDevice,
                                              nullptr,
                                              &extensionPropertyCount,
                                              extensionProperties.GetData());
    if (vr != VK_SUCCESS)
    {
        return false;
    }

    Array<VkExtensionProperties> extensionsToRequest;
    extensionsToRequest.Add(
    {
        .extensionName = VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    });
    if (vulkanInfo.ApiVersion < VK_API_VERSION_1_4)
    {
        extensionsToRequest.Add(
        {
            .extensionName = VK_KHR_GLOBAL_PRIORITY_EXTENSION_NAME,
        });
    }
    if (vulkanInfo.ApiVersion < VK_API_VERSION_1_3)
    {
        extensionsToRequest.Add(
        {
            .extensionName = VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
        });
    }
    extensionsToRequest.Add(
    {
        .extensionName = VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
    });
    extensionsToRequest.Add(
    {
        .extensionName = VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
    });
    extensionsToRequest.Add(
    {
        .extensionName = VK_KHR_RAY_QUERY_EXTENSION_NAME,
    });
    extensionsToRequest.Add(
    {
        .extensionName = VK_EXT_MEMORY_BUDGET_EXTENSION_NAME,
    });
    extensionsToRequest.Add(
    {
        .extensionName = VK_EXT_MESH_SHADER_EXTENSION_NAME,
    });
    extensionsToRequest.Add(
    {
        .extensionName = VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME,
    });
    extensionsToRequest.Add(
    {
        .extensionName = VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME,
    });
    extensionsToRequest.Add(
    {
        .extensionName = VK_EXT_GRAPHICS_PIPELINE_LIBRARY_EXTENSION_NAME,
    });
    extensionsToRequest.Add(
    {
        .extensionName = VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME,
    });
    extensionsToRequest.Add(
    {
        .extensionName = VK_EXT_PAGEABLE_DEVICE_LOCAL_MEMORY_EXTENSION_NAME,
    });

    for (uint32 i = 0; i < extensionPropertyCount; ++i)
    {
        LUDUS_LOG_INFO(LOG_RHI, "Found device extension: {}", extensionProperties[i].extensionName);
    }

    uint32 extensionsToRequestCount = static_cast<uint32>(extensionsToRequest.GetSize());
    Array<const char*> requestedExtensions;
    for (uint32 i = 0; i < extensionsToRequestCount; ++i)
    {
        if (std::find_if(extensionProperties.GetData(),
                         extensionProperties.GetData() + extensionPropertyCount,
                         [&](const VkExtensionProperties& ext) {
                             return strcmp(ext.extensionName, extensionsToRequest[i].extensionName) == 0;
                         }) != extensionProperties.GetData() + extensionPropertyCount)
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
        .features = {},
    };
    pNext = &inoutDeviceInfo.Features2;
    if (vulkanInfo.ApiVersion >= VK_API_VERSION_1_1)
    {
        inoutDeviceInfo.Vulkan11Features =
        {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
            .pNext = pNext,
        };
        pNext = &inoutDeviceInfo.Vulkan11Features;
    }
    if (vulkanInfo.ApiVersion >= VK_API_VERSION_1_2)
    {
        inoutDeviceInfo.Vulkan12Features =
        {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
            .pNext = pNext,
        };
        pNext = &inoutDeviceInfo.Vulkan12Features;
    }
    if (vulkanInfo.ApiVersion >= VK_API_VERSION_1_3)
    {
        inoutDeviceInfo.Vulkan13Features =
        {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
            .pNext = pNext,
        };
        pNext = &inoutDeviceInfo.Vulkan13Features;
    }
    if (vulkanInfo.ApiVersion >= VK_API_VERSION_1_4)
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

    // One queue from each selected family; a shared graphics/present family is
    // requested only once. Priority storage remains alive through vkCreateDevice.
    constexpr float32 queuePriority = 1.0f;
    Array<VkDeviceQueueCreateInfo> queueCreateInfos;
    queueCreateInfos.EnsureCapacity(2);
    for (const QueueFamilyInfo& family : gpuInfo.QueueFamilyInfo)
    {
        if (family.Index != gpuInfo.GraphicsQueueFamilyIndex && family.Index != gpuInfo.PresentQueueFamilyIndex)
        {
            continue;
        }
        queueCreateInfos.Add(VkDeviceQueueCreateInfo
        {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = family.Index,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority,
        });
    }

    const VkDeviceCreateInfo deviceCreateInfo
    {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = pNext,
        .flags = 0,
        .queueCreateInfoCount = static_cast<uint32>(queueCreateInfos.GetSize()),
        .pQueueCreateInfos = queueCreateInfos.GetData(),
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = static_cast<uint32>(requestedExtensions.GetSize()),
        .ppEnabledExtensionNames = requestedExtensions.GetData(),
        .pEnabledFeatures = nullptr,
    };
    vr = vkCreateDevice(gpuInfo.PhysicalDevice, &deviceCreateInfo, nullptr, &inoutDeviceInfo.Device);
    if (vr != VK_SUCCESS)
    {
        LUDUS_LOG_ERROR(LOG_RHI, "Failed to create Vulkan device: {}", vr);
        return false;
    }
    return true;
}

void shutdownDevice(DeviceInfo& inoutDeviceInfo) noexcept
{
    if (inoutDeviceInfo.Device != VK_NULL_HANDLE)
    {
        for (SemaphoreInfo& semaphoreInfo : inoutDeviceInfo.SemaphoreInfos)
        {
            shutdownSemaphore(semaphoreInfo, inoutDeviceInfo);
        }
        inoutDeviceInfo.SemaphoreInfos.Clear();
        for (FenceInfo& fenceInfo : inoutDeviceInfo.FenceInfos)
        {
            shutdownFence(fenceInfo, inoutDeviceInfo);
        }
        inoutDeviceInfo.FenceInfos.Clear();

        vkDestroyDevice(inoutDeviceInfo.Device, nullptr);
        inoutDeviceInfo = {};
    }
}

void initializeQueues(QueueFamilyInfo& inoutQueueFamilyInfo, const DeviceInfo& deviceInfo) noexcept
{
    const uint32 inoutQueueFamilyIndex = inoutQueueFamilyInfo.Properties.queueFamilyProperties.queueCount;
    for (uint32 i = 0; i < inoutQueueFamilyIndex; ++i)
    {
        QueueInfo queueInfo = {};
        vkGetDeviceQueue(deviceInfo.Device, inoutQueueFamilyInfo.Index, i, &queueInfo.Queue);
        inoutQueueFamilyInfo.QueueInfos.Add(queueInfo);
    }
    LUDUS_LOG_INFO(LOG_RHI,
                   "Initialized {} queues for family index {}",
                   inoutQueueFamilyInfo.QueueInfos.GetSize(),
                   inoutQueueFamilyInfo.Index);
}

bool initializeWindowSystemIntegration(WindowSystemIntegrationInfo& inoutWsiInfo, const VulkanInfo& vulkanInfo) noexcept
{
    bool result = false;
#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
    if (inoutWsiInfo.WindowInfo.System != platform::WindowSystem::Wayland ||
        inoutWsiInfo.WindowInfo.Display == nullptr || inoutWsiInfo.WindowInfo.Surface == nullptr ||
        vkCreateWaylandSurfaceKHR == nullptr)
    {
        return result;
    }
    VkResult vr = VK_SUCCESS;

    const VkWaylandSurfaceCreateInfoKHR waylandSurfaceCreateInfo =
    {
        .sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
        .display = inoutWsiInfo.WindowInfo.Display,
        .surface = inoutWsiInfo.WindowInfo.Surface,
    };

    vr = vkCreateWaylandSurfaceKHR(vulkanInfo.Instance,
                                   &waylandSurfaceCreateInfo,
                                   nullptr,
                                   &inoutWsiInfo.SurfaceInfo.Surface);
    if (vr != VK_SUCCESS)
    {
        LUDUS_LOG_ERROR(LOG_RHI, "Failed to create Wayland surface: {}", vr);
        return result;
    }

    result = true;
#else
    (void)inoutWsiInfo;
    (void)vulkanInfo;
#endif

    if (!result)
    {
        LUDUS_LOG_ERROR(LOG_RHI, "Failed to initialize window system integration.");
        shutdownWindowSystemIntegration(inoutWsiInfo, vulkanInfo);
        return false;
    }

    return result;
}

void shutdownWindowSystemIntegration(WindowSystemIntegrationInfo& inoutWsiInfo, const VulkanInfo& vulkanInfo) noexcept
{
    if (inoutWsiInfo.SurfaceInfo.Surface != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(vulkanInfo.Instance, inoutWsiInfo.SurfaceInfo.Surface, nullptr);
        inoutWsiInfo.SurfaceInfo.Surface = VK_NULL_HANDLE;
    }
    inoutWsiInfo = {};
}

bool initializeSwapchain(WindowSystemIntegrationInfo& inoutWsiInfo, DeviceInfo& deviceInfo) noexcept
{
    VkResult vr = VK_SUCCESS;

    const VkSwapchainCreateInfoKHR swapchainCreateInfo =
    {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = inoutWsiInfo.SurfaceInfo.Surface,
        .minImageCount = static_cast<uint32>(inoutWsiInfo.ImageInfos.GetSize()),
        .imageFormat = inoutWsiInfo.ImageInfos[0].ImageFormat,
        .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent = inoutWsiInfo.ImageInfos[0].ImageExtent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE,
    };

    vr = vkCreateSwapchainKHR(deviceInfo.Device, &swapchainCreateInfo, nullptr, &inoutWsiInfo.Swapchain);
    if (vr != VK_SUCCESS)
    {
        LUDUS_LOG_ERROR(LOG_RHI, "Failed to create swapchain: {}", vr);
        return false;
    }

    uint32 swapchainImageCount = 0;
    vr = vkGetSwapchainImagesKHR(deviceInfo.Device, inoutWsiInfo.Swapchain, &swapchainImageCount, nullptr);
    if (vr != VK_SUCCESS)
    {
        LUDUS_LOG_ERROR(LOG_RHI, "Failed to get swapchain image count: {}", vr);
        return false;
    }

    Array<VkImage> swapchainImages(swapchainImageCount);
    vr = vkGetSwapchainImagesKHR(deviceInfo.Device,
                                 inoutWsiInfo.Swapchain,
                                 &swapchainImageCount,
                                 reinterpret_cast<VkImage*>(swapchainImages.GetData()));
    if (vr != VK_SUCCESS)
    {
        LUDUS_LOG_ERROR(LOG_RHI, "Failed to get swapchain images: {}", vr);
        return false;
    }

    inoutWsiInfo.RenderFinishedSemaphoreIndices.EnsureCapacity(swapchainImageCount);
    ImageInfo imageInfoCopy = inoutWsiInfo.ImageInfos[0];
    inoutWsiInfo.ImageInfos.Resize(swapchainImageCount, imageInfoCopy);
    for (uint32 i = 0; i < swapchainImageCount; ++i)
    {
        inoutWsiInfo.ImageInfos[i].Image = swapchainImages[i];

        uint32 renderFinishedSemaphoreIndex = 0;
        if (!initializeSemaphore(renderFinishedSemaphoreIndex, deviceInfo))
        {
            LUDUS_ASSERT(false, "Failed to initialize semaphore.");
        }
        inoutWsiInfo.RenderFinishedSemaphoreIndices.Add(renderFinishedSemaphoreIndex);
    }

    return true;
}

void shutdownSwapchain(WindowSystemIntegrationInfo& inoutWsiInfo, const DeviceInfo& deviceInfo) noexcept
{
    if (inoutWsiInfo.Swapchain != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(deviceInfo.Device, inoutWsiInfo.Swapchain, nullptr);
        inoutWsiInfo.Swapchain = VK_NULL_HANDLE;
    }
}

bool initializeCommands(DeviceInfo& inoutDeviceInfo, const GpuInfo& gpuInfo) noexcept
{
    VkResult vr = VK_SUCCESS;
    CommandPoolInfo commandPoolInfo = {};
    const VkCommandPoolCreateInfo commandPoolCreateInfo =
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = gpuInfo.GraphicsQueueFamilyIndex,
    };
    vr = vkCreateCommandPool(inoutDeviceInfo.Device, &commandPoolCreateInfo, nullptr, &commandPoolInfo.CommandPool);
    if (vr != VK_SUCCESS)
    {
        LUDUS_LOG_ERROR(LOG_RHI, "Failed to create command pool: {}", vr);
        return false;
    }
    inoutDeviceInfo.CommandPoolInfos.Add(commandPoolInfo);

    return true;
}

void shutdownCommands(DeviceInfo& inoutDeviceInfo) noexcept
{
    // Implementation for shutting down command pools and related resources
    for (CommandPoolInfo& commandPoolInfo : inoutDeviceInfo.CommandPoolInfos)
    {
        for (CommandBufferInfo& commandBufferInfo : commandPoolInfo.CommandBufferInfos)
        {
            vkFreeCommandBuffers(inoutDeviceInfo.Device,
                                 commandPoolInfo.CommandPool,
                                 1,
                                 &commandBufferInfo.CommandBuffer);
        }
        vkDestroyCommandPool(inoutDeviceInfo.Device, commandPoolInfo.CommandPool, nullptr);
        commandPoolInfo.CommandPool = VK_NULL_HANDLE;
    }
    inoutDeviceInfo.CommandPoolInfos.Clear();
}

bool initializeSemaphore(uint32& outSemaphoreIndex, DeviceInfo& inoutDeviceInfo) noexcept
{
    VkResult vr = VK_SUCCESS;
    SemaphoreInfo semaphoreInfo = {};
    const VkSemaphoreCreateInfo semaphoreCreateInfo =
    {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
    };
    vr = vkCreateSemaphore(inoutDeviceInfo.Device, &semaphoreCreateInfo, nullptr, &semaphoreInfo.Semaphore);
    if (vr != VK_SUCCESS)
    {
        LUDUS_LOG_ERROR(LOG_RHI, "Failed to create semaphore: {}", vr);
        return false;
    }
    outSemaphoreIndex = inoutDeviceInfo.SemaphoreInfos.GetSize();
    inoutDeviceInfo.SemaphoreInfos.Add(semaphoreInfo);
    return true;
}

void shutdownSemaphore(SemaphoreInfo& inoutSemaphoreInfo, DeviceInfo& inoutDeviceInfo) noexcept
{
    if (inoutSemaphoreInfo.Semaphore != VK_NULL_HANDLE)
    {
        vkDestroySemaphore(inoutDeviceInfo.Device, inoutSemaphoreInfo.Semaphore, nullptr);
        inoutSemaphoreInfo.Semaphore = VK_NULL_HANDLE;
    }
}

bool initializeFence(uint32& outFenceIndex, DeviceInfo& inoutDeviceInfo) noexcept
{
    VkResult vr = VK_SUCCESS;
    FenceInfo fenceInfo = {};
    const VkFenceCreateInfo fenceCreateInfo =
    {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    vr = vkCreateFence(inoutDeviceInfo.Device, &fenceCreateInfo, nullptr, &fenceInfo.Fence);
    if (vr != VK_SUCCESS)
    {
        LUDUS_LOG_ERROR(LOG_RHI, "Failed to create fence: {}", vr);
        return false;
    }
    outFenceIndex = inoutDeviceInfo.FenceInfos.GetSize();
    inoutDeviceInfo.FenceInfos.Add(fenceInfo);
    return true;
}

void shutdownFence(FenceInfo& inoutFenceInfo, DeviceInfo& inoutDeviceInfo) noexcept
{
    if (inoutFenceInfo.Fence != VK_NULL_HANDLE)
    {
        vkDestroyFence(inoutDeviceInfo.Device, inoutFenceInfo.Fence, nullptr);
        inoutFenceInfo.Fence = VK_NULL_HANDLE;
    }
}

bool createCommandBufferInfo(uint32& outCommandBufferInfoIndex,
                             CommandPoolInfo& inoutCommandPoolInfo,
                             const DeviceInfo& deviceInfo) noexcept
{
    VkResult vr = VK_SUCCESS;

    CommandBufferInfo commandBufferInfo = {};
    const VkCommandBufferAllocateInfo commandBufferAllocateInfo =
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = inoutCommandPoolInfo.CommandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    vr = vkAllocateCommandBuffers(deviceInfo.Device, &commandBufferAllocateInfo, &commandBufferInfo.CommandBuffer);
    if (vr != VK_SUCCESS)
    {
        LUDUS_LOG_ERROR(LOG_RHI, "Failed to allocate command buffer: {}", vr);
        return false;
    }
    outCommandBufferInfoIndex = inoutCommandPoolInfo.CommandBufferInfos.GetSize();
    inoutCommandPoolInfo.CommandBufferInfos.Add(commandBufferInfo);

    return true;
}
} // namespace ludus::graphics::rhi
