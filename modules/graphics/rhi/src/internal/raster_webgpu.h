#pragma once
// Included only by the WebGPU backend. Thanks to W3C, WebGPU, sections
// "Error Scopes" and "GPUQueue" (validation and ordered completion):
// https://www.w3.org/TR/webgpu/#error-scopes and https://www.w3.org/TR/webgpu/#gpuqueue
namespace ludus::graphics::rhi::LUDUS_RHI_WEBGPU_NAMESPACE
{
namespace
{
struct RasterWebShader final
{
    WGPUShaderModule Object = nullptr;
    char Entry[64]{};
};
struct RasterWebPipeline final
{
    WGPURenderPipeline Object = nullptr;
    WGPUPipelineLayout Layout = nullptr;
    internal::RasterPipelineInfo Info;
    uint32 Request = 0;
};
struct RasterWebCompletion final
{
    uint64 Ordinal = 0;
    uint32 Session = 0;
    bool Busy = false;
    bool Submitted = false;
};
WGPUBuffer gRasterBuffers[internal::RASTER_CAPACITY]{};
WGPUTexture gRasterTextures[internal::RASTER_CAPACITY]{};
WGPUTexture gRasterDepths[internal::RASTER_CAPACITY]{};
WGPUTextureView gRasterAttachmentViews[internal::RASTER_CAPACITY]{};
WGPUTextureView gRasterDepthViews[internal::RASTER_CAPACITY]{};
WGPUTextureView gRasterViews[internal::RASTER_CAPACITY]{};
WGPUSampler gRasterSamplers[internal::RASTER_CAPACITY]{};
RasterWebShader gRasterShaders[internal::RASTER_CAPACITY];
WGPUBindGroupLayout gRasterLayouts[internal::RASTER_CAPACITY]{};
WGPUBindGroup gRasterSets[internal::RASTER_CAPACITY]{};
RasterWebPipeline gRasterPipelines[internal::RASTER_CAPACITY];
RasterWebCompletion gRasterCompletions[internal::RASTER_CAPACITY];
RasterWebCompletion* gRasterReserved = nullptr;
uint64 gRasterCompleted = 0;
void RasterValidated(WGPUPopErrorScopeStatus status,
                     WGPUErrorType error,
                     WGPUStringView message,
                     void* request,
                     void*) noexcept
{
    const auto result = status != WGPUPopErrorScopeStatus_Success ? RasterStatus::Failed
                        : error == WGPUErrorType_NoError          ? RasterStatus::Ready
                        : error == WGPUErrorType_OutOfMemory      ? RasterStatus::OutOfMemory
                                                                  : RasterStatus::Failed;
    if (result != RasterStatus::Ready)
    {
        Diagnose(message);
    }
    internal::RasterComplete(Token(request), result);
}
void RasterPipelineCreated(WGPUCreatePipelineAsyncStatus status,
                           WGPURenderPipeline object,
                           WGPUStringView message,
                           void* userdata,
                           void*) noexcept
{
    const auto request = Token(userdata);
    bool adopted = false;
    for (auto& pipeline : gRasterPipelines)
    {
        if (pipeline.Request == request)
        {
            pipeline.Object = object;
            adopted = true;
            break;
        }
    }
    if (!adopted && object != nullptr)
    {
        wgpuRenderPipelineRelease(object);
    }
    if (status != WGPUCreatePipelineAsyncStatus_Success)
    {
        Diagnose(message);
    }
    internal::RasterComplete(request,
                             status == WGPUCreatePipelineAsyncStatus_Success && object != nullptr
                                 ? RasterStatus::Ready
                                 : RasterStatus::Failed);
}
void RasterScopes(uint32 request) noexcept
{
    internal::RasterExpect(request, internal::RasterCallbacks::Two);
    wgpuDevicePushErrorScope(gDevice, WGPUErrorFilter_OutOfMemory);
    wgpuDevicePushErrorScope(gDevice, WGPUErrorFilter_Validation);
}
RasterStatus RasterPop(uint32 request) noexcept
{
    WGPUPopErrorScopeCallbackInfo callback = WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT;
    callback.mode = WGPUCallbackMode_AllowSpontaneous;
    callback.callback = RasterValidated;
    callback.userdata1 = Userdata(request);
    (void)wgpuDevicePopErrorScope(gDevice, callback);
    (void)wgpuDevicePopErrorScope(gDevice, callback);
    return RasterStatus::Pending;
}
void RasterDone(WGPUQueueWorkDoneStatus status, WGPUStringView, void* userdata, void*) noexcept
{
    auto& completion = *static_cast<RasterWebCompletion*>(userdata);
    const auto session = completion.Session;
    const auto ordinal = completion.Ordinal;
    completion = {};
    if (!internal::Current(session))
    {
        return;
    }
    if (status == WGPUQueueWorkDoneStatus_Success)
    {
        if (ordinal > gRasterCompleted)
        {
            gRasterCompleted = ordinal;
        }
    }
    else
    {
        internal::Fail(session, StartupError::DeviceLost);
    }
}
} // namespace
RasterCapabilities RasterLimits() noexcept
{
    WGPULimits limits = WGPU_LIMITS_INIT;
    if (gDevice == nullptr || wgpuDeviceGetLimits(gDevice, &limits) != WGPUStatus_Success ||
        limits.maxVertexBuffers < 2 || limits.maxVertexAttributes < 8 || limits.maxVertexBufferArrayStride < 256 ||
        limits.maxBindingsPerBindGroup < 8 || limits.maxUniformBuffersPerShaderStage < 8 ||
        limits.maxSampledTexturesPerShaderStage < 8 || limits.maxSamplersPerShaderStage < 8)
    {
        return {};
    }
    constexpr usize max = usize{64} * 1024 * 1024;
    return {static_cast<usize>(limits.maxBufferSize < max ? limits.maxBufferSize : max),
            limits.maxTextureDimension2D < 4096 ? limits.maxTextureDimension2D : 4096,
            limits.minUniformBufferOffsetAlignment > 256 ? limits.minUniformBufferOffsetAlignment : 256,
            static_cast<usize>(limits.maxUniformBufferBindingSize < 16384 ? limits.maxUniformBufferBindingSize : 16384),
            static_cast<uint32>(internal::RASTER_CAPACITY),
            static_cast<uint32>(internal::RASTER_DRAWS)};
}
RasterStatus
RasterCreateBuffer(usize slot, const BufferDescription& info, std::span<const uint8> bytes, uint32 request) noexcept
{
    RasterScopes(request);
    WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
    descriptor.size = (bytes.size() + 3) & ~usize{3};
    descriptor.mappedAtCreation = true;
    descriptor.usage = info.Role == BufferRole::Uniform  ? WGPUBufferUsage_Uniform
                       : info.Role == BufferRole::Vertex ? WGPUBufferUsage_Vertex
                                                         : WGPUBufferUsage_Index;
    descriptor.usage |= WGPUBufferUsage_CopySrc;
    auto object = wgpuDeviceCreateBuffer(gDevice, &descriptor);
    gRasterBuffers[slot] = object;
    if (object != nullptr && wgpuBufferGetMapState(object) == WGPUBufferMapState_Mapped)
    {
        void* mapped = wgpuBufferGetMappedRange(object, 0, descriptor.size);
        if (mapped != nullptr)
        {
            std::memset(mapped, 0, descriptor.size);
            std::memcpy(mapped, bytes.data(), bytes.size());
        }
        else
        {
            internal::RasterFail(request, RasterStatus::OutOfMemory);
        }
        wgpuBufferUnmap(object);
    }
    else
    {
        internal::RasterFail(request, RasterStatus::OutOfMemory);
    }
    return RasterPop(request);
}
RasterStatus
RasterCreateTexture(usize slot, const TextureDescription& info, const TextureUpload& upload, uint32 request) noexcept
{
    RasterScopes(request);
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.size = {info.Width, info.Height, 1};
    descriptor.dimension = WGPUTextureDimension_2D;
    descriptor.format =
        info.Format == RasterFormat::Rgba8Unorm ? WGPUTextureFormat_RGBA8Unorm : WGPUTextureFormat_RGBA8UnormSrgb;
    descriptor.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst |
                       (info.Attachment ? WGPUTextureUsage_RenderAttachment : WGPUTextureUsage_None);
    auto texture = wgpuDeviceCreateTexture(gDevice, &descriptor);
    gRasterTextures[slot] = texture;
    if (texture != nullptr && !info.Attachment)
    {
        WGPUTexelCopyTextureInfo destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        destination.texture = texture;
        WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
        layout.bytesPerRow = static_cast<uint32>(upload.RowPitch);
        layout.rowsPerImage = info.Height;
        wgpuQueueWriteTexture(gQueue,
                              &destination,
                              upload.Bytes.data(),
                              upload.Bytes.size(),
                              &layout,
                              &descriptor.size);
    }
    else if (texture != nullptr)
    {
        gRasterAttachmentViews[slot] = wgpuTextureCreateView(texture, nullptr);
        descriptor.format = WGPUTextureFormat_Depth24Plus;
        descriptor.usage = WGPUTextureUsage_RenderAttachment;
        gRasterDepths[slot] = wgpuDeviceCreateTexture(gDevice, &descriptor);
        if (gRasterDepths[slot] != nullptr)
        {
            gRasterDepthViews[slot] = wgpuTextureCreateView(gRasterDepths[slot], nullptr);
        }
        if (gRasterAttachmentViews[slot] == nullptr || gRasterDepthViews[slot] == nullptr)
        {
            internal::RasterFail(request, RasterStatus::OutOfMemory);
        }
    }
    else
    {
        internal::RasterFail(request, RasterStatus::OutOfMemory);
    }
    return RasterPop(request);
}
RasterStatus RasterCreateView(usize slot, const internal::RasterViewSource& source, uint32 request) noexcept
{
    RasterScopes(request);
    gRasterViews[slot] = wgpuTextureCreateView(gRasterTextures[source.Texture], nullptr);
    return RasterPop(request);
}
RasterStatus RasterCreateSampler(usize slot, const SamplerDescription& info, uint32 request) noexcept
{
    RasterScopes(request);
    WGPUSamplerDescriptor descriptor = WGPU_SAMPLER_DESCRIPTOR_INIT;
    descriptor.minFilter = descriptor.magFilter =
        info.Filter == RasterFilter::Nearest ? WGPUFilterMode_Nearest : WGPUFilterMode_Linear;
    descriptor.addressModeU = info.U == RasterAddress::Clamp ? WGPUAddressMode_ClampToEdge : WGPUAddressMode_Repeat;
    descriptor.addressModeV = info.V == RasterAddress::Clamp ? WGPUAddressMode_ClampToEdge : WGPUAddressMode_Repeat;
    descriptor.lodMaxClamp = 0;
    gRasterSamplers[slot] = wgpuDeviceCreateSampler(gDevice, &descriptor);
    return RasterPop(request);
}
RasterStatus RasterCreateShader(usize slot,
                                const ShaderDescription& info,
                                const internal::RasterShaderInfo&,
                                uint32 request) noexcept
{
    RasterScopes(request);
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = {info.Wgsl.data(), info.Wgsl.size()};
    WGPUShaderModuleDescriptor descriptor = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    descriptor.nextInChain = &source.chain;
    auto& shader = gRasterShaders[slot];
    shader.Object = wgpuDeviceCreateShaderModule(gDevice, &descriptor);
    std::memcpy(shader.Entry, info.WgslEntry.data(), info.WgslEntry.size());
    shader.Entry[info.WgslEntry.size()] = '\0';
    return RasterPop(request);
}
RasterStatus RasterCreateLayout(usize slot, const internal::RasterLayout& info, uint32 request) noexcept
{
    RasterScopes(request);
    WGPUBindGroupLayoutEntry entries[internal::RASTER_BINDINGS]{};
    for (usize i = 0; i < info.Count; ++i)
    {
        const auto& source = info.Entries[i];
        auto& entry = entries[i];
        entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        entry.binding = source.Binding;
        entry.visibility = static_cast<WGPUShaderStage>(source.Visibility);
        if (source.Kind == RasterBindingKind::UniformBuffer)
        {
            entry.buffer.type = WGPUBufferBindingType_Uniform;
            entry.buffer.minBindingSize = source.MinSize;
        }
        else if (source.Kind == RasterBindingKind::Texture2D)
        {
            entry.texture.sampleType = WGPUTextureSampleType_Float;
            entry.texture.viewDimension = WGPUTextureViewDimension_2D;
        }
        else
        {
            entry.sampler.type = WGPUSamplerBindingType_Filtering;
        }
    }
    WGPUBindGroupLayoutDescriptor descriptor = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    descriptor.entryCount = info.Count;
    descriptor.entries = entries;
    gRasterLayouts[slot] = wgpuDeviceCreateBindGroupLayout(gDevice, &descriptor);
    return RasterPop(request);
}
RasterStatus RasterCreateSet(usize slot,
                             const internal::RasterLayout& layout,
                             const internal::RasterSet& info,
                             uint32 request) noexcept
{
    RasterScopes(request);
    WGPUBindGroupEntry entries[internal::RASTER_BINDINGS]{};
    for (usize i = 0; i < layout.Count; ++i)
    {
        const auto& source = layout.Entries[i];
        auto& entry = entries[i];
        entry = WGPU_BIND_GROUP_ENTRY_INIT;
        entry.binding = source.Binding;
        if (source.Kind == RasterBindingKind::UniformBuffer)
        {
            entry.buffer = gRasterBuffers[info.Slots[i]];
            entry.offset = info.Offsets[i];
            entry.size = info.Sizes[i];
        }
        else if (source.Kind == RasterBindingKind::Texture2D)
        {
            entry.textureView = gRasterViews[info.Slots[i]];
        }
        else
        {
            entry.sampler = gRasterSamplers[info.Slots[i]];
        }
    }
    WGPUBindGroupDescriptor descriptor = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    descriptor.layout = gRasterLayouts[info.Layout];
    descriptor.entryCount = layout.Count;
    descriptor.entries = entries;
    gRasterSets[slot] = wgpuDeviceCreateBindGroup(gDevice, &descriptor);
    return RasterPop(request);
}
RasterStatus RasterCreatePipeline(usize slot, const internal::RasterPipelineInfo& info, uint32 request) noexcept
{
    RasterScopes(request);
    auto& pipeline = gRasterPipelines[slot];
    pipeline.Info = info;
    pipeline.Request = request;
    internal::RasterExpect(request, internal::RasterCallbacks::Three);
    WGPUPipelineLayoutDescriptor layout = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    layout.bindGroupLayoutCount = 1;
    layout.bindGroupLayouts = &gRasterLayouts[info.Layout];
    pipeline.Layout = wgpuDeviceCreatePipelineLayout(gDevice, &layout);
    WGPUVertexAttribute attributes[2][8]{};
    WGPUVertexBufferLayout buffers[2]{};
    for (usize stream = 0; stream < info.StreamCount; ++stream)
    {
        auto& buffer = buffers[stream];
        buffer = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
        buffer.arrayStride = info.Streams[stream].Stride;
        buffer.stepMode = info.Streams[stream].PerInstance ? WGPUVertexStepMode_Instance : WGPUVertexStepMode_Vertex;
        buffer.attributes = attributes[stream];
        for (usize i = 0; i < info.AttributeCount; ++i)
        {
            const auto& source = info.Attributes[i];
            if (source.Stream != stream)
            {
                continue;
            }
            auto& attribute = attributes[stream][buffer.attributeCount++];
            attribute = WGPU_VERTEX_ATTRIBUTE_INIT;
            attribute.shaderLocation = source.Location;
            attribute.offset = source.Offset;
            attribute.format = source.Format == RasterVertexFormat::Float2   ? WGPUVertexFormat_Float32x2
                               : source.Format == RasterVertexFormat::Float3 ? WGPUVertexFormat_Float32x3
                                                                             : WGPUVertexFormat_Float32x4;
        }
    }
    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = info.Target == RasterTarget::Surface      ? gConfiguration.format
                    : info.Target == RasterTarget::Rgba8Unorm ? WGPUTextureFormat_RGBA8Unorm
                                                              : WGPUTextureFormat_RGBA8UnormSrgb;
    WGPUBlendState blend = WGPU_BLEND_STATE_INIT;
    blend.color.srcFactor = blend.alpha.srcFactor = WGPUBlendFactor_One;
    blend.color.dstFactor = blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    if (info.Blend)
    {
        target.blend = &blend;
    }
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = gRasterShaders[info.Fragment].Object;
    fragment.entryPoint = {gRasterShaders[info.Fragment].Entry, WGPU_STRLEN};
    fragment.targetCount = 1;
    fragment.targets = &target;
    WGPUDepthStencilState depth = WGPU_DEPTH_STENCIL_STATE_INIT;
    depth.format = WGPUTextureFormat_Depth24Plus;
    depth.depthWriteEnabled = info.Depth && info.DepthWrite ? WGPUOptionalBool_True : WGPUOptionalBool_False;
    depth.depthCompare = info.Depth ? WGPUCompareFunction_Less : WGPUCompareFunction_Always;
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.layout = pipeline.Layout;
    descriptor.vertex.module = gRasterShaders[info.Vertex].Object;
    descriptor.vertex.entryPoint = {gRasterShaders[info.Vertex].Entry, WGPU_STRLEN};
    descriptor.vertex.bufferCount = info.StreamCount;
    descriptor.vertex.buffers = buffers;
    descriptor.fragment = &fragment;
    descriptor.depthStencil = &depth;
    descriptor.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    WGPUCreateRenderPipelineAsyncCallbackInfo callback = WGPU_CREATE_RENDER_PIPELINE_ASYNC_CALLBACK_INFO_INIT;
    callback.mode = WGPUCallbackMode_AllowSpontaneous;
    callback.callback = RasterPipelineCreated;
    callback.userdata1 = Userdata(request);
    wgpuDeviceCreateRenderPipelineAsync(gDevice, &descriptor, callback);
    return RasterPop(request);
}
void RasterDestroy(internal::RasterKind kind, usize slot) noexcept
{
    switch (kind)
    {
        case internal::RasterKind::Buffer:
            if (gRasterBuffers[slot])
            {
                wgpuBufferRelease(gRasterBuffers[slot]);
            }
            gRasterBuffers[slot] = nullptr;
            break;
        case internal::RasterKind::Texture:
            if (gRasterTextures[slot])
            {
                wgpuTextureRelease(gRasterTextures[slot]);
            }
            if (gRasterAttachmentViews[slot])
            {
                wgpuTextureViewRelease(gRasterAttachmentViews[slot]);
            }
            if (gRasterDepthViews[slot])
            {
                wgpuTextureViewRelease(gRasterDepthViews[slot]);
            }
            if (gRasterDepths[slot])
            {
                wgpuTextureRelease(gRasterDepths[slot]);
            }
            gRasterAttachmentViews[slot] = gRasterDepthViews[slot] = nullptr;
            gRasterDepths[slot] = nullptr;
            gRasterTextures[slot] = nullptr;
            break;
        case internal::RasterKind::View:
            if (gRasterViews[slot])
            {
                wgpuTextureViewRelease(gRasterViews[slot]);
            }
            gRasterViews[slot] = nullptr;
            break;
        case internal::RasterKind::Sampler:
            if (gRasterSamplers[slot])
            {
                wgpuSamplerRelease(gRasterSamplers[slot]);
            }
            gRasterSamplers[slot] = nullptr;
            break;
        case internal::RasterKind::Shader:
            if (gRasterShaders[slot].Object)
            {
                wgpuShaderModuleRelease(gRasterShaders[slot].Object);
            }
            gRasterShaders[slot] = {};
            break;
        case internal::RasterKind::Layout:
            if (gRasterLayouts[slot])
            {
                wgpuBindGroupLayoutRelease(gRasterLayouts[slot]);
            }
            gRasterLayouts[slot] = nullptr;
            break;
        case internal::RasterKind::Set:
            if (gRasterSets[slot])
            {
                wgpuBindGroupRelease(gRasterSets[slot]);
            }
            gRasterSets[slot] = nullptr;
            break;
        case internal::RasterKind::Pipeline:
            if (gRasterPipelines[slot].Object)
            {
                wgpuRenderPipelineRelease(gRasterPipelines[slot].Object);
            }
            if (gRasterPipelines[slot].Layout)
            {
                wgpuPipelineLayoutRelease(gRasterPipelines[slot].Layout);
            }
            gRasterPipelines[slot] = {};
            break;
        case internal::RasterKind::Count:
            break;
    }
}
RasterStatus RasterDraw(const internal::RasterPacket& packet) noexcept
{
    const auto& pipeline = gRasterPipelines[packet.Pipeline];
    wgpuRenderPassEncoderSetPipeline(gPass, pipeline.Object);
    wgpuRenderPassEncoderSetBindGroup(gPass, 0, gRasterSets[packet.Set], 0, nullptr);
    for (usize i = 0; i < pipeline.Info.StreamCount; ++i)
    {
        wgpuRenderPassEncoderSetVertexBuffer(gPass,
                                             static_cast<uint32>(i),
                                             gRasterBuffers[packet.Vertices[i]],
                                             packet.Offsets[i],
                                             WGPU_WHOLE_SIZE);
    }
    wgpuRenderPassEncoderSetIndexBuffer(gPass,
                                        gRasterBuffers[packet.Indices],
                                        packet.Index32 ? WGPUIndexFormat_Uint32 : WGPUIndexFormat_Uint16,
                                        packet.IndexOffset,
                                        WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDrawIndexed(gPass, packet.IndexCount, packet.InstanceCount, 0, 0, 0);
    return RasterStatus::Ready;
}
void RasterEndPass() noexcept
{
    if (gPass != nullptr)
    {
        wgpuRenderPassEncoderEnd(gPass);
        wgpuRenderPassEncoderRelease(gPass);
        gPass = nullptr;
    }
}
RasterStatus RasterPreparePass(const internal::RasterPassInfo&) noexcept
{
    return RasterStatus::Ready;
}
void RasterTextureBarrier(usize, RasterTextureUse) noexcept {}
RasterStatus RasterBeginPass(const internal::RasterPassInfo& info) noexcept
{
    const auto& state = info.Description;
    const bool surface = info.Texture == internal::RASTER_CAPACITY;
    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color.view = surface ? gView : gRasterAttachmentViews[info.Texture];
    // Discard has no WebGPU load operation; security initialization clears, but
    // the semantic ledger deliberately keeps those contents undefined.
    color.loadOp = state.ColorLoad == RasterLoad::Load ? WGPULoadOp_Load : WGPULoadOp_Clear;
    color.storeOp = state.ColorStore == RasterStore::Store ? WGPUStoreOp_Store : WGPUStoreOp_Discard;
    color.clearValue = {static_cast<float64>(state.Clear[0]),
                        static_cast<float64>(state.Clear[1]),
                        static_cast<float64>(state.Clear[2]),
                        static_cast<float64>(state.Clear[3])};
    WGPURenderPassDepthStencilAttachment depth = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    depth.view = surface ? gDepthView : gRasterDepthViews[info.Texture];
    depth.depthReadOnly = state.DepthReadOnly;
    // The browser bridge forwards this value even for read-only depth. Keep
    // the ignored value finite instead of forwarding the C API's NaN default.
    depth.depthClearValue = 1;
    if (!state.DepthReadOnly)
    {
        depth.depthLoadOp = state.DepthLoad == RasterLoad::Load ? WGPULoadOp_Load : WGPULoadOp_Clear;
        depth.depthStoreOp = state.DepthStore == RasterStore::Store ? WGPUStoreOp_Store : WGPUStoreOp_Discard;
    }
    WGPURenderPassDescriptor pass = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    pass.colorAttachmentCount = 1;
    pass.colorAttachments = &color;
    pass.depthStencilAttachment = &depth;
    gPass = wgpuCommandEncoderBeginRenderPass(gEncoder, &pass);
    return gPass != nullptr ? RasterStatus::Ready : RasterStatus::Failed;
}
RasterStatus RasterReserveSubmission() noexcept
{
    if (gRasterReserved != nullptr)
    {
        return RasterStatus::Ready;
    }
    for (auto& completion : gRasterCompletions)
    {
        if (!completion.Busy)
        {
            completion = {0, gSession, true, false};
            gRasterReserved = &completion;
            return RasterStatus::Ready;
        }
    }
    return RasterStatus::CapacityExceeded;
}
void RasterSubmit(uint64 ordinal) noexcept
{
    if (gRasterReserved == nullptr)
    {
        return;
    }
    auto* completion = gRasterReserved;
    gRasterReserved = nullptr;
    completion->Ordinal = ordinal;
    completion->Submitted = true;
    WGPUQueueWorkDoneCallbackInfo callback = WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT;
    callback.mode = WGPUCallbackMode_AllowSpontaneous;
    callback.callback = RasterDone;
    callback.userdata1 = completion;
    (void)wgpuQueueOnSubmittedWorkDone(gQueue, callback);
}
uint64 RasterCompleted() noexcept
{
    return gRasterCompleted;
}
void RasterReset() noexcept
{
    if (gRasterReserved != nullptr)
    {
        *gRasterReserved = {};
        gRasterReserved = nullptr;
    }
    // Submitted callback cells stay occupied until their old-session callback drains.
    // Reusing their addresses during restart would let late callbacks retire new work.
    gRasterCompleted = 0;
}
void RasterShutdown() noexcept {}
} // namespace ludus::graphics::rhi::LUDUS_RHI_WEBGPU_NAMESPACE
