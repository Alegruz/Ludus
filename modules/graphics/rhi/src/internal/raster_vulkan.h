#pragma once
// Private implementation included only by rhi_vulkan.cpp. Thanks to AMD GPUOpen,
// "Vulkan Memory Allocator 3.3.0", Quick start and Memory mapping (flush alignment),
// https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/memory_mapping.html
// and Khronos, "Synchronization Examples" (transfer write -> shader sample),
// https://docs.vulkan.org/guide/latest/synchronization_examples.html
// Initial uploads use a bounded synchronous setup copy, not an upload ring.
// Submitted raster resource release uses the existing ordered queue's fences.
namespace ludus::graphics::rhi::backend
{
namespace
{
struct RasterVulkanBuffer final
{
    VkBuffer Object = VK_NULL_HANDLE;
    VmaAllocation Allocation = nullptr;
};
struct RasterVulkanTexture final
{
    VkImage Object = VK_NULL_HANDLE;
    VmaAllocation Allocation = nullptr;
    TextureDescription Info;
    VkImage Depth = VK_NULL_HANDLE;
    VmaAllocation DepthAllocation = nullptr;
    VkImageView ColorView = VK_NULL_HANDLE;
    VkImageView DepthView = VK_NULL_HANDLE;
    VkFramebuffer Framebuffer = VK_NULL_HANDLE;
    VkImageLayout ColorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout DepthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags ColorStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkAccessFlags ColorAccess = 0;
    VkPipelineStageFlags ColorWriterStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkAccessFlags ColorWriterAccess = 0;
    VkPipelineStageFlags SampleVisibility = 0;
    RasterVulkanBuffer Upload;
    VkCommandBuffer Command = VK_NULL_HANDLE;
    VkFence Fence = VK_NULL_HANDLE;
    uint32 Request = 0;
};
struct RasterVulkanShader final
{
    VkShaderModule Object = VK_NULL_HANDLE;
    char Entry[64]{};
};
struct RasterVulkanSet final
{
    VkDescriptorPool Pool = VK_NULL_HANDLE;
    VkDescriptorSet Object = VK_NULL_HANDLE;
};
struct RasterVulkanPipeline final
{
    VkPipeline Object = VK_NULL_HANDLE;
    VkPipelineLayout Layout = VK_NULL_HANDLE;
    internal::RasterPipelineInfo Info;
};
VmaAllocator gRasterAllocator = nullptr;
RasterVulkanBuffer gRasterBuffers[internal::RASTER_CAPACITY];
RasterVulkanTexture gRasterTextures[internal::RASTER_CAPACITY];
VkImageView gRasterViews[internal::RASTER_CAPACITY]{};
VkSampler gRasterSamplers[internal::RASTER_CAPACITY]{};
RasterVulkanShader gRasterShaders[internal::RASTER_CAPACITY];
VkDescriptorSetLayout gRasterLayouts[internal::RASTER_CAPACITY]{};
RasterVulkanSet gRasterSets[internal::RASTER_CAPACITY];
RasterVulkanPipeline gRasterPipelines[internal::RASTER_CAPACITY];
uint64 gRasterOrdinals[FRAMES]{};
uint64 gRasterCompleted = 0;
VkExtent2D gRasterExtent{};
usize gRasterActiveTexture = internal::RASTER_CAPACITY;
struct RasterVkPass final
{
    VkRenderPass Object = VK_NULL_HANDLE;
    VkFormat Format = VK_FORMAT_UNDEFINED;
    RasterPassDescription State;
    bool Surface = false;
};
RasterVkPass gRasterPasses[192];
RasterStatus RasterError(VkResult result) noexcept
{
    if (result == VK_SUCCESS)
    {
        return RasterStatus::Ready;
    }
    (void)Check(result);
    if (result == VK_ERROR_DEVICE_LOST)
    {
        internal::Fail(gSession, StartupError::DeviceLost);
        return RasterStatus::DeviceLost;
    }
    return result == VK_ERROR_OUT_OF_HOST_MEMORY || result == VK_ERROR_OUT_OF_DEVICE_MEMORY ? RasterStatus::OutOfMemory
                                                                                            : RasterStatus::Failed;
}
// Thanks to Khronos, Vulkan Guide, "Synchronization Examples", Graphics to
// Graphics Dependencies (color attachment -> sampled image) and WAR/WAW cases:
// https://docs.vulkan.org/guide/latest/synchronization_examples.html
// Adapted to the retained Vulkan 1.1 barriers/render-pass path, not sync2.
// Thanks to Khronos, Vulkan Specification, "Render Pass Compatibility":
// https://docs.vulkan.org/spec/latest/chapters/renderpass.html#renderpass-compatibility
// Dependencies must match across compatible pass objects, including read-only
// depth variants and the original surface pipeline/framebuffer render pass.
void RasterVkDependencies(bool surface, VkSubpassDependency (&dependencies)[2]) noexcept
{
    auto& incoming = dependencies[0];
    incoming.srcSubpass = VK_SUBPASS_EXTERNAL;
    incoming.dstSubpass = 0;
    incoming.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    incoming.dstStageMask = incoming.srcStageMask;
    incoming.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    incoming.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    auto& outgoing = dependencies[1];
    outgoing.srcSubpass = 0;
    outgoing.dstSubpass = VK_SUBPASS_EXTERNAL;
    outgoing.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    outgoing.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    outgoing.dstStageMask =
        surface && gHeadless ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    outgoing.dstAccessMask = surface && gHeadless ? VK_ACCESS_TRANSFER_READ_BIT : 0;
}
VkResult
RasterVkRenderPass(VkFormat format, bool surface, const RasterPassDescription& state, VkRenderPass& output) noexcept
{
    RasterVkPass* available = nullptr;
    for (auto& pass : gRasterPasses)
    {
        const auto& cached = pass.State;
        if (pass.Object == VK_NULL_HANDLE)
        {
            if (available == nullptr)
            {
                available = &pass;
            }
            continue;
        }
        if (pass.Format == format && pass.Surface == surface && cached.ColorLoad == state.ColorLoad &&
            cached.ColorStore == state.ColorStore && cached.DepthLoad == state.DepthLoad &&
            cached.DepthStore == state.DepthStore && cached.DepthReadOnly == state.DepthReadOnly)
        {
            output = pass.Object;
            return VK_SUCCESS;
        }
    }
    if (available == nullptr)
    {
        return VK_ERROR_TOO_MANY_OBJECTS;
    }
    VkAttachmentDescription attachments[2]{};
    auto& color = attachments[0];
    color.format = format;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = state.ColorLoad == RasterLoad::Clear  ? VK_ATTACHMENT_LOAD_OP_CLEAR
                   : state.ColorLoad == RasterLoad::Load ? VK_ATTACHMENT_LOAD_OP_LOAD
                                                         : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.storeOp =
        state.ColorStore == RasterStore::Store ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = surface ? (gHeadless ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
                                  : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.finalLayout = color.initialLayout;
    auto& depth = attachments[1];
    depth.format = gDepthFormat;
    depth.samples = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp = state.DepthLoad == RasterLoad::Clear  ? VK_ATTACHMENT_LOAD_OP_CLEAR
                   : state.DepthLoad == RasterLoad::Load ? VK_ATTACHMENT_LOAD_OP_LOAD
                                                         : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.storeOp =
        state.DepthStore == RasterStore::Store ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference colorReference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthReference{1,
                                         state.DepthReadOnly ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                                                             : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorReference;
    subpass.pDepthStencilAttachment = &depthReference;
    VkSubpassDependency dependencies[2]{};
    RasterVkDependencies(surface, dependencies);
    VkRenderPassCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    create.attachmentCount = 2;
    create.pAttachments = attachments;
    create.subpassCount = 1;
    create.pSubpasses = &subpass;
    create.dependencyCount = 2;
    create.pDependencies = dependencies;
    const auto result = vkCreateRenderPass(gDevice, &create, nullptr, &available->Object);
    if (result == VK_SUCCESS)
    {
        available->Format = format;
        available->Surface = surface;
        available->State = state;
        output = available->Object;
    }
    return result;
}
VkResult RasterAllocator() noexcept
{
    if (gRasterAllocator != nullptr)
    {
        return VK_SUCCESS;
    }
    VmaVulkanFunctions functions{};
    functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    VmaAllocatorCreateInfo info{};
    info.physicalDevice = gPhysical;
    info.device = gDevice;
    info.instance = gInstance;
    info.vulkanApiVersion = VK_API_VERSION_1_1;
    info.pVulkanFunctions = &functions;
    return vmaCreateAllocator(&info, &gRasterAllocator);
}
VkResult RasterUploadBuffer(VkBufferUsageFlags usage, std::span<const uint8> bytes, RasterVulkanBuffer& buffer) noexcept
{
    auto result = RasterAllocator();
    if (result != VK_SUCCESS)
    {
        return result;
    }
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = bytes.size();
    info.usage = usage;
    VmaAllocationCreateInfo allocation{};
    allocation.usage = VMA_MEMORY_USAGE_AUTO;
    allocation.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo mapped{};
    result = vmaCreateBuffer(gRasterAllocator, &info, &allocation, &buffer.Object, &buffer.Allocation, &mapped);
    if (result != VK_SUCCESS)
    {
        return result;
    }
    if (mapped.pMappedData == nullptr)
    {
        return VK_ERROR_MEMORY_MAP_FAILED;
    }
    std::memcpy(mapped.pMappedData, bytes.data(), bytes.size());
    return vmaFlushAllocation(gRasterAllocator, buffer.Allocation, 0, bytes.size());
}
VkDescriptorType RasterDescriptor(RasterBindingKind kind) noexcept
{
    return kind == RasterBindingKind::UniformBuffer ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
           : kind == RasterBindingKind::Texture2D   ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                                                    : VK_DESCRIPTOR_TYPE_SAMPLER;
}
VkFormat RasterPixelFormat(RasterFormat format) noexcept
{
    return format == RasterFormat::Rgba8Unorm ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8G8B8A8_SRGB;
}
} // namespace
RasterCapabilities RasterLimits() noexcept
{
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(gPhysical, &properties);
    const auto max = properties.limits.maxImageDimension2D;
    const usize alignment = properties.limits.minUniformBufferOffsetAlignment;
    return {usize{64} * 1024 * 1024,
            max < 4096 ? max : 4096,
            alignment > 256 ? alignment : 256,
            properties.limits.maxUniformBufferRange < 16384 ? properties.limits.maxUniformBufferRange : 16384,
            static_cast<uint32>(internal::RASTER_CAPACITY),
            static_cast<uint32>(internal::RASTER_DRAWS)};
}
RasterStatus
RasterCreateBuffer(usize slot, const BufferDescription& info, std::span<const uint8> bytes, uint32) noexcept
{
    const auto usage = info.Role == BufferRole::Uniform  ? VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
                       : info.Role == BufferRole::Vertex ? VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
                                                         : VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    return RasterError(RasterUploadBuffer(usage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, bytes, gRasterBuffers[slot]));
}
RasterStatus
RasterCreateTexture(usize slot, const TextureDescription& info, const TextureUpload& upload, uint32 request) noexcept
{
    VkFormatProperties properties{};
    const auto format = RasterPixelFormat(info.Format);
    vkGetPhysicalDeviceFormatProperties(gPhysical, format, &properties);
    constexpr VkFormatFeatureFlags required = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                              VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
                                              VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    const auto features = required | (info.Attachment ? VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT : 0U);
    if ((properties.optimalTilingFeatures & features) != features)
    {
        return RasterStatus::Unsupported;
    }
    auto result = RasterAllocator();
    if (result != VK_SUCCESS)
    {
        return RasterError(result);
    }
    auto& texture = gRasterTextures[slot];
    texture.Info = info;
    VkImageCreateInfo image{};
    image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image.imageType = VK_IMAGE_TYPE_2D;
    image.format = format;
    image.extent = {info.Width, info.Height, 1};
    image.mipLevels = 1;
    image.arrayLayers = 1;
    image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                  (info.Attachment ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : 0U);
    VmaAllocationCreateInfo allocation{};
    allocation.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    result = vmaCreateImage(gRasterAllocator, &image, &allocation, &texture.Object, &texture.Allocation, nullptr);
    if (result != VK_SUCCESS)
    {
        return RasterError(result);
    }
    if (info.Attachment)
    {
        image.format = gDepthFormat;
        image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        result =
            vmaCreateImage(gRasterAllocator, &image, &allocation, &texture.Depth, &texture.DepthAllocation, nullptr);
        VkImageViewCreateInfo view{};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.image = texture.Object;
        view.format = format;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (result == VK_SUCCESS)
        {
            result = vkCreateImageView(gDevice, &view, nullptr, &texture.ColorView);
        }
        view.image = texture.Depth;
        view.format = gDepthFormat;
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (result == VK_SUCCESS)
        {
            result = vkCreateImageView(gDevice, &view, nullptr, &texture.DepthView);
        }
        VkRenderPass pass = VK_NULL_HANDLE;
        if (result == VK_SUCCESS)
        {
            result = RasterVkRenderPass(format, false, {}, pass);
        }
        VkFramebufferCreateInfo framebuffer{};
        framebuffer.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        const VkImageView views[2]{texture.ColorView, texture.DepthView};
        framebuffer.renderPass = pass;
        framebuffer.attachmentCount = 2;
        framebuffer.pAttachments = views;
        framebuffer.width = info.Width;
        framebuffer.height = info.Height;
        framebuffer.layers = 1;
        if (result == VK_SUCCESS)
        {
            result = vkCreateFramebuffer(gDevice, &framebuffer, nullptr, &texture.Framebuffer);
        }
        return RasterError(result);
    }
    texture.ColorLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    texture.ColorStage = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    texture.ColorAccess = VK_ACCESS_SHADER_READ_BIT;
    texture.ColorWriterStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    texture.ColorWriterAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
    texture.SampleVisibility = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    RasterVulkanBuffer staging;
    result = RasterUploadBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, upload.Bytes, staging);
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    if (result == VK_SUCCESS)
    {
        VkCommandBufferAllocateInfo allocate{};
        allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocate.commandPool = gCommands;
        allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate.commandBufferCount = 1;
        result = vkAllocateCommandBuffers(gDevice, &allocate, &command);
    }
    if (result == VK_SUCCESS)
    {
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        result = vkBeginCommandBuffer(command, &begin);
    }
    if (result == VK_SUCCESS)
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.image = texture.Object;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(command,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0,
                             0,
                             nullptr,
                             0,
                             nullptr,
                             1,
                             &barrier);
        VkBufferImageCopy copy{};
        copy.bufferRowLength = static_cast<uint32>(upload.RowPitch / 4);
        copy.bufferImageHeight = info.Height;
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {info.Width, info.Height, 1};
        vkCmdCopyBufferToImage(command, staging.Object, texture.Object, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(command,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0,
                             0,
                             nullptr,
                             0,
                             nullptr,
                             1,
                             &barrier);
        result = vkEndCommandBuffer(command);
    }
    if (result == VK_SUCCESS)
    {
        VkFenceCreateInfo create{};
        create.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        result = vkCreateFence(gDevice, &create, nullptr, &fence);
    }
    bool submitted = false;
    if (result == VK_SUCCESS)
    {
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &command;
        result = vkQueueSubmit(gQueue, 1, &submit, fence);
        submitted = result == VK_SUCCESS;
    }
    if (submitted)
    {
        result = vkWaitForFences(gDevice, 1, &fence, VK_TRUE, 5000000000ULL);
    }
    // Timeout or host-memory pressure does not establish GPU completion. Keep
    // the staging allocation and command until the fence signals; device loss
    // permits immediate teardown. Never infer completion from an unchecked wait.
    if (submitted && result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST)
    {
        texture.Upload = staging;
        texture.Command = command;
        texture.Fence = fence;
        texture.Request = request;
        internal::RasterExpect(request, internal::RasterCallbacks::One);
        return RasterStatus::Pending;
    }
    if (command != VK_NULL_HANDLE)
    {
        vkFreeCommandBuffers(gDevice, gCommands, 1, &command);
    }
    vkDestroyFence(gDevice, fence, nullptr);
    if (staging.Object != VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(gRasterAllocator, staging.Object, staging.Allocation);
    }
    return RasterError(result);
}
RasterStatus RasterCreateView(usize slot, const internal::RasterViewSource& source, uint32) noexcept
{
    VkImageViewCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    info.image = gRasterTextures[source.Texture].Object;
    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.format = RasterPixelFormat(gRasterTextures[source.Texture].Info.Format);
    info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    return RasterError(vkCreateImageView(gDevice, &info, nullptr, &gRasterViews[slot]));
}
RasterStatus RasterCreateSampler(usize slot, const SamplerDescription& info, uint32) noexcept
{
    VkSamplerCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    create.minFilter = create.magFilter = info.Filter == RasterFilter::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    create.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    create.addressModeU =
        info.U == RasterAddress::Clamp ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT;
    create.addressModeV =
        info.V == RasterAddress::Clamp ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT;
    create.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    return RasterError(vkCreateSampler(gDevice, &create, nullptr, &gRasterSamplers[slot]));
}
RasterStatus
RasterCreateShader(usize slot, const ShaderDescription& info, const internal::RasterShaderInfo&, uint32) noexcept
{
    bool found = false;
    for (usize offset = 5; offset < info.Spirv.size();)
    {
        const auto word = info.Spirv[offset];
        const usize count = word >> 16;
        if (count == 0 || count > info.Spirv.size() - offset)
        {
            return RasterStatus::InvalidDescription;
        }
        if ((word & 65535U) == 15 && count >= 4)
        {
            const auto* name = reinterpret_cast<const char*>(info.Spirv.data() + offset + 3);
            const auto length = info.SpirvEntry.size();
            found = found || (info.Spirv[offset + 1] == (info.Stage == ShaderStage::Vertex ? 0U : 4U) &&
                              length < (count - 3) * sizeof(uint32) && name[length] == '\0' &&
                              std::memcmp(name, info.SpirvEntry.data(), length) == 0);
        }
        offset += count;
    }
    if (!found)
    {
        return RasterStatus::InvalidDescription;
    }
    auto& shader = gRasterShaders[slot];
    std::memcpy(shader.Entry, info.SpirvEntry.data(), info.SpirvEntry.size());
    VkShaderModuleCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create.codeSize = info.Spirv.size_bytes();
    create.pCode = info.Spirv.data();
    return RasterError(vkCreateShaderModule(gDevice, &create, nullptr, &shader.Object));
}
RasterStatus RasterCreateLayout(usize slot, const internal::RasterLayout& info, uint32) noexcept
{
    VkDescriptorSetLayoutBinding entries[8]{};
    for (usize i = 0; i < info.Count; ++i)
    {
        const auto& entry = info.Entries[i];
        entries[i].binding = entry.Binding;
        entries[i].descriptorCount = 1;
        entries[i].descriptorType = RasterDescriptor(entry.Kind);
        if ((static_cast<uint8>(entry.Visibility) & 1) != 0)
        {
            entries[i].stageFlags |= VK_SHADER_STAGE_VERTEX_BIT;
        }
        if ((static_cast<uint8>(entry.Visibility) & 2) != 0)
        {
            entries[i].stageFlags |= VK_SHADER_STAGE_FRAGMENT_BIT;
        }
    }
    VkDescriptorSetLayoutCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    create.bindingCount = static_cast<uint32>(info.Count);
    create.pBindings = entries;
    return RasterError(vkCreateDescriptorSetLayout(gDevice, &create, nullptr, &gRasterLayouts[slot]));
}
RasterStatus
RasterCreateSet(usize slot, const internal::RasterLayout& layout, const internal::RasterSet& info, uint32) noexcept
{
    auto& set = gRasterSets[slot];
    VkDescriptorPoolSize sizes[3]{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0},
                                  {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 0},
                                  {VK_DESCRIPTOR_TYPE_SAMPLER, 0}};
    for (usize i = 0; i < layout.Count; ++i)
    {
        ++sizes[static_cast<usize>(layout.Entries[i].Kind)].descriptorCount;
    }
    VkDescriptorPoolSize used[3]{};
    uint32 count = 0;
    for (const auto& size : sizes)
    {
        if (size.descriptorCount != 0)
        {
            used[count++] = size;
        }
    }
    // Empty layouts still need a legal pool for their one empty descriptor set.
    if (count == 0)
    {
        used[count++] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1};
    }
    VkDescriptorPoolCreateInfo pool{};
    pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool.maxSets = 1;
    pool.poolSizeCount = count;
    pool.pPoolSizes = used;
    auto result = vkCreateDescriptorPool(gDevice, &pool, nullptr, &set.Pool);
    if (result != VK_SUCCESS)
    {
        return RasterError(result);
    }
    VkDescriptorSetAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate.descriptorPool = set.Pool;
    allocate.descriptorSetCount = 1;
    allocate.pSetLayouts = &gRasterLayouts[info.Layout];
    result = vkAllocateDescriptorSets(gDevice, &allocate, &set.Object);
    if (result != VK_SUCCESS)
    {
        return RasterError(result);
    }
    VkWriteDescriptorSet writes[8]{};
    VkDescriptorBufferInfo buffers[8]{};
    VkDescriptorImageInfo images[8]{};
    for (usize i = 0; i < layout.Count; ++i)
    {
        const auto& entry = layout.Entries[i];
        auto& write = writes[i];
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = set.Object;
        write.dstBinding = entry.Binding;
        write.descriptorCount = 1;
        write.descriptorType = RasterDescriptor(entry.Kind);
        if (entry.Kind == RasterBindingKind::UniformBuffer)
        {
            buffers[i] = {gRasterBuffers[info.Slots[i]].Object, info.Offsets[i], info.Sizes[i]};
            write.pBufferInfo = &buffers[i];
        }
        else
        {
            if (entry.Kind == RasterBindingKind::Texture2D)
            {
                images[i].imageView = gRasterViews[info.Slots[i]];
                images[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }
            else
            {
                images[i].sampler = gRasterSamplers[info.Slots[i]];
            }
            write.pImageInfo = &images[i];
        }
    }
    vkUpdateDescriptorSets(gDevice, static_cast<uint32>(layout.Count), writes, 0, nullptr);
    return RasterStatus::Ready;
}
RasterStatus RasterCreatePipeline(usize slot, const internal::RasterPipelineInfo& info, uint32) noexcept
{
    auto& pipeline = gRasterPipelines[slot];
    pipeline.Info = info;
    VkPipelineLayoutCreateInfo layout{};
    layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &gRasterLayouts[info.Layout];
    auto result = vkCreatePipelineLayout(gDevice, &layout, nullptr, &pipeline.Layout);
    if (result != VK_SUCCESS)
    {
        return RasterError(result);
    }
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[0].module = gRasterShaders[info.Vertex].Object;
    stages[1].module = gRasterShaders[info.Fragment].Object;
    stages[0].pName = gRasterShaders[info.Vertex].Entry;
    stages[1].pName = gRasterShaders[info.Fragment].Entry;
    VkVertexInputBindingDescription bindings[2]{};
    VkVertexInputAttributeDescription attributes[8]{};
    for (usize i = 0; i < info.StreamCount; ++i)
    {
        bindings[i] = {static_cast<uint32>(i),
                       info.Streams[i].Stride,
                       info.Streams[i].PerInstance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX};
    }
    for (usize i = 0; i < info.AttributeCount; ++i)
    {
        const auto& entry = info.Attributes[i];
        attributes[i] = {entry.Location,
                         entry.Stream,
                         entry.Format == RasterVertexFormat::Float2   ? VK_FORMAT_R32G32_SFLOAT
                         : entry.Format == RasterVertexFormat::Float3 ? VK_FORMAT_R32G32B32_SFLOAT
                                                                      : VK_FORMAT_R32G32B32A32_SFLOAT,
                         entry.Offset};
    }
    VkPipelineVertexInputStateCreateInfo input{};
    input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    input.vertexBindingDescriptionCount = static_cast<uint32>(info.StreamCount);
    input.pVertexBindingDescriptions = bindings;
    input.vertexAttributeDescriptionCount = static_cast<uint32>(info.AttributeCount);
    input.pVertexAttributeDescriptions = attributes;
    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo samples{};
    samples.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = info.Depth;
    depth.depthWriteEnable = info.Depth && info.DepthWrite;
    depth.depthCompareOp = VK_COMPARE_OP_LESS;
    VkPipelineColorBlendAttachmentState color{};
    color.colorWriteMask = 15;
    color.blendEnable = info.Blend;
    color.srcColorBlendFactor = color.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    color.dstColorBlendFactor = color.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &color;
    const VkDynamicState states[2]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = states;
    VkGraphicsPipelineCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    create.stageCount = 2;
    create.pStages = stages;
    create.pVertexInputState = &input;
    create.pInputAssemblyState = &assembly;
    create.pViewportState = &viewport;
    create.pRasterizationState = &raster;
    create.pMultisampleState = &samples;
    create.pDepthStencilState = &depth;
    create.pColorBlendState = &blend;
    create.pDynamicState = &dynamic;
    create.layout = pipeline.Layout;
    create.renderPass = gPass;
    if (info.Target != RasterTarget::Surface)
    {
        result = RasterVkRenderPass(info.Target == RasterTarget::Rgba8Unorm ? VK_FORMAT_R8G8B8A8_UNORM
                                                                            : VK_FORMAT_R8G8B8A8_SRGB,
                                    false,
                                    {},
                                    create.renderPass);
        if (result != VK_SUCCESS)
        {
            return RasterError(result);
        }
    }
    return RasterError(vkCreateGraphicsPipelines(gDevice, VK_NULL_HANDLE, 1, &create, nullptr, &pipeline.Object));
}
void RasterDestroy(internal::RasterKind kind, usize slot) noexcept
{
    if (gDevice == VK_NULL_HANDLE)
    {
        return;
    }
    switch (kind)
    {
        case internal::RasterKind::Buffer:
            if (gRasterBuffers[slot].Object != VK_NULL_HANDLE)
            {
                vmaDestroyBuffer(gRasterAllocator, gRasterBuffers[slot].Object, gRasterBuffers[slot].Allocation);
            }
            gRasterBuffers[slot] = {};
            break;
        case internal::RasterKind::Texture:
            vkDestroyFramebuffer(gDevice, gRasterTextures[slot].Framebuffer, nullptr);
            vkDestroyImageView(gDevice, gRasterTextures[slot].ColorView, nullptr);
            vkDestroyImageView(gDevice, gRasterTextures[slot].DepthView, nullptr);
            if (gRasterTextures[slot].Depth != VK_NULL_HANDLE)
            {
                vmaDestroyImage(gRasterAllocator, gRasterTextures[slot].Depth, gRasterTextures[slot].DepthAllocation);
            }
            if (gRasterTextures[slot].Command != VK_NULL_HANDLE)
            {
                vkFreeCommandBuffers(gDevice, gCommands, 1, &gRasterTextures[slot].Command);
                vkDestroyFence(gDevice, gRasterTextures[slot].Fence, nullptr);
                const auto& upload = gRasterTextures[slot].Upload;
                vmaDestroyBuffer(gRasterAllocator, upload.Object, upload.Allocation);
            }
            if (gRasterTextures[slot].Object != VK_NULL_HANDLE)
            {
                vmaDestroyImage(gRasterAllocator, gRasterTextures[slot].Object, gRasterTextures[slot].Allocation);
            }
            gRasterTextures[slot] = {};
            break;
        case internal::RasterKind::View:
            vkDestroyImageView(gDevice, gRasterViews[slot], nullptr);
            gRasterViews[slot] = VK_NULL_HANDLE;
            break;
        case internal::RasterKind::Sampler:
            vkDestroySampler(gDevice, gRasterSamplers[slot], nullptr);
            gRasterSamplers[slot] = VK_NULL_HANDLE;
            break;
        case internal::RasterKind::Shader:
            vkDestroyShaderModule(gDevice, gRasterShaders[slot].Object, nullptr);
            gRasterShaders[slot] = {};
            break;
        case internal::RasterKind::Layout:
            vkDestroyDescriptorSetLayout(gDevice, gRasterLayouts[slot], nullptr);
            gRasterLayouts[slot] = VK_NULL_HANDLE;
            break;
        case internal::RasterKind::Set:
            vkDestroyDescriptorPool(gDevice, gRasterSets[slot].Pool, nullptr);
            gRasterSets[slot] = {};
            break;
        case internal::RasterKind::Pipeline:
            vkDestroyPipeline(gDevice, gRasterPipelines[slot].Object, nullptr);
            vkDestroyPipelineLayout(gDevice, gRasterPipelines[slot].Layout, nullptr);
            gRasterPipelines[slot] = {};
            break;
        case internal::RasterKind::Count:
            break;
    }
}
RasterStatus RasterDraw(const internal::RasterPacket& packet) noexcept
{
    const auto& pipeline = gRasterPipelines[packet.Pipeline];
    const auto command = gFrames[gFrame].Command;
    const auto extent = gRasterExtent.width == 0 ? gExtent : gRasterExtent;
    // Canonical Y-up clip coordinates, top-left pixel/readback coordinates.
    const VkViewport viewport{0,
                              static_cast<float32>(extent.height),
                              static_cast<float32>(extent.width),
                              -static_cast<float32>(extent.height),
                              0,
                              1};
    const VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &scissor);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.Object);
    vkCmdBindDescriptorSets(command,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline.Layout,
                            0,
                            1,
                            &gRasterSets[packet.Set].Object,
                            0,
                            nullptr);
    for (usize i = 0; i < pipeline.Info.StreamCount; ++i)
    {
        const VkDeviceSize offset = packet.Offsets[i];
        vkCmdBindVertexBuffers(command, static_cast<uint32>(i), 1, &gRasterBuffers[packet.Vertices[i]].Object, &offset);
    }
    vkCmdBindIndexBuffer(command,
                         gRasterBuffers[packet.Indices].Object,
                         packet.IndexOffset,
                         packet.Index32 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexed(command, packet.IndexCount, packet.InstanceCount, 0, 0, 0);
    return RasterStatus::Ready;
}
void RasterFrameExtent() noexcept
{
    gRasterExtent = {};
    gRasterActiveTexture = internal::RASTER_CAPACITY;
}
void RasterEndPass() noexcept
{
    if (!gEncoding)
    {
        return;
    }
    vkCmdEndRenderPass(gFrames[gFrame].Command);
    gEncoding = false;
    if (gRasterActiveTexture != internal::RASTER_CAPACITY)
    {
        auto& texture = gRasterTextures[gRasterActiveTexture];
        texture.ColorLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        texture.ColorStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        texture.ColorAccess = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        texture.ColorWriterStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        texture.ColorWriterAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        texture.SampleVisibility = 0;
        texture.DepthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }
}
void RasterTextureBarrier(usize slot, RasterTextureUse use) noexcept
{
    auto& texture = gRasterTextures[slot];
    const bool color = use == RasterTextureUse::ColorAttachment;
    const VkPipelineStageFlags stage =
        color                                    ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
        : use == RasterTextureUse::SampledVertex ? VK_PIPELINE_STAGE_VERTEX_SHADER_BIT
        : use == RasterTextureUse::SampledFragment
            ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
            : VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    const auto access =
        color ? VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT : VK_ACCESS_SHADER_READ_BIT;
    const auto layout = color ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    if (texture.ColorLayout == layout && (texture.SampleVisibility & stage) == stage && !color)
    {
        texture.ColorStage |= stage;
        return;
    }
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = texture.ColorLayout;
    barrier.newLayout = layout;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.srcAccessMask = texture.ColorWriterAccess | texture.ColorAccess;
    barrier.dstAccessMask = access;
    barrier.image = texture.Object;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(gFrames[gFrame].Command,
                         texture.ColorStage | texture.ColorWriterStage,
                         stage,
                         0,
                         0,
                         nullptr,
                         0,
                         nullptr,
                         1,
                         &barrier);
    texture.ColorLayout = layout;
    texture.ColorStage = color ? stage : texture.ColorStage | stage;
    texture.ColorAccess = access;
    if (!color)
    {
        texture.SampleVisibility |= stage;
    }
}
RasterStatus RasterPreparePass(const internal::RasterPassInfo& info) noexcept
{
    VkRenderPass pass = VK_NULL_HANDLE;
    const bool surface = info.Texture == internal::RASTER_CAPACITY;
    const auto format = surface ? gFormat : RasterPixelFormat(gRasterTextures[info.Texture].Info.Format);
    const auto result = RasterVkRenderPass(format, surface, info.Description, pass);
    return result == VK_ERROR_TOO_MANY_OBJECTS ? RasterStatus::CapacityExceeded : RasterError(result);
}
RasterStatus RasterBeginPass(const internal::RasterPassInfo& info) noexcept
{
    const bool surface = info.Texture == internal::RASTER_CAPACITY;
    VkRenderPass pass = VK_NULL_HANDLE;
    const auto format = surface ? gFormat : RasterPixelFormat(gRasterTextures[info.Texture].Info.Format);
    const auto prepared = RasterVkRenderPass(format, surface, info.Description, pass);
    if (prepared != VK_SUCCESS)
    {
        return RasterError(prepared);
    }
    VkFramebuffer framebuffer = surface ? gTargets[gImage] : gRasterTextures[info.Texture].Framebuffer;
    gRasterExtent =
        surface ? gExtent
                : VkExtent2D{gRasterTextures[info.Texture].Info.Width, gRasterTextures[info.Texture].Info.Height};
    gRasterActiveTexture = info.Texture;
    if (!surface)
    {
        RasterTextureBarrier(info.Texture, RasterTextureUse::ColorAttachment);
        auto& texture = gRasterTextures[info.Texture];
        VkImageMemoryBarrier depth{};
        depth.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        depth.oldLayout = texture.DepthLayout;
        depth.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depth.srcQueueFamilyIndex = depth.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depth.srcAccessMask =
            texture.DepthLayout == VK_IMAGE_LAYOUT_UNDEFINED ? 0U : VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        depth.dstAccessMask =
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        depth.image = texture.Depth;
        depth.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
        const auto stages = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        vkCmdPipelineBarrier(gFrames[gFrame].Command,
                             texture.DepthLayout == VK_IMAGE_LAYOUT_UNDEFINED ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                                                                              : stages,
                             stages,
                             0,
                             0,
                             nullptr,
                             0,
                             nullptr,
                             1,
                             &depth);
    }
    VkClearValue clear[2]{};
    for (usize i = 0; i < 4; ++i)
    {
        clear[0].color.float32[i] = info.Description.Clear[i];
    }
    clear[1].depthStencil = {1, 0};
    VkRenderPassBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin.renderPass = pass;
    begin.framebuffer = framebuffer;
    begin.renderArea.extent = gRasterExtent;
    begin.clearValueCount = 2;
    begin.pClearValues = clear;
    vkCmdBeginRenderPass(gFrames[gFrame].Command, &begin, VK_SUBPASS_CONTENTS_INLINE);
    gEncoding = true;
    return RasterStatus::Ready;
}
RasterStatus RasterReserveSubmission() noexcept
{
    return RasterStatus::Ready;
}
void RasterSubmit(uint64 ordinal) noexcept
{
    gRasterOrdinals[(gFrame + FRAMES - 1) % FRAMES] = ordinal;
}
uint64 RasterCompleted() noexcept
{
    if (gDevice == VK_NULL_HANDLE)
    {
        return gRasterCompleted;
    }
    for (auto& texture : gRasterTextures)
    {
        if (texture.Fence == VK_NULL_HANDLE)
        {
            continue;
        }
        const auto result = vkGetFenceStatus(gDevice, texture.Fence);
        if (result != VK_SUCCESS && result != VK_NOT_READY)
        {
            internal::Fail(gSession,
                           result == VK_ERROR_DEVICE_LOST ? StartupError::DeviceLost
                                                          : StartupError::RenderingUnavailable);
            return gRasterCompleted;
        }
        if (result == VK_SUCCESS)
        {
            vkFreeCommandBuffers(gDevice, gCommands, 1, &texture.Command);
            vkDestroyFence(gDevice, texture.Fence, nullptr);
            vmaDestroyBuffer(gRasterAllocator, texture.Upload.Object, texture.Upload.Allocation);
            texture.Upload = {};
            texture.Command = VK_NULL_HANDLE;
            texture.Fence = VK_NULL_HANDLE;
            internal::RasterComplete(texture.Request, RasterStatus::Ready);
        }
    }
    for (usize i = 0; i < FRAMES; ++i)
    {
        if (gFrames[i].Fence == VK_NULL_HANDLE)
        {
            continue;
        }
        const auto result = vkGetFenceStatus(gDevice, gFrames[i].Fence);
        if (result != VK_SUCCESS && result != VK_NOT_READY)
        {
            internal::Fail(gSession,
                           result == VK_ERROR_DEVICE_LOST ? StartupError::DeviceLost
                                                          : StartupError::RenderingUnavailable);
            return gRasterCompleted;
        }
        if (result == VK_SUCCESS && gRasterOrdinals[i] > gRasterCompleted)
        {
            gRasterCompleted = gRasterOrdinals[i];
        }
    }
    return gRasterCompleted;
}
void RasterReset() noexcept
{
    WaitIdle();
    for (auto& ordinal : gRasterOrdinals)
    {
        ordinal = 0;
    }
    gRasterCompleted = 0;
}
void RasterShutdown() noexcept
{
    for (auto& pass : gRasterPasses)
    {
        if (gDevice != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(gDevice, pass.Object, nullptr);
        }
        pass = {};
    }
    if (gRasterAllocator != nullptr)
    {
        vmaDestroyAllocator(gRasterAllocator);
        gRasterAllocator = nullptr;
    }
}
} // namespace ludus::graphics::rhi::backend
