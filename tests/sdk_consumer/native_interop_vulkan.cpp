#include "native_interop_fixture.h"
#include "native_sample.h"

#include <ludus/foundation/base/core.h>

#include <cstring>
#include <new>

#define VK_NO_PROTOTYPES
#include <dlfcn.h>
#include <vulkan/vulkan.h>

namespace native_fixture
{
namespace
{
using namespace ludus::foundation;
#define NATIVE_DEVICE_API(X)                                                                                           \
    X(vkDestroyDevice)                                                                                                 \
    X(vkGetDeviceQueue)                                                                                                \
    X(vkDeviceWaitIdle)                                                                                                \
    X(vkCreateImage)                                                                                                   \
    X(vkDestroyImage)                                                                                                  \
    X(vkGetImageMemoryRequirements)                                                                                    \
    X(vkAllocateMemory)                                                                                                \
    X(vkFreeMemory)                                                                                                    \
    X(vkBindImageMemory)                                                                                               \
    X(vkCreateImageView)                                                                                               \
    X(vkDestroyImageView)                                                                                              \
    X(vkCreateBuffer)                                                                                                  \
    X(vkDestroyBuffer)                                                                                                 \
    X(vkGetBufferMemoryRequirements)                                                                                   \
    X(vkBindBufferMemory)                                                                                              \
    X(vkMapMemory)                                                                                                     \
    X(vkUnmapMemory)                                                                                                   \
    X(vkCreateShaderModule)                                                                                            \
    X(vkDestroyShaderModule)                                                                                           \
    X(vkCreateSampler)                                                                                                 \
    X(vkDestroySampler)                                                                                                \
    X(vkCreateDescriptorSetLayout)                                                                                     \
    X(vkDestroyDescriptorSetLayout)                                                                                    \
    X(vkCreateDescriptorPool)                                                                                          \
    X(vkDestroyDescriptorPool)                                                                                         \
    X(vkAllocateDescriptorSets)                                                                                        \
    X(vkUpdateDescriptorSets)                                                                                          \
    X(vkCreateRenderPass)                                                                                              \
    X(vkDestroyRenderPass)                                                                                             \
    X(vkCreateFramebuffer)                                                                                             \
    X(vkDestroyFramebuffer)                                                                                            \
    X(vkCreatePipelineLayout)                                                                                          \
    X(vkDestroyPipelineLayout)                                                                                         \
    X(vkCreateGraphicsPipelines)                                                                                       \
    X(vkDestroyPipeline)                                                                                               \
    X(vkCreateCommandPool)                                                                                             \
    X(vkDestroyCommandPool)                                                                                            \
    X(vkAllocateCommandBuffers)                                                                                        \
    X(vkBeginCommandBuffer)                                                                                            \
    X(vkEndCommandBuffer)                                                                                              \
    X(vkCmdBeginRenderPass)                                                                                            \
    X(vkCmdEndRenderPass)                                                                                              \
    X(vkCmdBindPipeline)                                                                                               \
    X(vkCmdBindDescriptorSets)                                                                                         \
    X(vkCmdDraw)                                                                                                       \
    X(vkCmdPipelineBarrier)                                                                                            \
    X(vkCmdCopyImageToBuffer)                                                                                          \
    X(vkCreateFence)                                                                                                   \
    X(vkDestroyFence)                                                                                                  \
    X(vkQueueSubmit)                                                                                                   \
    X(vkWaitForFences)
struct DeviceApi final
{
#define DECLARE_NATIVE_API(name) PFN_##name name = nullptr;
    NATIVE_DEVICE_API(DECLARE_NATIVE_API)
#undef DECLARE_NATIVE_API
};
struct Source final
{
    void* Loader = nullptr;
    PFN_vkGetInstanceProcAddr GetInstance = nullptr;
    PFN_vkDestroyInstance DestroyInstance = nullptr;
    VkInstance Instance = VK_NULL_HANDLE;
    VkPhysicalDevice Physical = VK_NULL_HANDLE;
    VkDevice Device = VK_NULL_HANDLE;
    VkQueue Queue = VK_NULL_HANDLE;
    uint32 Family = 0;
    DeviceApi Api;
    VkPhysicalDeviceMemoryProperties Memory{};
    usize Retained = 0, Released = 0;
    VkImage Target = VK_NULL_HANDLE;
    VkDeviceMemory TargetMemory = VK_NULL_HANDLE, OutputMemory = VK_NULL_HANDLE;
    VkImageView SourceView = VK_NULL_HANDLE, TargetView = VK_NULL_HANDLE;
    VkBuffer Output = VK_NULL_HANDLE;
    VkSampler Sampler = VK_NULL_HANDLE;
    VkShaderModule Shaders[2]{};
    VkDescriptorSetLayout SetLayout = VK_NULL_HANDLE;
    VkDescriptorPool Descriptors = VK_NULL_HANDLE;
    VkDescriptorSet Set = VK_NULL_HANDLE;
    VkRenderPass Pass = VK_NULL_HANDLE;
    VkFramebuffer Frame = VK_NULL_HANDLE;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    VkPipeline Pipeline = VK_NULL_HANDLE;
    VkCommandPool Commands = VK_NULL_HANDLE;
    VkCommandBuffer Command = VK_NULL_HANDLE;
    VkFence Fence = VK_NULL_HANDLE;
    static bool Retain(void* context) noexcept
    {
        ++static_cast<Source*>(context)->Retained;
        return true;
    }
    static void Release(void* context) noexcept
    {
        ++static_cast<Source*>(context)->Released;
    }
};
#define NATIVE_VK(expression)                                                                                          \
    do                                                                                                                 \
    {                                                                                                                  \
        if ((expression) != VK_SUCCESS)                                                                                \
            return false;                                                                                              \
    } while (false)
bool Allocate(Source& source,
              const VkMemoryRequirements& requirements,
              VkMemoryPropertyFlags flags,
              VkDeviceMemory& output) noexcept
{
    for (uint32 i = 0; i < source.Memory.memoryTypeCount; ++i)
    {
        if ((requirements.memoryTypeBits & (1U << i)) != 0 &&
            (source.Memory.memoryTypes[i].propertyFlags & flags) == flags)
        {
            VkMemoryAllocateInfo allocate{};
            allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocate.allocationSize = requirements.size;
            allocate.memoryTypeIndex = i;
            return source.Api.vkAllocateMemory(source.Device, &allocate, nullptr, &output) == VK_SUCCESS;
        }
    }
    return false;
}
void RetireSample(Source& source) noexcept
{
    if (source.Device == VK_NULL_HANDLE)
    {
        return;
    }
    auto& api = source.Api;
    api.vkDestroyCommandPool(source.Device, source.Commands, nullptr);
    api.vkDestroyFence(source.Device, source.Fence, nullptr);
    api.vkDestroyPipeline(source.Device, source.Pipeline, nullptr);
    api.vkDestroyPipelineLayout(source.Device, source.PipelineLayout, nullptr);
    api.vkDestroyFramebuffer(source.Device, source.Frame, nullptr);
    api.vkDestroyRenderPass(source.Device, source.Pass, nullptr);
    api.vkDestroyDescriptorPool(source.Device, source.Descriptors, nullptr);
    api.vkDestroyDescriptorSetLayout(source.Device, source.SetLayout, nullptr);
    for (auto shader : source.Shaders)
    {
        api.vkDestroyShaderModule(source.Device, shader, nullptr);
    }
    api.vkDestroySampler(source.Device, source.Sampler, nullptr);
    api.vkDestroyImageView(source.Device, source.SourceView, nullptr);
    api.vkDestroyImageView(source.Device, source.TargetView, nullptr);
    api.vkDestroyImage(source.Device, source.Target, nullptr);
    api.vkFreeMemory(source.Device, source.TargetMemory, nullptr);
    api.vkDestroyBuffer(source.Device, source.Output, nullptr);
    api.vkFreeMemory(source.Device, source.OutputMemory, nullptr);
    source.Commands = VK_NULL_HANDLE;
    source.Fence = VK_NULL_HANDLE;
    source.Pipeline = VK_NULL_HANDLE;
    source.PipelineLayout = VK_NULL_HANDLE;
    source.Frame = VK_NULL_HANDLE;
    source.Pass = VK_NULL_HANDLE;
    source.Descriptors = VK_NULL_HANDLE;
    source.SetLayout = VK_NULL_HANDLE;
    for (auto& shader : source.Shaders)
    {
        shader = VK_NULL_HANDLE;
    }
    source.Sampler = VK_NULL_HANDLE;
    source.SourceView = source.TargetView = VK_NULL_HANDLE;
    source.Target = VK_NULL_HANDLE;
    source.TargetMemory = VK_NULL_HANDLE;
    source.Output = VK_NULL_HANDLE;
    source.OutputMemory = VK_NULL_HANDLE;
}
} // namespace
bool CreateForeign(Foreign& fixture) noexcept
{
    auto* source = new (std::nothrow) Source;
    if (source == nullptr)
    {
        return false;
    }
    fixture.Context = source;
    // An independent loader reference and function table survive the RHI's own
    // loader teardown. This consumer uses only public native and installed SDK APIs.
    source->Loader = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (source->Loader == nullptr)
    {
        return false;
    }
    source->GetInstance = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(source->Loader, "vkGetInstanceProcAddr"));
    if (source->GetInstance == nullptr)
    {
        return false;
    }
    const auto createInstance =
        reinterpret_cast<PFN_vkCreateInstance>(source->GetInstance(nullptr, "vkCreateInstance"));
    VkApplicationInfo application{};
    application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo instance{};
    instance.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance.pApplicationInfo = &application;
    NATIVE_VK(createInstance(&instance, nullptr, &source->Instance));
    source->DestroyInstance =
        reinterpret_cast<PFN_vkDestroyInstance>(source->GetInstance(source->Instance, "vkDestroyInstance"));
    const auto enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
        source->GetInstance(source->Instance, "vkEnumeratePhysicalDevices"));
    const auto families = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
        source->GetInstance(source->Instance, "vkGetPhysicalDeviceQueueFamilyProperties"));
    const auto memory = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(
        source->GetInstance(source->Instance, "vkGetPhysicalDeviceMemoryProperties"));
    const auto createDevice =
        reinterpret_cast<PFN_vkCreateDevice>(source->GetInstance(source->Instance, "vkCreateDevice"));
    const auto getDevice =
        reinterpret_cast<PFN_vkGetDeviceProcAddr>(source->GetInstance(source->Instance, "vkGetDeviceProcAddr"));
    uint32 count = 0;
    NATIVE_VK(enumerate(source->Instance, &count, nullptr));
    if (count == 0 || count > 16)
    {
        return false;
    }
    VkPhysicalDevice devices[16]{};
    NATIVE_VK(enumerate(source->Instance, &count, devices));
    for (uint32 d = 0; d < count && source->Physical == VK_NULL_HANDLE; ++d)
    {
        uint32 n = 0;
        families(devices[d], &n, nullptr);
        if (n > 32)
        {
            continue;
        }
        VkQueueFamilyProperties queues[32]{};
        families(devices[d], &n, queues);
        for (uint32 q = 0; q < n; ++q)
        {
            if ((queues[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 && queues[q].queueCount != 0)
            {
                source->Physical = devices[d];
                source->Family = q;
                break;
            }
        }
    }
    if (source->Physical == VK_NULL_HANDLE)
    {
        return false;
    }
    float32 priority = 1;
    VkDeviceQueueCreateInfo queue{};
    queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue.queueFamilyIndex = source->Family;
    queue.queueCount = 1;
    queue.pQueuePriorities = &priority;
    VkDeviceCreateInfo device{};
    device.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device.queueCreateInfoCount = 1;
    device.pQueueCreateInfos = &queue;
    NATIVE_VK(createDevice(source->Physical, &device, nullptr, &source->Device));
#define LOAD_NATIVE_API(name) source->Api.name = reinterpret_cast<PFN_##name>(getDevice(source->Device, #name));
    NATIVE_DEVICE_API(LOAD_NATIVE_API)
#undef LOAD_NATIVE_API
    source->Api.vkGetDeviceQueue(source->Device, source->Family, 0, &source->Queue);
    memory(source->Physical, &source->Memory);
    fixture.Description.Identity.Api = ludus::graphics::rhi::Backend::Vulkan;
    fixture.Description.Identity.Instance = source->Instance;
    fixture.Description.Identity.PhysicalDevice = source->Physical;
    fixture.Description.Identity.Device = source->Device;
    fixture.Description.Identity.CommandQueue = source->Queue;
    fixture.Description.Identity.QueueFamily = source->Family;
    fixture.Description.InstanceApiVersion = VK_API_VERSION_1_1;
    fixture.Description.Lifetime = {source, Source::Retain, Source::Release};
    fixture.Description.SerializedOwnerQueue = true;
    return true;
}
void DestroyForeign(Foreign& fixture) noexcept
{
    auto* source = static_cast<Source*>(fixture.Context);
    if (source == nullptr)
    {
        return;
    }
    if (source->Device != VK_NULL_HANDLE)
    {
        (void)source->Api.vkDeviceWaitIdle(source->Device);
        RetireSample(*source);
        source->Api.vkDestroyDevice(source->Device, nullptr);
    }
    if (source->Instance != VK_NULL_HANDLE)
    {
        source->DestroyInstance(source->Instance, nullptr);
    }
    if (source->Loader != nullptr)
    {
        dlclose(source->Loader);
    }
    delete source;
    fixture = {};
}
bool SubmitSample(Foreign& fixture, const ludus::graphics::rhi::NativeTextureInfo& texture) noexcept
{
    auto& source = *static_cast<Source*>(fixture.Context);
    auto& api = source.Api;
#if VK_USE_64_BIT_PTR_DEFINES
    VkImage shared = VK_NULL_HANDLE;
    std::memcpy(reinterpret_cast<void*>(&shared), &texture.VulkanImage, sizeof(texture.VulkanImage));
#else
    const auto shared = static_cast<VkImage>(texture.VulkanImage);
#endif
    VkImageViewCreateInfo view{};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = shared;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = VK_FORMAT_R8G8B8A8_UNORM;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    NATIVE_VK(api.vkCreateImageView(source.Device, &view, nullptr, &source.SourceView));
    VkImageCreateInfo image{};
    image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image.imageType = VK_IMAGE_TYPE_2D;
    image.format = VK_FORMAT_R8G8B8A8_UNORM;
    image.extent = {4, 4, 1};
    image.mipLevels = image.arrayLayers = 1;
    image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    NATIVE_VK(api.vkCreateImage(source.Device, &image, nullptr, &source.Target));
    VkMemoryRequirements requirements{};
    api.vkGetImageMemoryRequirements(source.Device, source.Target, &requirements);
    if (!Allocate(source, requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, source.TargetMemory))
    {
        return false;
    }
    NATIVE_VK(api.vkBindImageMemory(source.Device, source.Target, source.TargetMemory, 0));
    view.image = source.Target;
    NATIVE_VK(api.vkCreateImageView(source.Device, &view, nullptr, &source.TargetView));
    VkBufferCreateInfo buffer{};
    buffer.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer.size = 64;
    buffer.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    NATIVE_VK(api.vkCreateBuffer(source.Device, &buffer, nullptr, &source.Output));
    api.vkGetBufferMemoryRequirements(source.Device, source.Output, &requirements);
    if (!Allocate(source,
                  requirements,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  source.OutputMemory))
    {
        return false;
    }
    NATIVE_VK(api.vkBindBufferMemory(source.Device, source.Output, source.OutputMemory, 0));
    VkSamplerCreateInfo sampler{};
    sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler.magFilter = sampler.minFilter = VK_FILTER_NEAREST;
    sampler.maxLod = 0;
    NATIVE_VK(api.vkCreateSampler(source.Device, &sampler, nullptr, &source.Sampler));
    const VkDescriptorSetLayoutBinding bindings[2]{
        {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
    VkDescriptorSetLayoutCreateInfo layout{};
    layout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout.bindingCount = 2;
    layout.pBindings = bindings;
    NATIVE_VK(api.vkCreateDescriptorSetLayout(source.Device, &layout, nullptr, &source.SetLayout));
    const VkDescriptorPoolSize sizes[2]{{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1}, {VK_DESCRIPTOR_TYPE_SAMPLER, 1}};
    VkDescriptorPoolCreateInfo pool{};
    pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool.maxSets = 1;
    pool.poolSizeCount = 2;
    pool.pPoolSizes = sizes;
    NATIVE_VK(api.vkCreateDescriptorPool(source.Device, &pool, nullptr, &source.Descriptors));
    VkDescriptorSetAllocateInfo set{};
    set.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    set.descriptorPool = source.Descriptors;
    set.descriptorSetCount = 1;
    set.pSetLayouts = &source.SetLayout;
    NATIVE_VK(api.vkAllocateDescriptorSets(source.Device, &set, &source.Set));
    const VkDescriptorImageInfo descriptors[2]{
        {VK_NULL_HANDLE, source.SourceView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {source.Sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED}};
    VkWriteDescriptorSet writes[2]{};
    for (uint32 i = 0; i < 2; ++i)
    {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = source.Set;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = bindings[i].descriptorType;
        writes[i].pImageInfo = &descriptors[i];
    }
    api.vkUpdateDescriptorSets(source.Device, 2, writes, 0, nullptr);
    VkAttachmentDescription attachment{};
    attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    const VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color;
    const VkSubpassDependency dependency{0,
                                         VK_SUBPASS_EXTERNAL,
                                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                                         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                         VK_ACCESS_TRANSFER_READ_BIT,
                                         0};
    VkRenderPassCreateInfo pass{};
    pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    pass.attachmentCount = 1;
    pass.pAttachments = &attachment;
    pass.subpassCount = 1;
    pass.pSubpasses = &subpass;
    pass.dependencyCount = 1;
    pass.pDependencies = &dependency;
    NATIVE_VK(api.vkCreateRenderPass(source.Device, &pass, nullptr, &source.Pass));
    VkFramebufferCreateInfo frame{};
    frame.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    frame.renderPass = source.Pass;
    frame.attachmentCount = 1;
    frame.pAttachments = &source.TargetView;
    frame.width = frame.height = 4;
    frame.layers = 1;
    NATIVE_VK(api.vkCreateFramebuffer(source.Device, &frame, nullptr, &source.Frame));
    const auto vertex = ludus::shaders::native_sample::Vertex().Artifact;
    const auto fragment = ludus::shaders::native_sample::Fragment().Artifact;
    const ludus::graphics::rhi::ShaderDescription shaders[2]{vertex, fragment};
    VkPipelineShaderStageCreateInfo stages[2]{};
    for (usize i = 0; i < 2; ++i)
    {
        VkShaderModuleCreateInfo module{};
        module.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        module.codeSize = shaders[i].Spirv.size_bytes();
        module.pCode = shaders[i].Spirv.data();
        NATIVE_VK(api.vkCreateShaderModule(source.Device, &module, nullptr, &source.Shaders[i]));
        stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[i].stage = i == 0 ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[i].module = source.Shaders[i];
        stages[i].pName = shaders[i].SpirvEntry.data();
    }
    VkPipelineLayoutCreateInfo pipelineLayout{};
    pipelineLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayout.setLayoutCount = 1;
    pipelineLayout.pSetLayouts = &source.SetLayout;
    NATIVE_VK(api.vkCreatePipelineLayout(source.Device, &pipelineLayout, nullptr, &source.PipelineLayout));
    VkPipelineVertexInputStateCreateInfo inputs{};
    inputs.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    const VkViewport viewport{0, 0, 4, 4, 0, 1};
    const VkRect2D scissor{{0, 0}, {4, 4}};
    VkPipelineViewportStateCreateInfo viewports{};
    viewports.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewports.viewportCount = viewports.scissorCount = 1;
    viewports.pViewports = &viewport;
    viewports.pScissors = &scissor;
    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo samples{};
    samples.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blend{};
    blend.colorWriteMask = 15;
    VkPipelineColorBlendStateCreateInfo blends{};
    blends.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blends.attachmentCount = 1;
    blends.pAttachments = &blend;
    VkGraphicsPipelineCreateInfo graphics{};
    graphics.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    graphics.stageCount = 2;
    graphics.pStages = stages;
    graphics.pVertexInputState = &inputs;
    graphics.pInputAssemblyState = &assembly;
    graphics.pViewportState = &viewports;
    graphics.pRasterizationState = &raster;
    graphics.pMultisampleState = &samples;
    graphics.pColorBlendState = &blends;
    graphics.layout = source.PipelineLayout;
    graphics.renderPass = source.Pass;
    NATIVE_VK(api.vkCreateGraphicsPipelines(source.Device, VK_NULL_HANDLE, 1, &graphics, nullptr, &source.Pipeline));
    VkCommandPoolCreateInfo commands{};
    commands.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commands.queueFamilyIndex = source.Family;
    NATIVE_VK(api.vkCreateCommandPool(source.Device, &commands, nullptr, &source.Commands));
    VkCommandBufferAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate.commandPool = source.Commands;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    NATIVE_VK(api.vkAllocateCommandBuffers(source.Device, &allocate, &source.Command));
    VkFenceCreateInfo fence{};
    fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    NATIVE_VK(api.vkCreateFence(source.Device, &fence, nullptr, &source.Fence));
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    NATIVE_VK(api.vkBeginCommandBuffer(source.Command, &begin));
    VkRenderPassBeginInfo render{};
    render.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render.renderPass = source.Pass;
    render.framebuffer = source.Frame;
    render.renderArea = {{0, 0}, {4, 4}};
    api.vkCmdBeginRenderPass(source.Command, &render, VK_SUBPASS_CONTENTS_INLINE);
    api.vkCmdBindPipeline(source.Command, VK_PIPELINE_BIND_POINT_GRAPHICS, source.Pipeline);
    api.vkCmdBindDescriptorSets(source.Command,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                source.PipelineLayout,
                                0,
                                1,
                                &source.Set,
                                0,
                                nullptr);
    api.vkCmdDraw(source.Command, 3, 1, 0, 0);
    api.vkCmdEndRenderPass(source.Command);
    const VkBufferImageCopy copy{0, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {4, 4, 1}};
    api.vkCmdCopyImageToBuffer(source.Command,
                               source.Target,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               source.Output,
                               1,
                               &copy);
    VkBufferMemoryBarrier host{};
    host.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    host.buffer = source.Output;
    host.size = VK_WHOLE_SIZE;
    api.vkCmdPipelineBarrier(source.Command,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_HOST_BIT,
                             0,
                             0,
                             nullptr,
                             1,
                             &host,
                             0,
                             nullptr);
    NATIVE_VK(api.vkEndCommandBuffer(source.Command));
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &source.Command;
    NATIVE_VK(api.vkQueueSubmit(source.Queue, 1, &submit, source.Fence));
    return true;
}
bool ReadSample(Foreign& fixture, ludus::foundation::uint8 (&pixels)[64]) noexcept
{
    auto& source = *static_cast<Source*>(fixture.Context);
    NATIVE_VK(source.Api.vkWaitForFences(source.Device, 1, &source.Fence, VK_TRUE, 5000000000ULL));
    void* mapped = nullptr;
    NATIVE_VK(source.Api.vkMapMemory(source.Device, source.OutputMemory, 0, 64, 0, &mapped));
    std::memcpy(pixels, mapped, sizeof(pixels));
    source.Api.vkUnmapMemory(source.Device, source.OutputMemory);
    // GPU completion precedes destruction of the foreign image view and descriptor.
    RetireSample(source);
    return true;
}
ludus::foundation::usize Retains(const Foreign& fixture) noexcept
{
    return static_cast<const Source*>(fixture.Context)->Retained;
}
ludus::foundation::usize Releases(const Foreign& fixture) noexcept
{
    return static_cast<const Source*>(fixture.Context)->Released;
}
} // namespace native_fixture
