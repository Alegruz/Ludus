#pragma once
// Private implementation included only by rhi_metal.mm: native device/frame
// ownership remains in the existing backend translation unit.
// Thanks to Apple, "Resource Fundamentals", Metal Programming Guide (retained
// command buffers), and "MTLVertexDescriptor" (separate stream/attribute maps):
// https://developer.apple.com/library/archive/documentation/Miscellaneous/Conceptual/MetalProgrammingGuide/ResourceFundamentals/ResourceFundamentals.html
// https://developer.apple.com/documentation/metal/mtlvertexdescriptor
namespace ludus::graphics::rhi::backend
{
namespace
{
struct RasterMetalShader final
{
    id<MTLFunction> Function = nil;
    internal::RasterShaderInfo Info;
};
struct RasterMetalPipeline final
{
    id<MTLRenderPipelineState> Object = nil;
    id<MTLDepthStencilState> Depth = nil;
    internal::RasterPipelineInfo Info;
};
id<MTLBuffer> gRasterBuffers[internal::RASTER_CAPACITY]{};
id<MTLTexture> gRasterTextures[internal::RASTER_CAPACITY]{};
id<MTLTexture> gRasterDepths[internal::RASTER_CAPACITY]{};
id<MTLTexture> gRasterViews[internal::RASTER_CAPACITY]{};
id<MTLSamplerState> gRasterSamplers[internal::RASTER_CAPACITY]{};
RasterMetalShader gRasterShaders[internal::RASTER_CAPACITY];
internal::RasterLayout gRasterLayouts[internal::RASTER_CAPACITY];
internal::RasterSet gRasterSets[internal::RASTER_CAPACITY];
RasterMetalPipeline gRasterPipelines[internal::RASTER_CAPACITY];
id<MTLComputePipelineState> gComputePipelines[internal::RASTER_CAPACITY]{};
internal::ComputePipelineInfo gComputeInfos[internal::RASTER_CAPACITY];
uint64 gRasterOrdinals[FRAMES]{};
uint64 gRasterCompleted = 0;
uint32 gRasterWidth = 0, gRasterHeight = 0;
RasterPassDescription gRasterRegion{};
bool RasterMetalBindings(NSArray<id<MTLBinding>>* bindings,
                         const internal::RasterShaderInfo& shader,
                         const internal::RasterPipelineInfo& pipeline) noexcept
{
    for (id<MTLBinding> binding in bindings)
    {
        if (!binding.used)
        {
            continue;
        }
        // Metal reports stage-in vertex streams as buffer bindings as well as
        // user shader resources. Their reserved indices follow the verified
        // vertex descriptor, independently of the group-zero resource layout.
        if (shader.Stage == ShaderStage::Vertex && binding.type == MTLBindingTypeBuffer &&
            binding.index >= internal::RASTER_BINDINGS &&
            binding.index < internal::RASTER_BINDINGS + pipeline.StreamCount)
        {
            continue;
        }
        bool found = false;
        for (usize i = 0; i < shader.Layout.Count; ++i)
        {
            const auto& entry = shader.Layout.Entries[i];
            if (entry.Binding != binding.index)
            {
                continue;
            }
            if ((entry.Kind == RasterBindingKind::UniformBuffer || entry.Kind == RasterBindingKind::StorageRead ||
                 entry.Kind == RasterBindingKind::StorageReadWrite) &&
                binding.type == MTLBindingTypeBuffer &&
                (binding.access == MTLBindingAccessReadOnly ||
                 (entry.Kind == RasterBindingKind::StorageReadWrite && binding.access == MTLBindingAccessReadWrite)))
            {
                found = ((id<MTLBufferBinding>)binding).bufferDataSize <= entry.MinSize;
            }
            else if (entry.Kind == RasterBindingKind::Texture2D && binding.type == MTLBindingTypeTexture)
            {
                found = ((id<MTLTextureBinding>)binding).textureType == MTLTextureType2D &&
                        binding.access == MTLBindingAccessReadOnly;
            }
            else if (entry.Kind == RasterBindingKind::Sampler && binding.type == MTLBindingTypeSampler)
            {
                found = true;
            }
        }
        if (!found)
        {
            LUDUS_LOG_WARN(LOG_RHI,
                           "Raster Metal binding {} index {} type {} is absent from stage reflection",
                           binding.name.UTF8String,
                           static_cast<uint64>(binding.index),
                           static_cast<uint64>(binding.type));
            return false;
        }
    }
    return true;
}
} // namespace
RasterCapabilities RasterLimits() noexcept
{
    const usize max = usize{64} * 1024 * 1024;
    return
    {
        .MaxBufferSize = gDevice.maxBufferLength < max ? gDevice.maxBufferLength : max,
        .MaxTextureDimension2D = 4096,
        .UniformOffsetAlignment = 256,
        .MaxUniformRange = 16384,
        .ResourcesPerKind = static_cast<uint32>(internal::RASTER_CAPACITY),
        .DrawsPerFrame = static_cast<uint32>(internal::RASTER_DRAWS),
    };
}
// Thanks to Apple, "MTLComputeCommandEncoder" and Metal's tracked-resource
// synchronization: encoder boundaries order storage writes before raster reads.
// https://developer.apple.com/documentation/metal/mtlcomputecommandencoder
ComputeCapabilities ComputeLimits() noexcept
{
    if (gDevice == nil)
    {
        return {};
    }
    const auto threads = gDevice.maxThreadsPerThreadgroup;
    constexpr usize range = usize{16} * 1024 * 1024;
    return {gDevice.maxBufferLength < range ? gDevice.maxBufferLength : range,
            256,
            {static_cast<uint32>(threads.width < 256 ? threads.width : 256),
             static_cast<uint32>(threads.height < 256 ? threads.height : 256),
             static_cast<uint32>(threads.depth < 64 ? threads.depth : 64)},
            256,
            {65535, 65535, 65535}};
}
RasterStatus ComputeCreatePipeline(usize slot, const internal::ComputePipelineInfo& info, uint32) noexcept
{
    @autoreleasepool
    {
        NSError* error = nil;
        MTLComputePipelineReflection* reflection = nil;
        auto pipeline = [gDevice newComputePipelineStateWithFunction:gRasterShaders[info.Shader].Function
                                                             options:BINDING_INFO | MTLPipelineOptionBufferTypeInfo
                                                          reflection:&reflection
                                                               error:&error];
        const uint32 threads = info.WorkgroupSize[0] * info.WorkgroupSize[1] * info.WorkgroupSize[2];
        if (pipeline == nil || reflection == nil || threads > pipeline.maxTotalThreadsPerThreadgroup ||
            !RasterMetalBindings(reflection.bindings, gRasterShaders[info.Shader].Info, {}))
        {
            LogError(error);
            return RasterStatus::Failed;
        }
        gComputePipelines[slot] = pipeline;
        gComputeInfos[slot] = info;
        return RasterStatus::Ready;
    }
}
RasterStatus ComputeEncode(const internal::ComputePacket& packet) noexcept
{
    auto encoder = [gCommand computeCommandEncoder];
    if (encoder == nil)
    {
        return RasterStatus::OutOfMemory;
    }
    [encoder setComputePipelineState:gComputePipelines[packet.Pipeline]];
    const auto& set = gRasterSets[packet.Set];
    const auto& layout = gRasterLayouts[set.Layout];
    for (usize i = 0; i < layout.Count; ++i)
    {
        [encoder setBuffer:gRasterBuffers[set.Slots[i]] offset:set.Offsets[i] atIndex:layout.Entries[i].Binding];
    }
    const auto& info = gComputeInfos[packet.Pipeline];
    [encoder dispatchThreadgroups:MTLSizeMake(packet.Groups[0], packet.Groups[1], packet.Groups[2])
            threadsPerThreadgroup:MTLSizeMake(info.WorkgroupSize[0], info.WorkgroupSize[1], info.WorkgroupSize[2])];
    [encoder endEncoding];
    return RasterStatus::Ready;
}
void ComputeBufferBarrier(usize, GraphAccessMode) noexcept {}
RasterStatus RasterCreateBuffer(usize slot, const BufferDescription&, std::span<const uint8> bytes, uint32) noexcept
{
    @autoreleasepool
    {
        gRasterBuffers[slot] = [gDevice newBufferWithBytes:bytes.data()
                                                    length:bytes.size()
                                                   options:MTLResourceStorageModeShared];
    }
    return gRasterBuffers[slot] != nil ? RasterStatus::Ready : RasterStatus::OutOfMemory;
}
RasterStatus
RasterCreateTexture(usize slot, const TextureDescription& info, const TextureUpload& upload, uint32) noexcept
{
    @autoreleasepool
    {
        MTLTextureDescriptor* descriptor = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:info.Format == RasterFormat::Rgba8Unorm ? MTLPixelFormatRGBA8Unorm
                                                                                       : MTLPixelFormatRGBA8Unorm_sRGB
                                         width:info.Width
                                        height:info.Height
                                     mipmapped:NO];
        descriptor.usage =
            MTLTextureUsageShaderRead | (info.Attachment ? MTLTextureUsageRenderTarget : MTLTextureUsageUnknown);
        descriptor.storageMode = MTLStorageModeShared;
        auto texture = [gDevice newTextureWithDescriptor:descriptor];
        if (texture == nil)
        {
            return RasterStatus::OutOfMemory;
        }
        if (!info.Attachment)
        {
            [texture replaceRegion:MTLRegionMake2D(0, 0, info.Width, info.Height)
                       mipmapLevel:0
                         withBytes:upload.Bytes.data()
                       bytesPerRow:upload.RowPitch];
        }
        else
        {
            auto depth = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                                            width:info.Width
                                                                           height:info.Height
                                                                        mipmapped:NO];
            depth.usage = MTLTextureUsageRenderTarget;
            depth.storageMode = MTLStorageModePrivate;
            gRasterDepths[slot] = [gDevice newTextureWithDescriptor:depth];
            if (gRasterDepths[slot] == nil)
            {
                return RasterStatus::OutOfMemory;
            }
        }
        gRasterTextures[slot] = texture;
        return RasterStatus::Ready;
    }
}
RasterStatus RasterCreateView(usize slot, const internal::RasterViewSource& source, uint32) noexcept
{
    gRasterViews[slot] = gRasterTextures[source.Texture];
    return RasterStatus::Ready;
}
RasterStatus RasterCreateSampler(usize slot, const SamplerDescription& info, uint32) noexcept
{
    @autoreleasepool
    {
        MTLSamplerDescriptor* descriptor = [MTLSamplerDescriptor new];
        descriptor.minFilter = descriptor.magFilter =
            info.Filter == RasterFilter::Nearest ? MTLSamplerMinMagFilterNearest : MTLSamplerMinMagFilterLinear;
        descriptor.sAddressMode =
            info.U == RasterAddress::Clamp ? MTLSamplerAddressModeClampToEdge : MTLSamplerAddressModeRepeat;
        descriptor.tAddressMode =
            info.V == RasterAddress::Clamp ? MTLSamplerAddressModeClampToEdge : MTLSamplerAddressModeRepeat;
        gRasterSamplers[slot] = [gDevice newSamplerStateWithDescriptor:descriptor];
        return gRasterSamplers[slot] != nil ? RasterStatus::Ready : RasterStatus::OutOfMemory;
    }
}
RasterStatus RasterCreateShader(usize slot,
                                const ShaderDescription& artifact,
                                const internal::RasterShaderInfo& info,
                                uint32) noexcept
{
    @autoreleasepool
    {
        NSString* source = [[NSString alloc] initWithBytes:artifact.Msl.data()
                                                    length:artifact.Msl.size()
                                                  encoding:NSUTF8StringEncoding];
        NSString* name = [[NSString alloc] initWithBytes:artifact.MslEntry.data()
                                                  length:artifact.MslEntry.size()
                                                encoding:NSUTF8StringEncoding];
        NSError* error = nil;
        MTLCompileOptions* options = [MTLCompileOptions new];
        options.languageVersion = MTLLanguageVersion2_3;
        id<MTLLibrary> library = [gDevice newLibraryWithSource:source options:options error:&error];
        id<MTLFunction> function = [library newFunctionWithName:name];
        if (function == nil || function.functionType != (info.Stage == ShaderStage::Vertex     ? MTLFunctionTypeVertex
                                                         : info.Stage == ShaderStage::Fragment ? MTLFunctionTypeFragment
                                                                                               : MTLFunctionTypeKernel))
        {
            LogError(error);
            return RasterStatus::Failed;
        }
        gRasterShaders[slot] = {function, info};
        return RasterStatus::Ready;
    }
}
RasterStatus RasterCreateLayout(usize slot, const internal::RasterLayout& info, uint32) noexcept
{
    gRasterLayouts[slot] = info;
    return RasterStatus::Ready;
}
RasterStatus
RasterCreateSet(usize slot, const internal::RasterLayout&, const internal::RasterSet& info, uint32) noexcept
{
    gRasterSets[slot] = info;
    return RasterStatus::Ready;
}
RasterStatus RasterCreatePipeline(usize slot, const internal::RasterPipelineInfo& info, uint32) noexcept
{
    @autoreleasepool
    {
        MTLRenderPipelineDescriptor* descriptor = [MTLRenderPipelineDescriptor new];
        descriptor.vertexFunction = gRasterShaders[info.Vertex].Function;
        descriptor.fragmentFunction = gRasterShaders[info.Fragment].Function;
        descriptor.colorAttachments[0].pixelFormat = info.Target == RasterTarget::Surface ? MTLPixelFormatBGRA8Unorm
                                                     : info.Target == RasterTarget::Rgba8Unorm
                                                         ? MTLPixelFormatRGBA8Unorm
                                                         : MTLPixelFormatRGBA8Unorm_sRGB;
        descriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
        auto color = descriptor.colorAttachments[0];
        color.blendingEnabled = info.Blend;
        color.sourceRGBBlendFactor = color.sourceAlphaBlendFactor = MTLBlendFactorOne;
        color.destinationRGBBlendFactor = color.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        MTLVertexDescriptor* vertex = [MTLVertexDescriptor vertexDescriptor];
        for (usize i = 0; i < info.StreamCount; ++i)
        {
            const usize binding = internal::RASTER_BINDINGS + i;
            vertex.layouts[binding].stride = info.Streams[i].Stride;
            vertex.layouts[binding].stepFunction =
                info.Streams[i].PerInstance ? MTLVertexStepFunctionPerInstance : MTLVertexStepFunctionPerVertex;
            vertex.layouts[binding].stepRate = 1;
        }
        for (usize i = 0; i < info.AttributeCount; ++i)
        {
            const auto& attribute = info.Attributes[i];
            vertex.attributes[attribute.Location].format =
                attribute.Format == RasterVertexFormat::Float2   ? MTLVertexFormatFloat2
                : attribute.Format == RasterVertexFormat::Float3 ? MTLVertexFormatFloat3
                                                                 : MTLVertexFormatFloat4;
            vertex.attributes[attribute.Location].offset = attribute.Offset;
            vertex.attributes[attribute.Location].bufferIndex = internal::RASTER_BINDINGS + attribute.Stream;
        }
        descriptor.vertexDescriptor = vertex;
        NSError* error = nil;
        MTLRenderPipelineReflection* reflection = nil;
        auto pipeline = [gDevice newRenderPipelineStateWithDescriptor:descriptor
                                                              options:BINDING_INFO | MTLPipelineOptionBufferTypeInfo
                                                           reflection:&reflection
                                                                error:&error];
        if (pipeline == nil || reflection == nil ||
            !RasterMetalBindings(reflection.vertexBindings, gRasterShaders[info.Vertex].Info, info) ||
            !RasterMetalBindings(reflection.fragmentBindings, gRasterShaders[info.Fragment].Info, info))
        {
            LogError(error);
            return RasterStatus::Failed;
        }
        MTLDepthStencilDescriptor* depth = [MTLDepthStencilDescriptor new];
        const MTLCompareFunction comparisons[]{MTLCompareFunctionLess,
                                               MTLCompareFunctionLessEqual,
                                               MTLCompareFunctionGreater,
                                               MTLCompareFunctionGreaterEqual,
                                               MTLCompareFunctionAlways};
        depth.depthCompareFunction =
            info.Depth ? comparisons[static_cast<usize>(info.DepthCompare)] : MTLCompareFunctionAlways;
        depth.depthWriteEnabled = info.Depth && info.DepthWrite;
        auto state = [gDevice newDepthStencilStateWithDescriptor:depth];
        if (state == nil)
        {
            return RasterStatus::OutOfMemory;
        }
        gRasterPipelines[slot] = {pipeline, state, info};
        return RasterStatus::Ready;
    }
}
void RasterDestroy(internal::RasterKind kind, usize slot) noexcept
{
    switch (kind)
    {
        case internal::RasterKind::Buffer:
            gRasterBuffers[slot] = nil;
            break;
        case internal::RasterKind::Texture:
            gRasterTextures[slot] = nil;
            gRasterDepths[slot] = nil;
            break;
        case internal::RasterKind::View:
            gRasterViews[slot] = nil;
            break;
        case internal::RasterKind::Sampler:
            gRasterSamplers[slot] = nil;
            break;
        case internal::RasterKind::Shader:
            gRasterShaders[slot] = {};
            break;
        case internal::RasterKind::Layout:
            gRasterLayouts[slot] = {};
            break;
        case internal::RasterKind::Set:
            gRasterSets[slot] = {};
            break;
        case internal::RasterKind::Pipeline:
            gRasterPipelines[slot] = {};
            break;
        case internal::RasterKind::ComputePipeline:
            gComputePipelines[slot] = nil;
            gComputeInfos[slot] = {};
            break;
        case internal::RasterKind::Count:
            break;
    }
}
RasterStatus RasterDraw(const internal::RasterPacket& packet) noexcept
{
    const auto& pipeline = gRasterPipelines[packet.Pipeline];
    const auto& set = gRasterSets[packet.Set];
    const auto& layout = gRasterLayouts[set.Layout];
    [gEncoder setRenderPipelineState:pipeline.Object];
    [gEncoder setDepthStencilState:pipeline.Depth];
    for (usize i = 0; i < layout.Count; ++i)
    {
        const auto& entry = layout.Entries[i];
        const bool vertex = (static_cast<uint8>(entry.Visibility) & 1) != 0;
        const bool fragment = (static_cast<uint8>(entry.Visibility) & 2) != 0;
        if (entry.Kind == RasterBindingKind::UniformBuffer)
        {
            if (vertex)
            {
                [gEncoder setVertexBuffer:gRasterBuffers[set.Slots[i]] offset:set.Offsets[i] atIndex:entry.Binding];
            }
            if (fragment)
            {
                [gEncoder setFragmentBuffer:gRasterBuffers[set.Slots[i]] offset:set.Offsets[i] atIndex:entry.Binding];
            }
        }
        else if (entry.Kind == RasterBindingKind::Texture2D)
        {
            if (vertex)
            {
                [gEncoder setVertexTexture:gRasterViews[set.Slots[i]] atIndex:entry.Binding];
            }
            if (fragment)
            {
                [gEncoder setFragmentTexture:gRasterViews[set.Slots[i]] atIndex:entry.Binding];
            }
        }
        else
        {
            if (vertex)
            {
                [gEncoder setVertexSamplerState:gRasterSamplers[set.Slots[i]] atIndex:entry.Binding];
            }
            if (fragment)
            {
                [gEncoder setFragmentSamplerState:gRasterSamplers[set.Slots[i]] atIndex:entry.Binding];
            }
        }
    }
    for (usize i = 0; i < pipeline.Info.StreamCount; ++i)
    {
        [gEncoder setVertexBuffer:gRasterBuffers[packet.Vertices[i]]
                           offset:packet.Offsets[i]
                          atIndex:internal::RASTER_BINDINGS + i];
    }
    [gEncoder setCullMode:MTLCullModeNone];
    internal::RasterArea area;
    const auto width = gRasterWidth == 0 ? gFrame.Width : gRasterWidth;
    const auto height = gRasterHeight == 0 ? gFrame.Height : gRasterHeight;
    if (!internal::RasterResolveArea(gRasterRegion, width, height, area))
    {
        return RasterStatus::InvalidDescription;
    }
    if (area.Empty)
    {
        return RasterStatus::Ready;
    }
    const auto& v = area.Viewport;
    const auto& c = area.Scissor;
    [gEncoder setViewport:MTLViewport{static_cast<float64>(v.X),
                                      static_cast<float64>(v.Y),
                                      static_cast<float64>(v.Width),
                                      static_cast<float64>(v.Height),
                                      0,
                                      1}];
    [gEncoder setScissorRect:MTLScissorRect{c.X, c.Y, c.Width, c.Height}];
    if (packet.Indirect != internal::RASTER_CAPACITY)
    {
        [gEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                              indexType:packet.Index32 ? MTLIndexTypeUInt32 : MTLIndexTypeUInt16
                            indexBuffer:gRasterBuffers[packet.Indices]
                      indexBufferOffset:packet.IndexOffset
                         indirectBuffer:gRasterBuffers[packet.Indirect]
                   indirectBufferOffset:packet.IndirectOffset];
    }
    else
    {
        [gEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                             indexCount:packet.IndexCount
                              indexType:packet.Index32 ? MTLIndexTypeUInt32 : MTLIndexTypeUInt16
                            indexBuffer:gRasterBuffers[packet.Indices]
                      indexBufferOffset:packet.IndexOffset
                          instanceCount:packet.InstanceCount];
    }
    return RasterStatus::Ready;
}
void RasterFrameExtent() noexcept
{
    gRasterWidth = gRasterHeight = 0;
    gRasterRegion = {};
}
void RasterEndPass() noexcept
{
    if (gEncoder != nil)
    {
        [gEncoder endEncoding];
        gEncoder = nil;
    }
}
RasterStatus RasterPreparePass(const internal::RasterPassInfo&) noexcept
{
    return RasterStatus::Ready;
}
void RasterTextureBarrier(usize, RasterTextureUse) noexcept {}
RasterStatus RasterBeginPass(const internal::RasterPassInfo& info) noexcept
{
    @autoreleasepool
    {
        const auto& state = info.Description;
        const bool surface = info.Texture == internal::RASTER_CAPACITY;
        auto color = surface ? (gDrawable != nil ? gDrawable.texture : gHeadless) : gRasterTextures[info.Texture];
        auto depth = surface ? gDepth : gRasterDepths[info.Texture];
        auto pass = [MTLRenderPassDescriptor renderPassDescriptor];
        auto attachment = pass.colorAttachments[0];
        attachment.texture = color;
        attachment.loadAction = state.ColorLoad == RasterLoad::Clear  ? MTLLoadActionClear
                                : state.ColorLoad == RasterLoad::Load ? MTLLoadActionLoad
                                                                      : MTLLoadActionDontCare;
        attachment.storeAction = state.ColorStore == RasterStore::Store ? MTLStoreActionStore : MTLStoreActionDontCare;
        attachment.clearColor = MTLClearColorMake(static_cast<float64>(state.Clear[0]),
                                                  static_cast<float64>(state.Clear[1]),
                                                  static_cast<float64>(state.Clear[2]),
                                                  static_cast<float64>(state.Clear[3]));
        pass.depthAttachment.texture = depth;
        pass.depthAttachment.loadAction = state.DepthLoad == RasterLoad::Clear  ? MTLLoadActionClear
                                          : state.DepthLoad == RasterLoad::Load ? MTLLoadActionLoad
                                                                                : MTLLoadActionDontCare;
        pass.depthAttachment.storeAction =
            state.DepthStore == RasterStore::Store ? MTLStoreActionStore : MTLStoreActionDontCare;
        pass.depthAttachment.clearDepth = static_cast<float64>(state.ClearDepth);
        gEncoder = [gCommand renderCommandEncoderWithDescriptor:pass];
        gRasterRegion = state;
        gRasterWidth = static_cast<uint32>(color.width);
        gRasterHeight = static_cast<uint32>(color.height);
        return gEncoder != nil ? RasterStatus::Ready : RasterStatus::Failed;
    }
}
RasterStatus RasterReserveSubmission() noexcept
{
    return RasterStatus::Ready;
}
void RasterSubmit(uint64 ordinal) noexcept
{
    gRasterOrdinals[(gSlot + FRAMES - 1) % FRAMES] = ordinal;
}
uint64 RasterCompleted() noexcept
{
    for (usize i = 0; i < FRAMES; ++i)
    {
        if (gSubmitted[i] != nil && !CheckCommand(gSubmitted[i]))
        {
            return gRasterCompleted;
        }
        if (gSubmitted[i] != nil && gSubmitted[i].status == MTLCommandBufferStatusCompleted &&
            gRasterOrdinals[i] > gRasterCompleted)
        {
            gRasterCompleted = gRasterOrdinals[i];
        }
    }
    return gRasterCompleted;
}
void RasterReset() noexcept
{
    for (auto command : gSubmitted)
    {
        [command waitUntilCompleted];
    }
    for (auto& ordinal : gRasterOrdinals)
    {
        ordinal = 0;
    }
    gRasterCompleted = 0;
}
void RasterShutdown() noexcept {}
} // namespace ludus::graphics::rhi::backend
