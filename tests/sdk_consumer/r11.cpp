#include "r11compute.h"
#include "r11scalar.h"
#if !defined(LUDUS_PLATFORM_WEB)
#    include "r11arrays.h"
#    include "r11constant.h"
#endif
#include <ludus/foundation/base/types.h>
#include <ludus/foundation/logging/log_format.hpp>
#include <ludus/foundation/logging/log_system.hpp>
#include <ludus/foundation/time/time.hpp>
#include <ludus/graphics/rhi/graph.h>
#include <ludus/graphics/rhi/measurement.h>

#include <cstring>
#include <span>
#include <type_traits>

namespace
{
using namespace ludus::foundation;
namespace rhi = ludus::graphics::rhi;
constexpr logging::LogCategory LOG_R11{"R11-Fixture"};
constexpr usize COLOR_COUNT = 4;
constexpr usize STATIC_SET_COUNT = 32;
constexpr uint32 WIDTH = 16;
constexpr uint32 HEIGHT = 4;
constexpr usize UNIFORM_ALIGNMENT = 256;
constexpr usize PIXEL_BYTES = usize{WIDTH} * HEIGHT * 4;
constexpr usize UNIFORM_BYTES = UNIFORM_ALIGNMENT * COLOR_COUNT;
constexpr float32 COLORS[COLOR_COUNT][4]{
    {1, 0, 0, 1},
    {0, 1, 0, 1},
    {0, 0, 1, 1},
    {1, 1, 0, 1},
};
rhi::DeviceHandle gDevice;
rhi::SurfaceHandle gSurface;
rhi::BufferHandle gUniform;
rhi::BufferHandle gVertices;
rhi::BufferHandle gIndices;
rhi::TextureHandle gTarget;
rhi::RasterShaderHandle gVertex;
rhi::RasterShaderHandle gScalar;
rhi::BindingLayoutHandle gStaticLayout;
rhi::BindingLayoutHandle gDynamicLayout;
rhi::BindingSetHandle gStaticSets[STATIC_SET_COUNT];
rhi::BindingSetHandle gDynamicSet;
rhi::RasterPipelineHandle gStaticPipeline;
rhi::RasterPipelineHandle gDynamicPipeline;
rhi::RasterVertexSlice gSlice;
uint8 gReferencePixels[PIXEL_BYTES]{};

template <typename T, usize N>
    requires std::is_trivially_copyable_v<T>
std::span<const uint8> GetBytes(const T (&values)[N]) noexcept
{
    return {reinterpret_cast<const uint8*>(values), sizeof(values)};
}
bool IsReady(rhi::RasterStatus status) noexcept
{
    return status == rhi::RasterStatus::Ready;
}
bool CreatePipeline(rhi::BindingLayoutHandle layout,
                    rhi::RasterShaderHandle vertex,
                    rhi::RasterShaderHandle fragment,
                    rhi::RasterPipelineHandle& outPipeline) noexcept
{
    const rhi::RasterVertexStream stream{ .Stride = 8 };
    const rhi::RasterVertexAttribute attribute{ .Format = rhi::RasterVertexFormat::Float2 };
    const rhi::RasterPipelineDescription description
    {
        .Vertex = vertex,
        .Fragment = fragment,
        .Layout = layout,
        .Streams = {&stream, 1},
        .Attributes = {&attribute, 1},
        .Target = rhi::RasterTarget::Rgba8Unorm,
    };
    return IsReady(rhi::CreateRasterPipeline(gDevice, description, outPipeline));
}
bool Prepare() noexcept
{
    const float32 vertices[8]{-1, -1, 1, -1, -1, 1, 1, 1};
    const uint16 indices[6]{0, 1, 2, 2, 1, 3};
    uint8 uniform[UNIFORM_BYTES]{};
    for (usize color = 0; color < COLOR_COUNT; ++color)
    {
        std::memcpy(uniform + color * UNIFORM_ALIGNMENT, COLORS[color], sizeof(COLORS[color]));
    }
    if (IsReady(rhi::CreateBuffer(gDevice,
                                  { .Role = rhi::BufferRole::Vertex, .Size = sizeof(vertices) },
                                  GetBytes(vertices),
                                  gVertices)) == false ||
        IsReady(rhi::CreateBuffer(gDevice,
                                  { .Role = rhi::BufferRole::Index16, .Size = sizeof(indices) },
                                  GetBytes(indices),
                                  gIndices)) == false ||
        IsReady(rhi::CreateBuffer(gDevice,
                                  { .Role = rhi::BufferRole::Uniform, .Size = sizeof(uniform) },
                                  uniform,
                                  gUniform)) == false ||
        IsReady(rhi::CreateTexture(gDevice, { .Width = WIDTH, .Height = HEIGHT, .Attachment = true }, {}, gTarget)) ==
            false ||
        IsReady(rhi::CreateRasterShader(gDevice, ludus::shaders::r11scalar::Vertex(), gVertex)) == false ||
        IsReady(rhi::CreateRasterShader(gDevice, ludus::shaders::r11scalar::Fragment(), gScalar)) == false)
    {
        return false;
    }
    const rhi::RasterBinding staticBinding
    {
        .Kind = rhi::RasterBindingKind::UniformBuffer,
        .Visibility = rhi::RasterVisibility::Fragment,
        .MinSize = 16,
    };
    rhi::RasterBinding dynamicBinding = staticBinding;
    dynamicBinding.HasDynamicOffset = true;
    if (IsReady(rhi::CreateBindingLayout(gDevice, {&staticBinding, 1}, gStaticLayout)) == false ||
        IsReady(rhi::CreateBindingLayout(gDevice, {&dynamicBinding, 1}, gDynamicLayout)) == false ||
        CreatePipeline(gStaticLayout, gVertex, gScalar, gStaticPipeline) == false ||
        CreatePipeline(gDynamicLayout, gVertex, gScalar, gDynamicPipeline) == false)
    {
        return false;
    }
    gSlice = { .Buffer = gVertices, .Size = sizeof(vertices) };
    rhi::StorageUsage initialStorage;
    if (IsReady(rhi::GetStorageUsage(gDevice, initialStorage)) == false)
    {
        return false;
    }
    const uint64 staticStarted = time::NowTicks();
    for (usize index = 0; index < STATIC_SET_COUNT; ++index)
    {
        const rhi::RasterBindingResource resource
        {
            .Buffer = gUniform,
            .Offset = (index % COLOR_COUNT) * UNIFORM_ALIGNMENT,
            .Size = 16,
        };
        if (IsReady(rhi::CreateBindingSet(gDevice, gStaticLayout, {&resource, 1}, gStaticSets[index])) == false)
        {
            return false;
        }
    }
    const uint64 staticDuration = time::NowTicks() - staticStarted;
    rhi::StorageUsage staticStorage;
    if (IsReady(rhi::GetStorageUsage(gDevice, staticStorage)) == false)
    {
        return false;
    }
    const rhi::RasterBindingResource resource{ .Buffer = gUniform, .Size = 16 };
    const uint64 dynamicStarted = time::NowTicks();
    if (IsReady(rhi::CreateBindingSet(gDevice, gDynamicLayout, {&resource, 1}, gDynamicSet)) == false)
    {
        return false;
    }
    const uint64 dynamicDuration = time::NowTicks() - dynamicStarted;
    rhi::StorageUsage dynamicStorage;
    if (IsReady(rhi::GetStorageUsage(gDevice, dynamicStorage)) == false)
    {
        return false;
    }
    rhi::AllocationTelemetry telemetry;
    if (IsReady(rhi::GetAllocationTelemetry(gDevice, telemetry)) == false ||
        telemetry.BindingDescriptors != STATIC_SET_COUNT + 1)
    {
        return false;
    }
    if (telemetry.DescriptorArenasAvailable == true && telemetry.DescriptorArenaPools >= STATIC_SET_COUNT)
    {
        return false;
    }
    LUDUS_LOG_INFO(LOG_R11,
                   "CPU creation: {} static snapshots {} ns; one dynamic snapshot {} ns; populated descriptors {}; "
                   "native pools {} reserved slots {} (driver bytes unavailable)",
                   STATIC_SET_COUNT,
                   staticDuration,
                   dynamicDuration,
                   telemetry.BindingDescriptors,
                   telemetry.DescriptorArenaPools,
                   telemetry.ReservedArenaDescriptors);
    LUDUS_LOG_INFO(LOG_R11,
                   "Retained host metadata: initial {} bytes; static {} bytes including cold page/pool growth; "
                   "additional dynamic {} bytes; peak {} bytes",
                   initialStorage.HostBytes,
                   staticStorage.HostBytes - initialStorage.HostBytes,
                   dynamicStorage.HostBytes - staticStorage.HostBytes,
                   dynamicStorage.PeakHostBytes);
    return true;
}
rhi::RasterDraw MakeDraw(bool isDynamic, usize color) noexcept
{
    rhi::RasterDraw draw
    {
        .Pipeline = isDynamic == true ? gDynamicPipeline : gStaticPipeline,
        .Bindings = isDynamic == true ? gDynamicSet : gStaticSets[color],
        .Vertices = {&gSlice, 1},
        .Indices = gIndices,
        .IndexCount = 6,
        .VertexCount = 4,
    };
    if (isDynamic == true)
    {
        draw.BindingOffsets.Bytes[0] = static_cast<uint32>(color * UNIFORM_ALIGNMENT);
    }
    return draw;
}
bool ReadPixels(uint8 (&outPixels)[PIXEL_BYTES]) noexcept
{
    rhi::ReadbackTicket readback;
    if (rhi::RequestTextureReadback(gDevice,
                                    gTarget,
                                    {
                                        .Width = WIDTH,
                                        .Height = HEIGHT,
                                    },
                                    usize{WIDTH} * 4,
                                    readback) != rhi::RasterStatus::Pending)
    {
        return false;
    }
    const uint64 started = time::NowTicks();
    rhi::RasterStatus status = rhi::RasterStatus::Pending;
    while (status == rhi::RasterStatus::Pending && time::NowTicks() - started < 10 * time::NANOSECONDS_PER_SECOND)
    {
        status = rhi::CopyReadback(gDevice, readback, outPixels, sizeof(outPixels));
    }
    const bool isReady = IsReady(status);
    return IsReady(rhi::Release(gDevice, readback)) == true && isReady == true;
}
bool CheckPixels(const uint8 (&pixels)[PIXEL_BYTES]) noexcept
{
    for (usize pixel = 0; pixel < usize{WIDTH} * HEIGHT; ++pixel)
    {
        const usize color = (pixel % WIDTH) / (WIDTH / COLOR_COUNT);
        for (usize channel = 0; channel < 4; ++channel)
        {
            if (pixels[pixel * 4 + channel] != static_cast<uint8>(COLORS[color][channel] * 255))
            {
                LUDUS_LOG_ERROR(LOG_R11, "Pixel {} channel {} actual {}", pixel, channel, pixels[pixel * 4 + channel]);
                return false;
            }
        }
    }
    return true;
}
bool RenderDirect(bool isDynamic) noexcept
{
    if (rhi::BeginFrame(gDevice, gSurface) != rhi::DeviceStatus::Ready)
    {
        return false;
    }
    for (usize color = 0; color < COLOR_COUNT; ++color)
    {
        const rhi::RasterPassDescription pass
        {
            .Color = gTarget,
            .ColorLoad = color == 0 ? rhi::RasterLoad::Clear : rhi::RasterLoad::Load,
            .Scissor =
            {
                .X = static_cast<uint32>(color * WIDTH / COLOR_COUNT),
                .Width = WIDTH / COLOR_COUNT,
                .Height = HEIGHT,
            },
            .UseScissor = true,
        };
        if (IsReady(rhi::BeginRasterPass(gDevice, pass)) == false ||
            IsReady(rhi::DrawIndexed(gDevice, MakeDraw(isDynamic, color))) == false)
        {
            return false;
        }
    }
    return rhi::EndFrame(gDevice) == rhi::DeviceStatus::Ready;
}
bool RenderGraph() noexcept
{
    rhi::OrderedGraph graph;
    rhi::GraphVersion vertices;
    rhi::GraphVersion indices;
    rhi::GraphVersion uniform;
    rhi::GraphVersion target;
    rhi::RasterTextureState targetState;
    if (IsReady(rhi::CreateOrderedGraph(gDevice, graph)) == false ||
        IsReady(rhi::ImportGraphBuffer(gDevice, graph, gVertices, vertices)) == false ||
        IsReady(rhi::ImportGraphBuffer(gDevice, graph, gIndices, indices)) == false ||
        IsReady(rhi::ImportGraphBuffer(gDevice, graph, gUniform, uniform)) == false ||
        IsReady(rhi::GetTextureState(gDevice, gTarget, targetState)) == false ||
        IsReady(rhi::ImportGraphTexture(gDevice,
                                        graph,
                                        gTarget,
                                        targetState,
                                        rhi::RasterTextureUse::ColorAttachment,
                                        target)) == false)
    {
        return false;
    }
    for (usize color = 0; color < COLOR_COUNT; ++color)
    {
        rhi::GraphVersion output;
        if (IsReady(rhi::NextGraphVersion(gDevice, graph, target, output)) == false)
        {
            return false;
        }
        const rhi::RasterDraw draw = MakeDraw(true, color);
        const rhi::GraphUse uses[]{
            { .Resource = vertices, .Access = rhi::GraphAccessMode::Vertex, .Size = gSlice.Size },
            { .Resource = indices, .Access = rhi::GraphAccessMode::Index, .Size = 12 },
            {
                .Resource = uniform,
                .Access = rhi::GraphAccessMode::Uniform,
                .Offset = color * UNIFORM_ALIGNMENT,
                .Size = 16,
                .Visibility = rhi::RasterVisibility::Fragment,
            },
            {
                .Resource = output,
                .Access = color == 0 ? rhi::GraphAccessMode::ColorWrite : rhi::GraphAccessMode::ColorReadWrite,
            },
        };
        const rhi::GraphPassDescription pass
        {
            .Name = "dynamic offsets",
            .Attachment =
            {
                .Color = gTarget,
                .ColorLoad = color == 0 ? rhi::RasterLoad::Clear : rhi::RasterLoad::Load,
                .Scissor =
                {
                    .X = static_cast<uint32>(color * WIDTH / COLOR_COUNT),
                    .Width = WIDTH / COLOR_COUNT,
                    .Height = HEIGHT,
                },
                .UseScissor = true,
            },
            .Uses = uses,
            .UseCount = 4,
            .Draws = &draw,
            .DrawCount = 1,
            .SideEffect = true,
        };
        if (IsReady(rhi::AddGraphPass(gDevice, graph, pass)) == false)
        {
            return false;
        }
        target = output;
    }
    rhi::GraphReport report;
    rhi::SubmissionToken submission;
    return IsReady(rhi::CompileOrderedGraph(gDevice, graph, report)) == true &&
           IsReady(rhi::ExecuteOrderedGraph(gDevice, gSurface, graph, submission)) == true;
}
#if !defined(LUDUS_PLATFORM_WEB)
bool RenderArrays(const rhi::RasterShaderDescription& fragment, bool isNonUniform) noexcept
{
    rhi::RasterShaderHandle shader;
    rhi::RasterShaderHandle vertex;
    rhi::BindingLayoutHandle layout;
    rhi::BindingSetHandle set;
    rhi::RasterPipelineHandle pipeline;
    rhi::BufferHandle selection;
    rhi::TextureHandle textures[COLOR_COUNT];
    rhi::TextureViewHandle views[COLOR_COUNT];
    rhi::SamplerHandle sampler;
    const float32 white[4]{1, 1, 1, 1};
    if (IsReady(rhi::CreateBuffer(gDevice,
                                  { .Role = rhi::BufferRole::Uniform, .Size = sizeof(white) },
                                  GetBytes(white),
                                  selection)) == false ||
        IsReady(rhi::CreateSampler(gDevice, {}, sampler)) == false ||
        IsReady(rhi::CreateRasterShader(gDevice, ludus::shaders::r11constant::Vertex(), vertex)) == false ||
        IsReady(rhi::CreateRasterShader(gDevice, fragment, shader)) == false ||
        IsReady(rhi::CreateBindingLayout(gDevice, fragment.Bindings, layout)) == false ||
        CreatePipeline(layout, vertex, shader, pipeline) == false)
    {
        return false;
    }
    rhi::RasterBindingResource resources[COLOR_COUNT + 2]{};
    resources[0] = { .Buffer = selection, .Size = sizeof(white) };
    resources[COLOR_COUNT + 1] = { .Binding = 2, .Sampler = sampler };
    for (usize color = 0; color < COLOR_COUNT; ++color)
    {
        uint8 pixel[4]{};
        for (usize channel = 0; channel < 4; ++channel)
        {
            pixel[channel] = static_cast<uint8>(COLORS[color][channel] * 255);
        }
        if (IsReady(rhi::CreateTexture(gDevice,
                                       { .Width = 1, .Height = 1 },
                                       { .Bytes = pixel, .RowPitch = 4 },
                                       textures[color])) == false ||
            IsReady(rhi::CreateTextureView(gDevice, textures[color], views[color])) == false)
        {
            return false;
        }
        resources[color + 1] = { .Binding = 1, .Texture = views[color], .ArrayElement = static_cast<uint32>(color) };
    }
    if (IsReady(rhi::CreateBindingSet(gDevice, layout, resources, set)) == false)
    {
        return false;
    }
    rhi::RasterBindingIndex reachable[COLOR_COUNT];
    for (usize color = 0; color < COLOR_COUNT; ++color)
    {
        if (IsReady(rhi::GetBindingIndex(gDevice, set, 1, static_cast<uint32>(color), reachable[color])) == false ||
            IsReady(rhi::Destroy(gDevice, views[color])) == false ||
            IsReady(rhi::Destroy(gDevice, textures[color])) == false)
        {
            return false;
        }
    }
    rhi::RasterDraw draw
    {
        .Pipeline = pipeline,
        .Bindings = set,
        .Vertices = {&gSlice, 1},
        .Indices = gIndices,
        .IndexCount = 6,
        .VertexCount = 4,
    };
    if (rhi::BeginFrame(gDevice, gSurface) != rhi::DeviceStatus::Ready ||
        IsReady(rhi::BeginRasterPass(gDevice, { .Color = gTarget })) == false)
    {
        return false;
    }
    if (isNonUniform == true)
    {
        if (rhi::DrawIndexed(gDevice, draw) != rhi::RasterStatus::InvalidDescription)
        {
            return false;
        }
        draw.ReachableBindings = reachable;
    }
    if (IsReady(rhi::DrawIndexed(gDevice, draw)) == false || rhi::EndFrame(gDevice) != rhi::DeviceStatus::Ready ||
        IsReady(rhi::Destroy(gDevice, set)) == false)
    {
        return false;
    }
    uint8 pixels[PIXEL_BYTES]{};
    if (ReadPixels(pixels) == false || CheckPixels(pixels) == false ||
        std::memcmp(pixels, gReferencePixels, sizeof(pixels)) != 0)
    {
        return false;
    }
    uint32 staleElement = 99;
    if (rhi::GetBindingArrayElement(gDevice, set, reachable[0], staleElement) != rhi::RasterStatus::InvalidHandle ||
        staleElement != 99)
    {
        return false;
    }
    return IsReady(rhi::Destroy(gDevice, vertex)) == true && IsReady(rhi::Destroy(gDevice, shader)) == true &&
           IsReady(rhi::Destroy(gDevice, pipeline)) == true && IsReady(rhi::Destroy(gDevice, layout)) == true &&
           IsReady(rhi::Destroy(gDevice, sampler)) == true && IsReady(rhi::Destroy(gDevice, selection)) == true;
}
#endif
bool CheckComputeOffsets() noexcept
{
    constexpr usize BUFFER_BYTES = 512;
    constexpr uint32 EXPECTED_VALUE = 0x12345678U;
    const uint8 bytes[BUFFER_BYTES]{};
    rhi::BufferHandle buffer;
    rhi::RasterShaderHandle shader;
    rhi::BindingLayoutHandle layout;
    rhi::BindingSetHandle set;
    rhi::ComputePipelineHandle pipeline;
    const rhi::RasterBinding binding
    {
        .Kind = rhi::RasterBindingKind::StorageReadWrite,
        .Visibility = rhi::RasterVisibility::Compute,
        .MinSize = 4,
        .HasDynamicOffset = true,
    };
    if (IsReady(rhi::CreateBuffer(gDevice,
                                  {
                                      .Role = rhi::BufferRole::Storage,
                                      .Size = sizeof(bytes),
                                  },
                                  bytes,
                                  buffer)) == false ||
        IsReady(rhi::CreateRasterShader(gDevice, ludus::shaders::r11compute::Compute(), shader)) == false ||
        IsReady(rhi::CreateBindingLayout(gDevice, {&binding, 1}, layout)) == false)
    {
        return false;
    }
    const rhi::RasterBindingResource resource{ .Buffer = buffer, .Size = 16 };
    if (IsReady(rhi::CreateBindingSet(gDevice, layout, {&resource, 1}, set)) == false ||
        IsReady(rhi::CreateComputePipeline(gDevice, { .Shader = shader, .Layout = layout }, pipeline)) == false)
    {
        return false;
    }
    rhi::OrderedGraph graph;
    rhi::GraphVersion input;
    rhi::GraphVersion output;
    if (IsReady(rhi::CreateOrderedGraph(gDevice, graph)) == false ||
        IsReady(rhi::ImportGraphBuffer(gDevice, graph, buffer, input)) == false ||
        IsReady(rhi::NextGraphVersion(gDevice, graph, input, output)) == false)
    {
        return false;
    }
    rhi::ComputeDispatch dispatch{ .Pipeline = pipeline, .Bindings = set };
    dispatch.BindingOffsets.Bytes[0] = static_cast<uint32>(UNIFORM_ALIGNMENT);
    const rhi::GraphUse use
    {
        .Resource = output,
        .Access = rhi::GraphAccessMode::StorageReadWrite,
        .Offset = UNIFORM_ALIGNMENT,
        .Size = 16,
        .Visibility = rhi::RasterVisibility::Compute,
    };
    const rhi::GraphPassDescription pass
    {
        .Name = "dynamic compute storage",
        .Attachment = {},
        .Uses = &use,
        .UseCount = 1,
        .SideEffect = true,
        .Dispatch = &dispatch,
    };
    rhi::GraphReport report;
    rhi::SubmissionToken submission;
    if (IsReady(rhi::AddGraphPass(gDevice, graph, pass)) == false ||
        IsReady(rhi::CompileOrderedGraph(gDevice, graph, report)) == false ||
        IsReady(rhi::ExecuteOrderedGraph(gDevice, gSurface, graph, submission)) == false)
    {
        return false;
    }
    rhi::ReadbackTicket readback;
    if (rhi::RequestBufferReadback(gDevice, buffer, 0, sizeof(bytes), readback) != rhi::RasterStatus::Pending)
    {
        return false;
    }
    uint8 actual[BUFFER_BYTES]{};
    rhi::RasterStatus status = rhi::RasterStatus::Pending;
    const uint64 started = time::NowTicks();
    while (status == rhi::RasterStatus::Pending && time::NowTicks() - started < 10 * time::NANOSECONDS_PER_SECOND)
    {
        status = rhi::CopyReadback(gDevice, readback, actual, sizeof(actual));
    }
    if (IsReady(status) == false || IsReady(rhi::Release(gDevice, readback)) == false)
    {
        return false;
    }
    for (usize offset = 0; offset < BUFFER_BYTES; offset += sizeof(uint32))
    {
        uint32 value = 0;
        std::memcpy(&value, actual + offset, sizeof(value));
        if (value != (offset == UNIFORM_ALIGNMENT ? EXPECTED_VALUE : 0U))
        {
            return false;
        }
    }
    return IsReady(rhi::Destroy(gDevice, pipeline)) == true && IsReady(rhi::Destroy(gDevice, set)) == true &&
           IsReady(rhi::Destroy(gDevice, layout)) == true && IsReady(rhi::Destroy(gDevice, shader)) == true &&
           IsReady(rhi::Destroy(gDevice, buffer)) == true;
}
bool Run() noexcept
{
    if (Prepare() == false || CheckComputeOffsets() == false || RenderDirect(false) == false ||
        ReadPixels(gReferencePixels) == false || CheckPixels(gReferencePixels) == false)
    {
        return false;
    }
    uint8 pixels[PIXEL_BYTES]{};
    if (RenderDirect(true) == false || ReadPixels(pixels) == false ||
        std::memcmp(pixels, gReferencePixels, sizeof(pixels)) != 0 || RenderGraph() == false ||
        ReadPixels(pixels) == false || std::memcmp(pixels, gReferencePixels, sizeof(pixels)) != 0)
    {
        return false;
    }
#if !defined(LUDUS_PLATFORM_WEB)
    rhi::DeviceInfo info;
    if (rhi::GetDeviceInfo(gDevice, info) != rhi::DeviceStatus::Ready ||
        RenderArrays(ludus::shaders::r11constant::Fragment(), false) == false)
    {
        return false;
    }
    if (info.Enabled.BindlessTextureIndexing == true)
    {
        if (RenderArrays(ludus::shaders::r11arrays::Fragment(), true) == false)
        {
            return false;
        }
    }
    else
    {
        LUDUS_LOG_INFO(LOG_R11, "Non-uniform array indexing unavailable; constant-array control qualified");
    }
#endif
    return true;
}
} // namespace
int main()
{
    (void)logging::LogSystem::Initialize({ .EnableFile = false, .Directory = {} });
    const rhi::WindowInfo window{ .Width = WIDTH, .Height = HEIGHT };
    const rhi::DeviceDescription description
    {
        .Limits = {},
        .Required = { .PortableRaster = true, .Compute = true },
        .Preferred = { .BindlessTextureIndexing = true },
        .Storage = { .ResourcesPerKind = 128, .BindingDescriptors = 512 },
        .RequiredProfile = {},
        .PreferredProfile = {},
    };
    if (rhi::CreateDevice({}, window, description, gDevice, gSurface) != rhi::DeviceStatus::Ready)
    {
        return 1;
    }
    const bool hasPassed = Run();
    rhi::Shutdown();
    (void)logging::LogSystem::Shutdown();
    return hasPassed == true ? 0 : 2;
}
