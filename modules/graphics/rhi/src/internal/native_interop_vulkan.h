#pragma once

#include "native_interop.h"

// Thanks to Khronos, "Vulkan Specification", Synchronization and Cache Control,
// submission order and fence signal operations: a pipeline barrier publishes the
// producer image, and a zero-submit fence follows foreign work on the same queue.
// https://docs.vulkan.org/spec/latest/chapters/synchronization.html
namespace ludus::graphics::rhi::backend
{
namespace
{
struct NativeVulkanRead final
{
    VkCommandPool Pool = VK_NULL_HANDLE;
    VkCommandBuffer Command = VK_NULL_HANDLE;
    VkFence Publication = VK_NULL_HANDLE, Completion = VK_NULL_HANDLE;
    bool Released = false;
};
internal::StorageTable<NativeVulkanRead> gNativeVulkanReads;
RasterStatus NativeVkResult(VkResult result) noexcept
{
    return result == VK_SUCCESS                             ? RasterStatus::Ready
           : result == VK_NOT_READY || result == VK_TIMEOUT ? RasterStatus::Pending
           : result == VK_ERROR_DEVICE_LOST                 ? RasterStatus::DeviceLost
           : result == VK_ERROR_OUT_OF_HOST_MEMORY || result == VK_ERROR_OUT_OF_DEVICE_MEMORY
               ? RasterStatus::OutOfMemory
               : RasterStatus::Failed;
}
} // namespace
bool NativeInteropSupported() noexcept
{
    return true;
}
RasterStatus NativeDeviceSnapshot(NativeDeviceInfo& output) noexcept
{
    if (gDevice == VK_NULL_HANDLE || gQueue == VK_NULL_HANDLE)
    {
        return RasterStatus::NotReady;
    }
    NativeDeviceInfo result;
    result.Identity.Api = Backend::Vulkan;
    result.Identity.Instance = gInstance;
    result.Identity.PhysicalDevice = gPhysical;
    result.Identity.Device = gDevice;
    result.Identity.CommandQueue = gQueue;
    result.Identity.QueueFamily = gFamily;
    const auto* imported = internal::NativeDeviceRequest();
    result.Identity.QueueIndex = imported == nullptr ? 0 : imported->Identity.QueueIndex;
    result.InstanceApiVersion = gNativeInstanceVersion;
    VkPhysicalDeviceIDProperties identity{};
    identity.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
    VkPhysicalDeviceProperties2 properties{};
    properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    properties.pNext = &identity;
    vkGetPhysicalDeviceProperties2(gPhysical, &properties);
    std::memcpy(result.DeviceUuid, identity.deviceUUID, sizeof(result.DeviceUuid));
    output = result;
    return RasterStatus::Ready;
}
RasterStatus NativeTexturePublish(const internal::NativeTexturePublication& publication,
                                  NativeTextureInfo& output) noexcept
{
    const auto slot = publication.Lease;
    const auto textureSlot = publication.Texture;
    if (!gNativeVulkanReads.EnsureSlots(slot + 1))
    {
        return internal::StorageFailure();
    }
    auto& read = gNativeVulkanReads[slot];
    VkCommandPoolCreateInfo pool{};
    pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool.queueFamilyIndex = gFamily;
    auto result = vkCreateCommandPool(gDevice, &pool, nullptr, &read.Pool);
    if (result != VK_SUCCESS)
    {
        return NativeVkResult(result);
    }
    VkCommandBufferAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate.commandPool = read.Pool;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    result = vkAllocateCommandBuffers(gDevice, &allocate, &read.Command);
    if (result != VK_SUCCESS)
    {
        return NativeVkResult(result);
    }
    VkFenceCreateInfo fence{};
    fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    result = vkCreateFence(gDevice, &fence, nullptr, &read.Publication);
    if (result != VK_SUCCESS)
    {
        return NativeVkResult(result);
    }
    result = vkCreateFence(gDevice, &fence, nullptr, &read.Completion);
    if (result != VK_SUCCESS)
    {
        return NativeVkResult(result);
    }
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    result = vkBeginCommandBuffer(read.Command, &begin);
    if (result != VK_SUCCESS)
    {
        return NativeVkResult(result);
    }
    auto& texture = gRasterTextures[textureSlot];
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = texture.ColorLayout;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.srcAccessMask = texture.ColorWriterAccess | texture.ColorAccess;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.image = texture.Object;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    const VkPipelineStageFlags readers = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                         (gQueueCompute ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : 0U);
    vkCmdPipelineBarrier(read.Command,
                         texture.ColorStage | texture.ColorWriterStage,
                         readers,
                         0,
                         0,
                         nullptr,
                         0,
                         nullptr,
                         1,
                         &barrier);
    result = vkEndCommandBuffer(read.Command);
    if (result != VK_SUCCESS)
    {
        return NativeVkResult(result);
    }
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &read.Command;
    result = vkQueueSubmit(gQueue, 1, &submit, read.Publication);
    if (result != VK_SUCCESS)
    {
        return NativeVkResult(result);
    }
    texture.ColorLayout = barrier.newLayout;
    texture.ColorStage |= readers;
    texture.ColorAccess = VK_ACCESS_SHADER_READ_BIT;
    texture.SampleVisibility |= readers;
#if VK_USE_64_BIT_PTR_DEFINES
    output.VulkanImage = reinterpret_cast<uint64>(texture.Object);
#else
    output.VulkanImage = texture.Object;
#endif
    output.VulkanLayout = static_cast<uint32>(barrier.newLayout);
    return RasterStatus::Ready;
}
RasterStatus NativeTextureRelease(usize slot) noexcept
{
    auto& read = gNativeVulkanReads[slot];
    if (read.Released)
    {
        return RasterStatus::Pending;
    }
    const auto result = vkQueueSubmit(gQueue, 0, nullptr, read.Completion);
    if (result == VK_SUCCESS || result == VK_ERROR_DEVICE_LOST)
    {
        read.Released = true;
    }
    return NativeVkResult(result);
}
RasterStatus NativeTexturePoll(usize slot, bool wait) noexcept
{
    auto& read = gNativeVulkanReads[slot];
    const auto fence = read.Released ? read.Completion : read.Publication;
    return NativeVkResult(wait ? vkWaitForFences(gDevice, 1, &fence, VK_TRUE, 5000000000ULL)
                               : vkGetFenceStatus(gDevice, fence));
}
void NativeTextureDestroy(usize slot) noexcept
{
    if (slot >= gNativeVulkanReads.GetSize())
    {
        return;
    }
    auto& read = gNativeVulkanReads[slot];
    vkDestroyFence(gDevice, read.Publication, nullptr);
    vkDestroyFence(gDevice, read.Completion, nullptr);
    vkDestroyCommandPool(gDevice, read.Pool, nullptr);
    read = {};
}
void NativeInteropDetachSurface() noexcept
{
    ShutdownRendering();
    ReleaseTargets();
    vkDestroyRenderPass(gDevice, gPass, nullptr);
    gPass = VK_NULL_HANDLE;
    if (gSurface != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(gInstance, gSurface, nullptr);
        gSurface = VK_NULL_HANDLE;
    }
    gWindow = {};
}
void NativeInteropReset() noexcept {}
} // namespace ludus::graphics::rhi::backend
