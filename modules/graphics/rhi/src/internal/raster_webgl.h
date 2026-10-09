#pragma once
// Included only by rhi_webgl.cpp. Thanks to Khronos, OpenGL ES 3.0.6,
// sections 2.8 (vertex arrays), 2.12.6 (uniform blocks) and 5.2 (sync objects):
// https://registry.khronos.org/OpenGL/specs/es/3.0/es_spec_3.0.pdf
// GL syncs are polled without blocking; shader metadata comes from the offline
// GLSL ES artifact, independently of WGSL/Metal/SPIR-V packing.
namespace ludus::graphics::rhi::LUDUS_RHI_WEBGL_NAMESPACE
{
namespace
{
struct RasterGlShader final
{
    GLuint Object = 0;
    internal::RasterShaderInfo Info;
};
struct RasterGlPipeline final
{
    GLuint Object = 0;
    GLuint Array = 0;
    internal::RasterPipelineInfo Info;
    GLint Textures[8]{-1, -1, -1, -1, -1, -1, -1, -1};
    uint32 Samplers[8]{};
};
struct RasterGlCompletion final
{
    GLsync Sync = nullptr;
    uint64 Ordinal = 0;
    bool Reserved = false;
};
GLuint gRasterBuffers[internal::RASTER_CAPACITY]{};
GLuint gRasterTextures[internal::RASTER_CAPACITY]{};
TextureDescription gRasterTextureDescriptions[internal::RASTER_CAPACITY];
GLuint gRasterFramebuffers[internal::RASTER_CAPACITY]{};
GLuint gRasterRenderTextures[internal::RASTER_CAPACITY]{};
GLuint gRasterSampleFramebuffers[internal::RASTER_CAPACITY]{};
internal::RasterPassInfo gRasterGlPass;
bool gRasterGlPassOpen = false;
GLuint gRasterDepths[internal::RASTER_CAPACITY]{};
GLuint gRasterViews[internal::RASTER_CAPACITY]{};
GLuint gRasterSamplers[internal::RASTER_CAPACITY]{};
RasterGlShader gRasterShaders[internal::RASTER_CAPACITY];
internal::RasterLayout gRasterLayouts[internal::RASTER_CAPACITY];
internal::RasterSet gRasterSets[internal::RASTER_CAPACITY];
RasterGlPipeline gRasterPipelines[internal::RASTER_CAPACITY];
RasterGlCompletion gRasterCompletions[internal::RASTER_CAPACITY];
RasterGlCompletion* gRasterReserved = nullptr;
uint64 gRasterCompleted = 0;
RasterStatus RasterGlResult() noexcept
{
    const auto error = glGetError();
    return error == GL_NO_ERROR        ? RasterStatus::Ready
           : error == GL_OUT_OF_MEMORY ? RasterStatus::OutOfMemory
                                       : RasterStatus::Failed;
}
bool RasterGlInterface(GLuint program, const internal::RasterShaderInfo& shader, RasterGlPipeline& pipeline) noexcept
{
    for (usize i = 0; i < shader.Layout.Count; ++i)
    {
        const auto& entry = shader.Layout.Entries[i];
        if (entry.Kind == RasterBindingKind::UniformBuffer)
        {
            const GLuint block = glGetUniformBlockIndex(program, shader.UniformBlocks[entry.Binding]);
            if (block == GL_INVALID_INDEX)
            {
                return false;
            }
            GLint size = 0;
            glGetActiveUniformBlockiv(program, block, GL_UNIFORM_BLOCK_DATA_SIZE, &size);
            if (size <= 0 || static_cast<usize>(size) > entry.MinSize)
            {
                return false;
            }
            glUniformBlockBinding(program, block, entry.Binding);
        }
        else if (entry.Kind == RasterBindingKind::Texture2D)
        {
            const GLint location = glGetUniformLocation(program, shader.TextureNames[entry.Binding]);
            if (location < 0)
            {
                return false;
            }
            if (pipeline.Textures[entry.Binding] >= 0 &&
                (pipeline.Textures[entry.Binding] != location ||
                 pipeline.Samplers[entry.Binding] != shader.TextureSamplers[entry.Binding]))
            {
                return false;
            }
            pipeline.Textures[entry.Binding] = location;
            pipeline.Samplers[entry.Binding] = shader.TextureSamplers[entry.Binding];
        }
    }
    return true;
}
bool RasterGlProgram(GLuint program, RasterGlPipeline& pipeline) noexcept
{
    const auto& vertex = gRasterShaders[pipeline.Info.Vertex].Info;
    const auto& fragment = gRasterShaders[pipeline.Info.Fragment].Info;
    if (!RasterGlInterface(program, vertex, pipeline) || !RasterGlInterface(program, fragment, pipeline))
    {
        return false;
    }
    GLint count = 0;
    glGetProgramiv(program, GL_ACTIVE_ATTRIBUTES, &count);
    if (count < 0 || static_cast<usize>(count) != vertex.InputCount)
    {
        return false;
    }
    for (GLint i = 0; i < count; ++i)
    {
        char name[128]{};
        GLsizei length = 0;
        GLint size = 0;
        GLenum type = 0;
        glGetActiveAttrib(program, static_cast<GLuint>(i), sizeof(name), &length, &size, &type, name);
        if (size != 1 || length <= 0 || length >= static_cast<GLsizei>(sizeof(name) - 1))
        {
            return false;
        }
        const auto location = glGetAttribLocation(program, name);
        bool found = false;
        for (usize j = 0; j < vertex.InputCount; ++j)
        {
            const auto& input = vertex.Inputs[j];
            const GLenum expected = input.Format == RasterVertexFormat::Float2   ? GL_FLOAT_VEC2
                                    : input.Format == RasterVertexFormat::Float3 ? GL_FLOAT_VEC3
                                                                                 : GL_FLOAT_VEC4;
            if (location == static_cast<GLint>(input.Location) && type == expected)
            {
                found = true;
            }
        }
        if (!found)
        {
            return false;
        }
    }
    glGetProgramiv(program, GL_ACTIVE_UNIFORM_BLOCKS, &count);
    for (GLint i = 0; i < count; ++i)
    {
        char name[128]{};
        GLsizei length = 0;
        glGetActiveUniformBlockName(program, static_cast<GLuint>(i), sizeof(name), &length, name);
        bool found = false;
        for (usize binding = 0; binding < 8; ++binding)
        {
            if (std::strcmp(name, vertex.UniformBlocks[binding]) == 0 ||
                std::strcmp(name, fragment.UniformBlocks[binding]) == 0)
            {
                found = true;
            }
        }
        if (!found || length <= 0 || length >= static_cast<GLsizei>(sizeof(name) - 1))
        {
            return false;
        }
    }
    glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &count);
    for (GLint i = 0; i < count; ++i)
    {
        const auto index = static_cast<GLuint>(i);
        GLint block = -1;
        glGetActiveUniformsiv(program, 1, &index, GL_UNIFORM_BLOCK_INDEX, &block);
        if (block >= 0)
        {
            continue;
        }
        char name[128]{};
        GLsizei length = 0;
        GLint size = 0;
        GLenum type = 0;
        glGetActiveUniform(program, index, sizeof(name), &length, &size, &type, name);
        if (size != 1 || type != GL_SAMPLER_2D || length <= 0 || length >= static_cast<GLsizei>(sizeof(name) - 1))
        {
            return false;
        }
        bool found = false;
        for (usize binding = 0; binding < 8; ++binding)
        {
            if (std::strcmp(name, vertex.TextureNames[binding]) == 0 ||
                std::strcmp(name, fragment.TextureNames[binding]) == 0)
            {
                found = true;
            }
        }
        if (!found)
        {
            return false;
        }
    }
    return true;
}
} // namespace
ComputeCapabilities ComputeLimits() noexcept
{
    return {};
}
RasterStatus ComputeCreatePipeline(usize, const internal::ComputePipelineInfo&, uint32) noexcept
{
    return RasterStatus::Unsupported;
}
RasterStatus ComputeEncode(const internal::ComputePacket&) noexcept
{
    return RasterStatus::Unsupported;
}
void ComputeBufferBarrier(usize, GraphAccessMode) noexcept {}
RasterCapabilities RasterLimits() noexcept
{
    if (gContext <= 0)
    {
        return {};
    }
    GLint alignment = 0;
    GLint uniform = 0;
    GLint attributes = 0;
    GLint textures = 0;
    GLint vertexTextures = 0;
    GLint vertexBlocks = 0;
    GLint fragmentBlocks = 0;
    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &alignment);
    glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &uniform);
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &attributes);
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &textures);
    glGetIntegerv(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, &vertexTextures);
    glGetIntegerv(GL_MAX_VERTEX_UNIFORM_BLOCKS, &vertexBlocks);
    glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_BLOCKS, &fragmentBlocks);
    if (alignment <= 0 || uniform < 16384 || attributes < 8 || textures < 8 || vertexTextures < 8 || vertexBlocks < 8 ||
        fragmentBlocks < 8)
    {
        return {};
    }
    return {64U * 1024U * 1024U,
            gMaxDimension < 4096 ? gMaxDimension : 4096,
            static_cast<usize>(alignment > 256 ? alignment : 256),
            16384,
            static_cast<uint32>(internal::RASTER_CAPACITY),
            static_cast<uint32>(internal::RASTER_DRAWS)};
}
RasterStatus
RasterCreateBuffer(usize slot, const BufferDescription& info, std::span<const uint8> bytes, uint32) noexcept
{
    const bool indices = info.Role == BufferRole::Index16 || info.Role == BufferRole::Index32;
    const GLenum target = indices                            ? GL_ELEMENT_ARRAY_BUFFER
                          : info.Role == BufferRole::Uniform ? GL_UNIFORM_BUFFER
                                                             : GL_ARRAY_BUFFER;
    // WebGL classifies index buffers at their first binding. Use the default
    // VAO during setup so initialization cannot replace a pipeline's index binding.
    if (indices)
    {
        glBindVertexArray(0);
    }
    glGenBuffers(1, &gRasterBuffers[slot]);
    glBindBuffer(target, gRasterBuffers[slot]);
    glBufferData(target, static_cast<GLsizeiptr>(bytes.size()), bytes.data(), GL_STATIC_DRAW);
    glBindBuffer(target, 0);
    return RasterGlResult();
}
RasterStatus
RasterCreateTexture(usize slot, const TextureDescription& info, const TextureUpload& upload, uint32) noexcept
{
    gRasterTextureDescriptions[slot] = info;
    glGenTextures(1, &gRasterTextures[slot]);
    glBindTexture(GL_TEXTURE_2D, gRasterTextures[slot]);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    uint32 width = info.Width, height = info.Height;
    for (uint32 level = 0; level < info.MipLevels; ++level)
    {
        const auto mip = upload.Mips.empty() ? TextureMipUpload{0, upload.RowPitch} : upload.Mips[level];
        const usize channels = info.Format == RasterFormat::R8Unorm    ? 1U
                               : info.Format == RasterFormat::Rg8Unorm ? 2U
                                                                       : 4U;
        glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(mip.RowPitch / channels));
        glTexImage2D(GL_TEXTURE_2D,
                     static_cast<GLint>(level),
                     info.Format == RasterFormat::R8Unorm      ? GL_R8
                     : info.Format == RasterFormat::Rg8Unorm   ? GL_RG8
                     : info.Format == RasterFormat::Rgba8Unorm ? GL_RGBA8
                                                               : GL_SRGB8_ALPHA8,
                     static_cast<GLsizei>(width),
                     static_cast<GLsizei>(height),
                     0,
                     info.Format == RasterFormat::R8Unorm    ? GL_RED
                     : info.Format == RasterFormat::Rg8Unorm ? GL_RG
                                                             : GL_RGBA,
                     GL_UNSIGNED_BYTE,
                     info.Attachment ? nullptr : upload.Bytes.data() + mip.Offset);
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(info.MipLevels - 1));
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (info.Attachment)
    {
        glGenTextures(1, &gRasterRenderTextures[slot]);
        glBindTexture(GL_TEXTURE_2D, gRasterRenderTextures[slot]);
        glTexStorage2D(GL_TEXTURE_2D,
                       1,
                       info.Format == RasterFormat::Rgba8Unorm ? GL_RGBA8 : GL_SRGB8_ALPHA8,
                       static_cast<GLsizei>(info.Width),
                       static_cast<GLsizei>(info.Height));
        glGenRenderbuffers(1, &gRasterDepths[slot]);
        glBindRenderbuffer(GL_RENDERBUFFER, gRasterDepths[slot]);
        glRenderbufferStorage(GL_RENDERBUFFER,
                              GL_DEPTH_COMPONENT24,
                              static_cast<GLsizei>(info.Width),
                              static_cast<GLsizei>(info.Height));
        glGenFramebuffers(1, &gRasterFramebuffers[slot]);
        glBindFramebuffer(GL_FRAMEBUFFER, gRasterFramebuffers[slot]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gRasterRenderTextures[slot], 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, gRasterDepths[slot]);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            return RasterStatus::Unsupported;
        }
        glGenFramebuffers(1, &gRasterSampleFramebuffers[slot]);
        glBindFramebuffer(GL_FRAMEBUFFER, gRasterSampleFramebuffers[slot]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gRasterTextures[slot], 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            return RasterStatus::Unsupported;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    return RasterGlResult();
}
RasterStatus RasterCreateView(usize slot, const internal::RasterViewSource& source, uint32) noexcept
{
    gRasterViews[slot] = gRasterTextures[source.Texture];
    return RasterStatus::Ready;
}
RasterStatus RasterCreateSampler(usize slot, const SamplerDescription& info, uint32) noexcept
{
    glGenSamplers(1, &gRasterSamplers[slot]);
    const GLuint sampler = gRasterSamplers[slot];
    const auto filter = info.Filter == RasterFilter::Nearest ? GL_NEAREST : GL_LINEAR;
    const auto minFilter =
        info.MipFilter == RasterMipFilter::None ? filter
        : info.MipFilter == RasterMipFilter::Nearest
            ? (info.Filter == RasterFilter::Nearest ? GL_NEAREST_MIPMAP_NEAREST : GL_LINEAR_MIPMAP_NEAREST)
            : (info.Filter == RasterFilter::Nearest ? GL_NEAREST_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_LINEAR);
    glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, minFilter);
    glSamplerParameterf(sampler, GL_TEXTURE_MIN_LOD, info.MinLod);
    glSamplerParameterf(sampler, GL_TEXTURE_MAX_LOD, info.MipFilter == RasterMipFilter::None ? 0 : info.MaxLod);
    glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, filter);
    glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, info.U == RasterAddress::Clamp ? GL_CLAMP_TO_EDGE : GL_REPEAT);
    glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, info.V == RasterAddress::Clamp ? GL_CLAMP_TO_EDGE : GL_REPEAT);
    return RasterGlResult();
}
RasterStatus RasterCreateShader(usize slot,
                                const ShaderDescription& info,
                                const internal::RasterShaderInfo& metadata,
                                uint32) noexcept
{
    auto& shader = gRasterShaders[slot];
    shader.Info = metadata;
    shader.Object = Compile(info.Stage == ShaderStage::Vertex ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER, info.GlslEs);
    return shader.Object != 0 ? RasterGlResult() : RasterStatus::Failed;
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
    auto& pipeline = gRasterPipelines[slot];
    pipeline.Info = info;
    pipeline.Object = glCreateProgram();
    if (pipeline.Object == 0)
    {
        return RasterStatus::OutOfMemory;
    }
    glAttachShader(pipeline.Object, gRasterShaders[info.Vertex].Object);
    glAttachShader(pipeline.Object, gRasterShaders[info.Fragment].Object);
    glLinkProgram(pipeline.Object);
    GLint linked = 0;
    glGetProgramiv(pipeline.Object, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE || !RasterGlProgram(pipeline.Object, pipeline))
    {
        char log[512]{};
        GLsizei length = 0;
        glGetProgramInfoLog(pipeline.Object, sizeof(log), &length, log);
        LUDUS_LOG_WARN(LOG_RHI,
                       "Raster GLSL ES link/interface failure: {}",
                       std::string_view(log, static_cast<usize>(length)));
        return RasterStatus::Failed;
    }
    glGenVertexArrays(1, &pipeline.Array);
    return RasterGlResult();
}
void RasterDestroy(internal::RasterKind kind, usize slot) noexcept
{
    switch (kind)
    {
        case internal::RasterKind::Buffer:
            glDeleteBuffers(1, &gRasterBuffers[slot]);
            gRasterBuffers[slot] = 0;
            break;
        case internal::RasterKind::Texture:
            glDeleteFramebuffers(1, &gRasterSampleFramebuffers[slot]);
            glDeleteTextures(1, &gRasterRenderTextures[slot]);
            gRasterSampleFramebuffers[slot] = gRasterRenderTextures[slot] = 0;
            glDeleteFramebuffers(1, &gRasterFramebuffers[slot]);
            glDeleteRenderbuffers(1, &gRasterDepths[slot]);
            gRasterFramebuffers[slot] = gRasterDepths[slot] = 0;
            glDeleteTextures(1, &gRasterTextures[slot]);
            gRasterTextures[slot] = 0;
            break;
        case internal::RasterKind::View:
            gRasterViews[slot] = 0;
            break;
        case internal::RasterKind::Sampler:
            glDeleteSamplers(1, &gRasterSamplers[slot]);
            gRasterSamplers[slot] = 0;
            break;
        case internal::RasterKind::Shader:
            glDeleteShader(gRasterShaders[slot].Object);
            gRasterShaders[slot] = {};
            break;
        case internal::RasterKind::Layout:
            gRasterLayouts[slot] = {};
            break;
        case internal::RasterKind::Set:
            gRasterSets[slot] = {};
            break;
        case internal::RasterKind::Pipeline:
            glDeleteProgram(gRasterPipelines[slot].Object);
            glDeleteVertexArrays(1, &gRasterPipelines[slot].Array);
            gRasterPipelines[slot] = {};
            break;
        case internal::RasterKind::ComputePipeline:
        case internal::RasterKind::Count:
            break;
    }
}
RasterStatus RasterDraw(const internal::RasterPacket& packet) noexcept
{
    const auto& pipeline = gRasterPipelines[packet.Pipeline];
    const auto& info = pipeline.Info;
    const auto& set = gRasterSets[packet.Set];
    const auto& layout = gRasterLayouts[set.Layout];
    glUseProgram(pipeline.Object);
    glBindVertexArray(pipeline.Array);
    for (usize i = 0; i < info.AttributeCount; ++i)
    {
        const auto& attribute = info.Attributes[i];
        const auto& stream = info.Streams[attribute.Stream];
        glBindBuffer(GL_ARRAY_BUFFER, gRasterBuffers[packet.Vertices[attribute.Stream]]);
        const GLint components = attribute.Format == RasterVertexFormat::Float2   ? 2
                                 : attribute.Format == RasterVertexFormat::Float3 ? 3
                                                                                  : 4;
        // GL interprets this pointer as a byte offset into the bound buffer.
        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        const void* offset = reinterpret_cast<const void*>(packet.Offsets[attribute.Stream] + attribute.Offset);
        glVertexAttribPointer(attribute.Location,
                              components,
                              GL_FLOAT,
                              GL_FALSE,
                              static_cast<GLsizei>(stream.Stride),
                              offset);
        glEnableVertexAttribArray(attribute.Location);
        glVertexAttribDivisor(attribute.Location, stream.PerInstance ? 1 : 0);
    }
    for (usize i = 0; i < layout.Count; ++i)
    {
        const auto& entry = layout.Entries[i];
        if (entry.Kind == RasterBindingKind::UniformBuffer)
        {
            glBindBufferRange(GL_UNIFORM_BUFFER,
                              entry.Binding,
                              gRasterBuffers[set.Slots[i]],
                              static_cast<GLintptr>(set.Offsets[i]),
                              static_cast<GLsizeiptr>(set.Sizes[i]));
        }
        else if (entry.Kind == RasterBindingKind::Texture2D)
        {
            const uint32 sampler = pipeline.Samplers[entry.Binding];
            GLuint object = 0;
            for (usize j = 0; j < layout.Count; ++j)
            {
                if (layout.Entries[j].Kind == RasterBindingKind::Sampler && layout.Entries[j].Binding == sampler)
                {
                    object = gRasterSamplers[set.Slots[j]];
                }
            }
            glActiveTexture(GL_TEXTURE0 + entry.Binding);
            glBindTexture(GL_TEXTURE_2D, gRasterViews[set.Slots[i]]);
            glBindSampler(entry.Binding, object);
            glUniform1i(pipeline.Textures[entry.Binding], static_cast<GLint>(entry.Binding));
        }
    }
    if (info.Depth)
    {
        glEnable(GL_DEPTH_TEST);
    }
    else
    {
        glDisable(GL_DEPTH_TEST);
    }
    const GLenum comparisons[]{GL_LESS, GL_LEQUAL, GL_GREATER, GL_GEQUAL, GL_ALWAYS};
    internal::RasterArea area;
    const auto& pass = gRasterGlPass;
    const bool surface = !gRasterGlPassOpen || pass.Texture == internal::RASTER_CAPACITY;
    const auto width = surface ? gTarget.Width : gRasterTextureDescriptions[pass.Texture].Width;
    const auto height = surface ? gTarget.Height : gRasterTextureDescriptions[pass.Texture].Height;
    if (!internal::RasterResolveArea(gRasterGlPassOpen ? pass.Description : RasterPassDescription{},
                                     width,
                                     height,
                                     area))
    {
        return RasterStatus::InvalidDescription;
    }
    if (area.Empty)
    {
        return RasterStatus::Ready;
    }
    const auto& v = area.Viewport;
    const auto& c = area.Scissor;
    glViewport(static_cast<GLint>(v.X),
               static_cast<GLint>(height - v.Y - v.Height),
               static_cast<GLsizei>(v.Width),
               static_cast<GLsizei>(v.Height));
    glEnable(GL_SCISSOR_TEST);
    glScissor(static_cast<GLint>(c.X),
              static_cast<GLint>(height - c.Y - c.Height),
              static_cast<GLsizei>(c.Width),
              static_cast<GLsizei>(c.Height));
    glDepthFunc(comparisons[static_cast<usize>(info.DepthCompare)]);
    glDepthMask(info.Depth && info.DepthWrite ? GL_TRUE : GL_FALSE);
    if (info.Blend)
    {
        glEnable(GL_BLEND);
    }
    else
    {
        glDisable(GL_BLEND);
    }
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gRasterBuffers[packet.Indices]);
    // GL interprets this pointer-shaped argument as an element-buffer byte offset.
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    const void* indexOffset = reinterpret_cast<const void*>(packet.IndexOffset);
    glDrawElementsInstanced(GL_TRIANGLES,
                            static_cast<GLsizei>(packet.IndexCount),
                            packet.Index32 ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT,
                            indexOffset,
                            static_cast<GLsizei>(packet.InstanceCount));
    glBindVertexArray(0);
    return RasterGlResult();
}
uint64 RasterCompleted() noexcept
{
    for (auto& completion : gRasterCompletions)
    {
        if (completion.Sync == nullptr)
        {
            continue;
        }
        const auto status = glClientWaitSync(completion.Sync, 0, 0);
        if (status == GL_WAIT_FAILED)
        {
            internal::Fail(gSession,
                           emscripten_is_webgl_context_lost(gContext) ? StartupError::DeviceLost
                                                                      : StartupError::RenderingUnavailable);
            return gRasterCompleted;
        }
        if (status == GL_ALREADY_SIGNALED || status == GL_CONDITION_SATISFIED)
        {
            if (completion.Ordinal > gRasterCompleted)
            {
                gRasterCompleted = completion.Ordinal;
            }
            glDeleteSync(completion.Sync);
            completion = {};
        }
    }
    return gRasterCompleted;
}
void RasterEndPass() noexcept
{
    glDisable(GL_SCISSOR_TEST);
    if (!gRasterGlPassOpen)
    {
        return;
    }
    const auto& pass = gRasterGlPass;
    if (pass.Texture != internal::RASTER_CAPACITY && pass.Description.ColorStore == RasterStore::Store)
    {
        // GL framebuffer rows run bottom-up; uploaded/sampled texture rows in
        // the portable API run top-down. A private flipped blit preserves the
        // same UV convention as Metal, Vulkan and WebGPU, including Load passes.
        const auto& texture = gRasterTextureDescriptions[pass.Texture];
        const auto width = static_cast<GLint>(texture.Width), height = static_cast<GLint>(texture.Height);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, gRasterFramebuffers[pass.Texture]);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gRasterSampleFramebuffers[pass.Texture]);
        glBlitFramebuffer(0, 0, width, height, 0, height, width, 0, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, gRasterFramebuffers[pass.Texture]);
    }
    GLenum discard[2]{};
    GLsizei count = 0;
    const bool surface = pass.Texture == internal::RASTER_CAPACITY;
    if (pass.Description.ColorStore == RasterStore::Discard)
    {
        discard[count++] = surface ? GL_COLOR : GL_COLOR_ATTACHMENT0;
    }
    if (pass.Description.DepthStore == RasterStore::Discard)
    {
        discard[count++] = surface ? GL_DEPTH : GL_DEPTH_ATTACHMENT;
    }
    if (count != 0)
    {
        glInvalidateFramebuffer(GL_FRAMEBUFFER, count, discard);
    }
    gRasterGlPassOpen = false;
}
void RasterTextureBarrier(usize, RasterTextureUse) noexcept {}
RasterStatus RasterPreparePass(const internal::RasterPassInfo& info) noexcept
{
    if (info.Texture == internal::RASTER_CAPACITY || gRasterFramebuffers[info.Texture] != 0)
    {
        return RasterStatus::Ready;
    }
    return RasterStatus::InvalidState;
}
RasterStatus RasterBeginPass(const internal::RasterPassInfo& info) noexcept
{
    gRasterGlPass = info;
    gRasterGlPassOpen = true;
    const auto& state = info.Description;
    const bool surface = info.Texture == internal::RASTER_CAPACITY;
    glBindFramebuffer(GL_FRAMEBUFFER, surface ? 0 : gRasterFramebuffers[info.Texture]);
    const auto width = surface ? gTarget.Width : gRasterTextureDescriptions[info.Texture].Width;
    const auto height = surface ? gTarget.Height : gRasterTextureDescriptions[info.Texture].Height;
    glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(state.DepthReadOnly ? GL_FALSE : GL_TRUE);
    glClearColor(state.Clear[0], state.Clear[1], state.Clear[2], state.Clear[3]);
    glClearDepthf(state.ClearDepth);
    GLbitfield clear = 0;
    if (state.ColorLoad != RasterLoad::Load)
    {
        clear |= GL_COLOR_BUFFER_BIT;
    }
    if (state.DepthLoad != RasterLoad::Load)
    {
        clear |= GL_DEPTH_BUFFER_BIT;
    }
    if (clear != 0)
    {
        glClear(clear);
    }
    return RasterGlResult();
}
RasterStatus RasterReserveSubmission() noexcept
{
    (void)RasterCompleted();
    if (gRasterReserved != nullptr)
    {
        return RasterStatus::Ready;
    }
    for (auto& completion : gRasterCompletions)
    {
        if (completion.Sync == nullptr && !completion.Reserved)
        {
            completion.Reserved = true;
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
    auto& completion = *gRasterReserved;
    gRasterReserved = nullptr;
    completion.Reserved = false;
    completion.Ordinal = ordinal;
    completion.Sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    glFlush();
    if (completion.Sync == nullptr)
    {
        internal::Fail(gSession, StartupError::RenderingUnavailable);
    }
}
void RasterReset() noexcept
{
    gRasterGlPassOpen = false;
    gRasterGlPass = {};
    if (gContext > 0 && !emscripten_is_webgl_context_lost(gContext))
    {
        glFinish();
    }
    for (auto& completion : gRasterCompletions)
    {
        if (completion.Sync != nullptr)
        {
            glDeleteSync(completion.Sync);
        }
        completion = {};
    }
    gRasterReserved = nullptr;
    gRasterCompleted = 0;
}
void RasterShutdown() noexcept {}
} // namespace ludus::graphics::rhi::LUDUS_RHI_WEBGL_NAMESPACE
