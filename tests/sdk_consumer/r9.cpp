#include <ludus/foundation/base/config.h>
#include <ludus/foundation/base/types.h>
#include <ludus/foundation/logging/log_format.hpp>
#include <ludus/foundation/time/time.hpp>
#include <ludus/graphics/rhi/graph.h>
#include <ludus/graphics/rhi/lifetime.h>

#include "r9.h"

#include <cstring>
#include <span>

#if defined(LUDUS_PLATFORM_WEB)
#    include <emscripten.h>
#    include <ludus/foundation/base/pointer.hpp>
#    include <ludus/platform/base/window.h>
#endif
namespace
{
using namespace ludus::foundation;
namespace rhi = ludus::graphics::rhi;
constexpr logging::LogCategory LOG_R9{"R9-Fixture"};
rhi::DeviceHandle gDevice;
rhi::SurfaceHandle gSurface;
rhi::TextureHandle gTextures[7], gTarget;
rhi::TextureViewHandle gViews[7];
rhi::BufferHandle gVertices, gIndices, gUniforms[14];
rhi::BindingSetHandle gSets[14];
rhi::SamplerHandle gSampler;
rhi::BindingLayoutHandle gLayout;
rhi::RasterShaderHandle gVertex, gFragment;
rhi::RasterPipelineHandle gPipeline;
rhi::OrderedGraph gGraph;
bool gGraphBuilt = false;
rhi::ReadbackTicket gRead;
rhi::TransferTicket gWrite;
uint32 gStage = 0, gFrames = 0, gCase = 0;
bool gDone = false, gFailed = false, gBC = false;
#if defined(LUDUS_PLATFORM_WEB)
core::UniquePtr<ludus::platform::Window> gWindow;
// Application conformance reporting, outside the engine API.
// clang-format off
EM_JS(int32, Selection, (), { const b = new URL(location.href).searchParams.get('backend'); return b === 'webgpu' ? 1 : b === 'webgl2' ? 2 : 0; });
EM_JS(int32, Paused, (), { return globalThis.__qaPause ? 1 : 0; });
EM_JS(void, Report, (uint32 stage, uint32 frames, int32 failed, int32 done), {
    const n = document.getElementById('status'); n.dataset.state = failed ? 'failed' : done ? 'passed' : frames ? 'rendering' : 'loading';
    n.dataset.stage = stage; n.dataset.frames = frames; n.textContent = n.dataset.state + ' stage=' + stage;
});
// clang-format on
#else
int32 Selection() noexcept
{
    return 0;
}
int32 Paused() noexcept
{
    return 0;
}
void Report(uint32, uint32, int32, int32) noexcept {}
#endif

void Stop(bool failed) noexcept
{
    if (failed)
    {
        LUDUS_LOG_ERROR(LOG_R9, "R9 fixture failed at stage {} case {}", gStage, gCase);
    }
    gFailed = failed;
    gDone = true;
    Report(gStage, gFrames, failed ? 1 : 0, 1);
    rhi::Shutdown();
#if defined(LUDUS_PLATFORM_WEB)
    emscripten_cancel_main_loop();
#endif
}
bool Accepted(rhi::RasterStatus value) noexcept
{
    return value == rhi::RasterStatus::Ready || value == rhi::RasterStatus::Pending;
}
template <typename T>
bool Complete(T handle) noexcept
{
    const auto status = rhi::GetStatus(gDevice, handle);
    if (status != rhi::RasterStatus::Ready && status != rhi::RasterStatus::Pending)
    {
        Stop(true);
    }
    return status == rhi::RasterStatus::Ready;
}
template <typename T, usize N>
std::span<const uint8> Bytes(const T (&values)[N]) noexcept
{
    return {reinterpret_cast<const uint8*>(values), sizeof(values)};
}
constexpr uint8 COLORS[6][4]{{255, 0, 0, 255},
                             {0, 255, 0, 255},
                             {0, 0, 255, 255},
                             {255, 255, 0, 255},
                             {255, 0, 255, 255},
                             {0, 255, 255, 255}};
void Fill(uint8* bytes, usize pixels, const uint8* color) noexcept
{
    for (usize i = 0; i < pixels; ++i)
    {
        std::memcpy(bytes + i * 4, color, 4);
    }
}
bool CreateResources() noexcept
{
    const float32 vertices[16]{-1, 1, 0, 0, -1, -1, 0, 1, 1, 1, 1, 0, 1, -1, 1, 1};
    const uint16 indices[6]{0, 1, 2, 2, 1, 3};
    if (!Accepted(
            rhi::CreateBuffer(gDevice, {rhi::BufferRole::Vertex, sizeof(vertices)}, Bytes(vertices), gVertices)) ||
        !Accepted(rhi::CreateBuffer(gDevice, {rhi::BufferRole::Index16, sizeof(indices)}, Bytes(indices), gIndices)) ||
        !Accepted(rhi::CreateTexture(gDevice,
                                     { .Width = 4, .Height = 4, .MipLevels = 3, .DeferredInitialization = true },
                                     {},
                                     gTextures[0])) ||
        !Accepted(rhi::CreateTexture(gDevice, { .Width = 8, .Height = 8, .Attachment = true }, {}, gTarget)))
    {
        return false;
    }
    uint8 bytes[384]{};
    for (uint32 slice = 0; slice < 3; ++slice)
    {
        Fill(bytes + usize{slice} * 64, 16, COLORS[slice]);
    }
    if (!Accepted(rhi::CreateTexture(
            gDevice,
            { .Width = 4, .Height = 4, .Dimension = rhi::TextureDimension::D2Array, .DepthOrLayers = 3 },
            {{bytes, 192}, 16},
            gTextures[1])))
    {
        return false;
    }
    for (uint32 slice = 0; slice < 6; ++slice)
    {
        for (uint32 y = 0; y < 4; ++y)
        {
            for (uint32 x = 0; x < 4; ++x)
            {
                auto* pixel = bytes + usize{slice} * 64 + usize{y} * 16 + usize{x} * 4;
                pixel[0] = static_cast<uint8>(31 + slice * 32);
                pixel[1] = x >= 2 ? 255 : 0;
                pixel[2] = y >= 2 ? 255 : 0;
                pixel[3] = 255;
            }
        }
    }
    if (!Accepted(rhi::CreateTexture(gDevice,
                                     {
                                         .Width = 4,
                                         .Height = 4,
                                         .Dimension = rhi::TextureDimension::Cube,
                                         .DepthOrLayers = 6,
                                     },
                                     {bytes, 16},
                                     gTextures[2])))
    {
        return false;
    }
    for (uint32 slice = 0; slice < 4; ++slice)
    {
        Fill(bytes + usize{slice} * 64, 16, COLORS[slice]);
    }
    Fill(bytes + 256, 4, COLORS[4]);
    Fill(bytes + 272, 4, COLORS[5]);
    Fill(bytes + 288, 1, COLORS[0]);
    const rhi::TextureMipUpload mips[3]{{0, 16}, {256, 8}, {288, 4}};
    if (!Accepted(rhi::CreateTexture(
            gDevice,
            { .Width = 4, .Height = 4, .MipLevels = 3, .Dimension = rhi::TextureDimension::D3, .DepthOrLayers = 4 },
            {{bytes, 292}, 0, mips},
            gTextures[3])))
    {
        return false;
    }
    uint16 hdr[64]{};
    for (usize i = 0; i < 16; ++i)
    {
        hdr[i * 4] = 0x3800;
        hdr[i * 4 + 1] = 0x3400;
        hdr[i * 4 + 2] = 0x3c00;
        hdr[i * 4 + 3] = 0x3c00;
    }
    if (!Accepted(rhi::CreateTexture(gDevice,
                                     { .Width = 4, .Height = 4, .Format = rhi::RasterFormat::Rgba16Float },
                                     rhi::TextureUpload{Bytes(hdr), 32},
                                     gTextures[4])))
    {
        return false;
    }
    rhi::FormatCapabilities bc1, bc3;
    gBC = rhi::GetFormatCapabilities(gDevice, rhi::RasterFormat::Bc1RgbaUnorm, bc1) == rhi::RasterStatus::Ready &&
          rhi::GetFormatCapabilities(gDevice, rhi::RasterFormat::Bc3RgbaUnorm, bc3) == rhi::RasterStatus::Ready;
    const uint8 block1[8]{0, 0xf8, 0, 0, 0, 0, 0, 0};
    const uint8 block3[16]{255, 0, 0, 0, 0, 0, 0, 0, 0xe0, 7, 0, 0, 0, 0, 0, 0};
    uint8 chain1[40]{}, chain3[80]{};
    for (usize block = 0; block < 5; ++block)
    {
        std::memcpy(chain1 + block * 8, block1, 8);
        std::memcpy(chain3 + block * 16, block3, 16);
    }
    const rhi::TextureMipUpload bc1Mips[4]{{0, 16}, {16, 8}, {24, 8}, {32, 8}};
    const rhi::TextureMipUpload bc3Mips[4]{{0, 32}, {32, 16}, {48, 16}, {64, 16}};
    const rhi::TextureMipUpload rgbaMips[4]{{0, 32}, {128, 16}, {160, 8}, {168, 4}};
    Fill(bytes, 43, COLORS[0]);
    if (!Accepted(rhi::CreateTexture(
            gDevice,
            {
                .Width = 8,
                .Height = 4,
                .Format = gBC ? rhi::RasterFormat::Bc1RgbaUnorm : rhi::RasterFormat::Rgba8Unorm,
                .MipLevels = 4,
            },
            gBC ? rhi::TextureUpload{chain1, 0, bc1Mips} : rhi::TextureUpload{{bytes, 172}, 0, rgbaMips},
            gTextures[5])))
    {
        return false;
    }
    Fill(bytes, 43, COLORS[1]);
    LUDUS_LOG_INFO(LOG_R9, "R9 BC profile enabled {}", gBC);
    return Accepted(
        rhi::CreateTexture(gDevice,
                           {
                               .Width = 8,
                               .Height = 4,
                               .Format = gBC ? rhi::RasterFormat::Bc3RgbaUnorm : rhi::RasterFormat::Rgba8Unorm,
                               .MipLevels = 4,
                           },
                           gBC ? rhi::TextureUpload{chain3, 0, bc3Mips} : rhi::TextureUpload{{bytes, 172}, 0, rgbaMips},
                           gTextures[6]));
}
bool CreateDraw() noexcept
{
    const auto vertex = ludus::shaders::r9::Vertex();
    const auto fragment = ludus::shaders::r9::Fragment();
    if (!Accepted(rhi::CreateRasterShader(gDevice, vertex, gVertex)) ||
        !Accepted(rhi::CreateRasterShader(gDevice, fragment, gFragment)) ||
        !Accepted(rhi::CreateBindingLayout(gDevice, fragment.Bindings, gLayout)) ||
        !Accepted(rhi::CreateSampler(gDevice, { .MipFilter = rhi::RasterMipFilter::Nearest }, gSampler)) ||
        !Accepted(rhi::CreateTextureView(gDevice, gTextures[0], { .BaseMip = 1, .MipCount = 1 }, gViews[0])))
    {
        return false;
    }
    for (usize i = 1; i < 7; ++i)
    {
        if (!Accepted(i >= 5 ? rhi::CreateTextureView(gDevice, gTextures[i], { .BaseMip = 2, .MipCount = 1 }, gViews[i])
                             : rhi::CreateTextureView(gDevice, gTextures[i], gViews[i])))
        {
            return false;
        }
    }
    rhi::TextureViewHandle forbidden;
    return rhi::CreateTextureView(gDevice, gTextures[0], forbidden) == rhi::RasterStatus::InvalidState;
}
bool CreateSets() noexcept
{
    for (uint32 test = 0; test < 14; ++test)
    {
        float32 settings[8]{};
        if (test == 1)
        {
            settings[0] = 1;
            settings[6] = 1;
        }
        else if (test >= 2 && test < 8)
        {
            settings[0] = 2;
            settings[4 + (test - 2) / 2] = (test % 2 == 0) ? 1.0F : -1.0F;
        }
        else if (test == 8)
        {
            settings[0] = 3;
            settings[6] = 0.375F;
        }
        else if (test == 9)
        {
            settings[0] = 3;
            settings[1] = 1;
            settings[6] = 0.75F;
        }
        else if (test == 10)
        {
            settings[0] = 3;
            settings[1] = 2;
            settings[6] = 0.5F;
        }
        else if (test == 11)
        {
            settings[0] = 4;
        }
        else if (test >= 12)
        {
            settings[0] = 5;
        }
        if (!Accepted(rhi::CreateBuffer(gDevice,
                                        {rhi::BufferRole::Uniform, sizeof(settings)},
                                        Bytes(settings),
                                        gUniforms[test])))
        {
            return false;
        }
    }
    return true;
}
bool Verify() noexcept
{
    uint8 bytes[256]{};
    if (rhi::CopyReadback(gDevice, gRead, bytes, sizeof(bytes)) != rhi::RasterStatus::Ready)
    {
        return false;
    }
    const uint8 hdr[4]{128, 64, 255, 255};
    const auto* expected = gCase == 0    ? COLORS[0]
                           : gCase == 1  ? COLORS[1]
                           : gCase < 8   ? COLORS[gCase - 2]
                           : gCase == 8  ? COLORS[1]
                           : gCase == 9  ? COLORS[5]
                           : gCase == 10 ? COLORS[0]
                           : gCase == 11 ? hdr
                           : gCase == 12 ? COLORS[0]
                                         : COLORS[1];
    for (usize i = 0; i < sizeof(bytes); ++i)
    {
        const usize channel = i % 4, x = (i / 4) % 8, y = i / 32;
        const uint8 cubeExpected = channel == 0   ? static_cast<uint8>(31 + (gCase - 2) * 32)
                                   : channel == 1 ? (x >= 4 ? 255 : 0)
                                   : channel == 2 ? (y >= 4 ? 255 : 0)
                                                  : 255;
        const auto value = gCase >= 2 && gCase < 8 ? cubeExpected : expected[channel];
        const int32 delta = static_cast<int32>(bytes[i]) - value;
        if (delta < -1 || delta > 1)
        {
            LUDUS_LOG_ERROR(LOG_R9, "R9 pixel byte {} actual {} expected {}", i, bytes[i], value);
            return false;
        }
    }
    return true;
}
rhi::RasterStatus DrawGraph() noexcept
{
    if (!gGraphBuilt)
    {
        if (rhi::CreateOrderedGraph(gDevice, gGraph) != rhi::RasterStatus::Ready)
        {
            return rhi::RasterStatus::Failed;
        }
        rhi::GraphVersion versions[11], written;
        const rhi::BufferHandle buffers[3]{gVertices, gIndices, gUniforms[0]};
        for (usize i = 0; i < 3; ++i)
        {
            if (rhi::ImportGraphBuffer(gDevice, gGraph, buffers[i], versions[i]) != rhi::RasterStatus::Ready)
            {
                return rhi::RasterStatus::Failed;
            }
        }
        for (usize i = 0; i < 8; ++i)
        {
            const auto texture = i == 7 ? gTarget : gTextures[i];
            rhi::RasterTextureState state;
            if (rhi::GetTextureState(gDevice, texture, state) != rhi::RasterStatus::Ready ||
                rhi::ImportGraphTexture(gDevice,
                                        gGraph,
                                        texture,
                                        state,
                                        rhi::RasterTextureUse::SampledBoth,
                                        versions[i + 3]) != rhi::RasterStatus::Ready)
            {
                return rhi::RasterStatus::Failed;
            }
        }
        if (rhi::NextGraphVersion(gDevice, gGraph, versions[10], written) != rhi::RasterStatus::Ready)
        {
            return rhi::RasterStatus::Failed;
        }
        rhi::GraphUse uses[11]{{versions[0], rhi::GraphAccessMode::Vertex, 0, 64},
                               {versions[1], rhi::GraphAccessMode::Index, 0, 12},
                               {versions[2], rhi::GraphAccessMode::Uniform, 0, 32}};
        for (usize i = 0; i < 7; ++i)
        {
            uses[i + 3] = {versions[i + 3], rhi::GraphAccessMode::Sampled};
        }
        uses[10] = {written, rhi::GraphAccessMode::ColorWrite};
        const rhi::RasterVertexSlice slice{gVertices, 0, 64};
        const rhi::RasterDraw draw{gPipeline, gSets[0], {&slice, 1}, gIndices, 0, 6, 4, 1};
        rhi::GraphPassDescription pass;
        pass.Name = "R9 initialized views";
        pass.Attachment.Color = gTarget;
        pass.Uses = uses;
        pass.UseCount = 11;
        pass.Draws = &draw;
        pass.DrawCount = 1;
        rhi::GraphReport report;
        if (rhi::AddGraphPass(gDevice, gGraph, pass) != rhi::RasterStatus::Ready ||
            rhi::AddGraphRoot(gDevice, gGraph, written, rhi::GraphRoot::Readback) != rhi::RasterStatus::Ready ||
            rhi::CompileOrderedGraph(gDevice, gGraph, report) != rhi::RasterStatus::Ready)
        {
            return rhi::RasterStatus::Failed;
        }
        for (auto& texture : gTextures)
        {
            if (rhi::Destroy(gDevice, texture) != rhi::RasterStatus::Ready)
            {
                return rhi::RasterStatus::Failed;
            }
        }
        gGraphBuilt = true;
    }
    rhi::SubmissionToken completion;
    return rhi::ExecuteOrderedGraph(gDevice, gSurface, gGraph, completion);
}
void Tick() noexcept
{
    if (gDone || Paused() != 0)
    {
        return;
    }
    rhi::DeviceInfo info;
    if (rhi::GetDeviceInfo(gDevice, info) != rhi::DeviceStatus::Ready)
    {
        if (info.Startup.State != rhi::StartupState::Pending)
        {
            Stop(true);
        }
        return;
    }
    if (rhi::PollLifetime(gDevice) != rhi::RasterStatus::Ready)
    {
        Stop(true);
        return;
    }
    if (gStage == 0)
    {
        if (!CreateResources())
        {
            Stop(true);
            return;
        }
        gStage = 1;
    }
    if (gStage == 1)
    {
        for (const auto texture : gTextures)
        {
            if (!Complete(texture))
            {
                return;
            }
        }
        if (!Complete(gTarget) || !Complete(gVertices) || !Complete(gIndices))
        {
            return;
        }
        uint8 red[16]{};
        Fill(red, 4, COLORS[0]);
        if (rhi::RequestTextureUpdate(gDevice, gTextures[0], {1, 0, 0, 2, 2}, red, 16, 8, gWrite) !=
            rhi::RasterStatus::Pending)
        {
            Stop(true);
            return;
        }
        gStage = 2;
    }
    if (gStage == 2)
    {
        if (!Complete(gWrite))
        {
            return;
        }
        if (rhi::Release(gDevice, gWrite) != rhi::RasterStatus::Ready || !CreateDraw() || !CreateSets())
        {
            Stop(true);
            return;
        }
        gStage = 3;
    }
    if (gStage == 3)
    {
        for (const auto view : gViews)
        {
            if (!Complete(view))
            {
                return;
            }
        }
        for (const auto uniform : gUniforms)
        {
            if (!Complete(uniform))
            {
                return;
            }
        }
        if (!Complete(gLayout) || !Complete(gVertex) || !Complete(gFragment) || !Complete(gSampler))
        {
            return;
        }
        for (uint32 test = 0; test < 14; ++test)
        {
            const rhi::RasterBindingResource resources[8]{{ .Buffer = gUniforms[test], .Size = 32 },
                                                          { .Binding = 1, .Texture = gViews[0] },
                                                          { .Binding = 2, .Sampler = gSampler },
                                                          { .Binding = 3, .Texture = gViews[1] },
                                                          { .Binding = 4, .Texture = gViews[2] },
                                                          { .Binding = 5, .Texture = gViews[3] },
                                                          { .Binding = 6, .Texture = gViews[4] },
                                                          { .Binding = 7, .Texture = gViews[test == 13 ? 6 : 5] }};
            if (!Accepted(rhi::CreateBindingSet(gDevice, gLayout, resources, gSets[test])))
            {
                Stop(true);
                return;
            }
        }
        const rhi::RasterVertexStream stream{16, false};
        const rhi::RasterVertexAttribute attributes[2]{{0, 0, 0, rhi::RasterVertexFormat::Float2},
                                                       {1, 0, 8, rhi::RasterVertexFormat::Float2}};
        rhi::RasterPipelineDescription pipeline;
        pipeline.Vertex = gVertex;
        pipeline.Fragment = gFragment;
        pipeline.Layout = gLayout;
        pipeline.Streams = {&stream, 1};
        pipeline.Attributes = attributes;
        pipeline.Depth = false;
        pipeline.Target = rhi::RasterTarget::Rgba8Unorm;
        if (!Accepted(rhi::CreateRasterPipeline(gDevice, pipeline, gPipeline)))
        {
            Stop(true);
            return;
        }
        gStage = 4;
    }
    if (gStage == 4)
    {
        if (!Complete(gPipeline) || !Complete(gSets[gCase]))
        {
            return;
        }
        if (rhi::SetFrameTarget(gDevice, gSurface, {96, 64, 0, 0, 0, 1}) != rhi::DeviceStatus::Ready)
        {
            Stop(true);
            return;
        }
        if (gCase == 0)
        {
            const auto status = DrawGraph();
            if (status == rhi::RasterStatus::NotReady)
            {
                return;
            }
            if (status != rhi::RasterStatus::Ready)
            {
                Stop(true);
                return;
            }
        }
        else
        {
            const auto begin = rhi::BeginFrame(gDevice, gSurface);
            if (begin == rhi::DeviceStatus::Skipped)
            {
                return;
            }
            const rhi::RasterVertexSlice slice{gVertices, 0, 64};
            if (begin != rhi::DeviceStatus::Ready ||
                rhi::BeginRasterPass(gDevice, { .Color = gTarget }) != rhi::RasterStatus::Ready ||
                rhi::DrawIndexed(gDevice, {gPipeline, gSets[gCase], {&slice, 1}, gIndices, 0, 6, 4, 1}) !=
                    rhi::RasterStatus::Ready ||
                rhi::EndFrame(gDevice) != rhi::DeviceStatus::Ready)
            {
                Stop(true);
                return;
            }
        }
        if (rhi::RequestTextureReadback(gDevice, gTarget, {0, 0, 0, 8, 8}, 32, gRead) != rhi::RasterStatus::Pending)
        {
            Stop(true);
            return;
        }
        gStage = 5;
    }
    if (gStage == 5)
    {
        if (!Complete(gRead))
        {
            return;
        }
        if (!Verify() || rhi::Release(gDevice, gRead) != rhi::RasterStatus::Ready)
        {
            Stop(true);
            return;
        }
        ++gCase;
        ++gFrames;
        Report(gStage, gFrames, 0, 0);
        if (gCase == 14)
        {
            Stop(false);
            return;
        }
        gStage = 4;
    }
}
} // namespace
int main()
{
    rhi::WindowInfo window;
    window.Width = 96;
    window.Height = 64;
#if defined(LUDUS_PLATFORM_WEB)
    ludus::platform::WindowManager manager;
    ludus::platform::WindowBase::CreateInfo description;
    description.Width = 96;
    description.Height = 64;
    description.CanvasSelector = "#canvas";
    description.CaptureBrowserInput = false;
    if (!manager.Initialize({}) || !manager.CreateWindow(description, gWindow))
    {
        return 1;
    }
    window = gWindow->GetNativeWindowInfo();
#endif
    rhi::DeviceDescription device;
    device.Required.PortableRaster = true;
    device.RequiredProfile.FormatCount = 1;
    device.RequiredProfile.Formats[0].Uses.CopySource = true;
    device.RequiredProfile.Formats[0].Uses.CopyDestination = true;
    device.Storage.TransferSlices = 2;
    device.Storage.TransferBytes = 4096;
    device.Selection = Selection() == 1   ? rhi::BackendSelection::WebGPU
                       : Selection() == 2 ? rhi::BackendSelection::WebGL2
                                          : rhi::BackendSelection::Auto;
    const auto created = rhi::CreateDevice({}, window, device, gDevice, gSurface);
    if (created != rhi::DeviceStatus::Ready && created != rhi::DeviceStatus::Pending)
    {
        return 1;
    }
#if defined(LUDUS_PLATFORM_WEB)
    emscripten_set_main_loop(Tick, 0, 1);
#else
    const auto started = time::NowTicks();
    while (!gDone && time::NowTicks() - started < 30 * time::NANOSECONDS_PER_SECOND)
    {
        Tick();
    }
    if (!gDone)
    {
        Stop(true);
    }
#endif
    return gFailed ? 1 : 0;
}
