#include <ludus/foundation/base/config.h>
#include <ludus/foundation/base/types.h>
#include <ludus/graphics/rhi/raster.h>

#include "raster.h"

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
rhi::DeviceHandle gDevice;
rhi::SurfaceHandle gSurface;
rhi::RasterShaderHandle gVertex, gFragment;
rhi::BindingLayoutHandle gLayout;
rhi::BindingSetHandle gSet, gBackgroundSet;
rhi::RasterPipelineHandle gPipeline;
rhi::BufferHandle gTint, gDim, gVertices, gBackground, gInstances, gCenter, gIndices;
rhi::TextureHandle gTexture;
rhi::TextureViewHandle gView;
rhi::SamplerHandle gSampler;
uint32 gStep = 0;
uint32 gFrames = 0;
bool gDone = false;
bool gFailed = false;
#if defined(LUDUS_PLATFORM_WEB)
core::UniquePtr<ludus::platform::Window> gWindow;
// Test reporting belongs to the application, outside the engine API.
// clang-format off
EM_JS(void, Report, (uint32 step, uint32 frames, int32 failed, int32 backend), {
    const node = document.getElementById('status');
    node.dataset.state = failed ? 'failed' : step === 4 ? 'rendering' : step === 5 ? 'passed' : 'loading';
    node.dataset.frames = frames;
    node.dataset.backend = backend === 3 ? 'metal' : backend === 1 ? 'webgpu' : backend === 2 ? 'webgl2' : 'vulkan';
    node.textContent = node.dataset.state + ' frames=' + frames;
});
EM_JS(int32, Paused, (), { return globalThis.__qaPause ? 1 : 0; });
EM_JS(int32, Selection, (), {
    const value = new URL(location.href).searchParams.get('backend');
    return value === 'webgpu' ? 1 : value === 'webgl2' ? 2 : 0;
});
// clang-format on
#else
void Report(uint32, uint32, int32, int32) noexcept {}
int32 Paused() noexcept
{
    return 0;
}
int32 Selection() noexcept
{
    return 0;
}
#endif
template <typename T, usize N>
std::span<const uint8> Bytes(const T (&values)[N]) noexcept
{
    return {reinterpret_cast<const uint8*>(values), sizeof(values)};
}
bool Accepted(rhi::RasterStatus result) noexcept
{
    return result == rhi::RasterStatus::Ready || result == rhi::RasterStatus::Pending;
}
template <typename T>
bool Ready(T value) noexcept
{
    const auto status = rhi::GetStatus(gDevice, value);
    if (status != rhi::RasterStatus::Ready && status != rhi::RasterStatus::Pending)
    {
        gFailed = true;
    }
    return status == rhi::RasterStatus::Ready;
}
void Stop(bool failed) noexcept
{
    gFailed = failed;
    gDone = true;
    gStep = 5;
    rhi::DeviceInfo info;
    (void)rhi::GetDeviceInfo(gDevice, info);
    (void)rhi::DestroyDevice(gDevice);
    Report(gStep, gFrames, failed ? 1 : 0, static_cast<int32>(info.Startup.SelectedBackend));
#if defined(LUDUS_PLATFORM_WEB)
    gWindow.Reset();
    emscripten_cancel_main_loop();
#endif
}
bool Create() noexcept
{
    const auto vertex = ludus::shaders::raster::Vertex();
    const auto fragment = ludus::shaders::raster::Fragment();
    const float32 tint[4]{1, 1, 1, 1};
    const float32 dim[4]{.5F, .5F, .5F, 1};
    const uint8 image[16]{255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
    const float32 points[20]{-.4F, -.75F, .25F, 0, 1, -.4F, .75F,  .25F, 0, 0,
                             .4F,  .75F,  .25F, 1, 0, .4F,  -.75F, .25F, 1, 1};
    const float32 background[20]{-.95F, -.95F, .75F, 0, 1, -.95F, .95F,  .75F, 0, 0,
                                 .95F,  .95F,  .75F, 1, 0, .95F,  -.95F, .75F, 1, 1};
    const float32 offsets[4]{-.45F, 0, .45F, 0};
    const float32 center[2]{0, 0};
    const uint32 indices[6]{0, 1, 2, 0, 2, 3};
    return Accepted(rhi::CreateRasterShader(gDevice, vertex, gVertex)) &&
           Accepted(rhi::CreateRasterShader(gDevice, fragment, gFragment)) &&
           Accepted(rhi::CreateBindingLayout(gDevice, fragment.Bindings, gLayout)) &&
           Accepted(rhi::CreateBuffer(gDevice, {rhi::BufferRole::Uniform, sizeof(tint)}, Bytes(tint), gTint)) &&
           Accepted(rhi::CreateBuffer(gDevice, {rhi::BufferRole::Uniform, sizeof(dim)}, Bytes(dim), gDim)) &&
           Accepted(rhi::CreateTexture(gDevice, {2, 2}, {image, 8}, gTexture)) &&
           Accepted(rhi::CreateSampler(gDevice, {}, gSampler)) &&
           Accepted(rhi::CreateBuffer(gDevice, {rhi::BufferRole::Vertex, sizeof(points)}, Bytes(points), gVertices)) &&
           Accepted(rhi::CreateBuffer(gDevice,
                                      {rhi::BufferRole::Vertex, sizeof(background)},
                                      Bytes(background),
                                      gBackground)) &&
           Accepted(
               rhi::CreateBuffer(gDevice, {rhi::BufferRole::Vertex, sizeof(offsets)}, Bytes(offsets), gInstances)) &&
           Accepted(rhi::CreateBuffer(gDevice, {rhi::BufferRole::Vertex, sizeof(center)}, Bytes(center), gCenter)) &&
           Accepted(rhi::CreateBuffer(gDevice, {rhi::BufferRole::Index32, sizeof(indices)}, Bytes(indices), gIndices));
}
void Tick() noexcept
{
    if (gDone || Paused() != 0)
    {
        return;
    }
    rhi::DeviceInfo info;
    const auto status = rhi::GetDeviceInfo(gDevice, info);
    if (status == rhi::DeviceStatus::Pending)
    {
        return;
    }
    if (status != rhi::DeviceStatus::Ready)
    {
        Stop(true);
        return;
    }
    if (gStep == 0)
    {
        if (!Create())
        {
            Stop(true);
            return;
        }
        gStep = 1;
    }
    if (gStep == 1)
    {
        const bool ready = Ready(gVertex) && Ready(gFragment) && Ready(gLayout) && Ready(gTint) && Ready(gDim) &&
                           Ready(gTexture) && Ready(gSampler) && Ready(gVertices) && Ready(gBackground) &&
                           Ready(gInstances) && Ready(gCenter) && Ready(gIndices);
        if (gFailed)
        {
            Stop(true);
            return;
        }
        if (!ready)
        {
            return;
        }
        if (!Accepted(rhi::CreateTextureView(gDevice, gTexture, gView)))
        {
            Stop(true);
            return;
        }
        gStep = 2;
    }
    if (gStep == 2)
    {
        if (!Ready(gView))
        {
            if (gFailed)
            {
                Stop(true);
            }
            return;
        }
        rhi::RasterBindingResource bindings[3]{};
        bindings[0].Buffer = gTint;
        bindings[0].Size = 16;
        bindings[1].Binding = 1;
        bindings[1].Texture = gView;
        bindings[2].Binding = 2;
        bindings[2].Sampler = gSampler;
        if (!Accepted(rhi::CreateBindingSet(gDevice, gLayout, bindings, gSet)))
        {
            Stop(true);
            return;
        }
        bindings[0].Buffer = gDim;
        if (!Accepted(rhi::CreateBindingSet(gDevice, gLayout, bindings, gBackgroundSet)))
        {
            Stop(true);
            return;
        }
        const rhi::RasterVertexStream streams[2]{{20, false}, {8, true}};
        const rhi::RasterVertexAttribute attributes[3]{{0, 0, 0, rhi::RasterVertexFormat::Float3},
                                                       {1, 0, 12, rhi::RasterVertexFormat::Float2},
                                                       {2, 1, 0, rhi::RasterVertexFormat::Float2}};
        if (!Accepted(rhi::CreateRasterPipeline(gDevice,
                                                {gVertex, gFragment, gLayout, streams, attributes, true},
                                                gPipeline)))
        {
            Stop(true);
            return;
        }
        gStep = 3;
    }
    if (gStep == 3)
    {
        const bool ready = Ready(gSet) && Ready(gBackgroundSet) && Ready(gPipeline);
        if (gFailed)
        {
            Stop(true);
            return;
        }
        if (!ready)
        {
            return;
        }
        if (rhi::Destroy(gDevice, gTint) != rhi::RasterStatus::Ready ||
            rhi::Destroy(gDevice, gDim) != rhi::RasterStatus::Ready ||
            rhi::Destroy(gDevice, gTexture) != rhi::RasterStatus::Ready ||
            rhi::Destroy(gDevice, gView) != rhi::RasterStatus::Ready ||
            rhi::Destroy(gDevice, gSampler) != rhi::RasterStatus::Ready ||
            rhi::Destroy(gDevice, gVertex) != rhi::RasterStatus::Ready ||
            rhi::Destroy(gDevice, gFragment) != rhi::RasterStatus::Ready ||
            rhi::Destroy(gDevice, gLayout) != rhi::RasterStatus::Ready)
        {
            Stop(true);
            return;
        }
        gStep = 4;
    }
    if (rhi::SetFrameTarget(gDevice, gSurface, {96, 64, 0, 0, 0, 1}) != rhi::DeviceStatus::Ready)
    {
        Stop(true);
        return;
    }
    const auto acquired = rhi::BeginFrame(gDevice, gSurface);
    if (acquired == rhi::DeviceStatus::Skipped)
    {
        return;
    }
    if (acquired != rhi::DeviceStatus::Ready)
    {
        Stop(true);
        return;
    }
    const rhi::RasterVertexSlice slices[2]{{gVertices, 0, 80}, {gInstances, 0, 16}};
    const rhi::RasterVertexSlice back[2]{{gBackground, 0, 80}, {gCenter, 0, 8}};
    const auto foreground = rhi::DrawIndexed(gDevice, {gPipeline, gSet, slices, gIndices, 0, 6, 4, 2});
    const auto background = foreground == rhi::RasterStatus::Ready
                                ? rhi::DrawIndexed(gDevice, {gPipeline, gBackgroundSet, back, gIndices, 0, 6, 4, 1})
                                : foreground;
    const auto ended = rhi::EndFrame(gDevice);
    if (foreground == rhi::RasterStatus::CapacityExceeded)
    {
        return;
    }
    if (foreground != rhi::RasterStatus::Ready || background != rhi::RasterStatus::Ready ||
        (ended != rhi::DeviceStatus::Ready && ended != rhi::DeviceStatus::Skipped))
    {
        Stop(true);
        return;
    }
    ++gFrames;
    Report(gStep, gFrames, 0, static_cast<int32>(info.Startup.SelectedBackend));
    if (gFrames == 120)
    {
        Stop(false);
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
    ludus::platform::WindowBase::CreateInfo windowDescription;
    windowDescription.Width = 96;
    windowDescription.Height = 64;
    windowDescription.CanvasSelector = "#canvas";
    windowDescription.CaptureBrowserInput = false;
    if (!manager.Initialize({}) || !manager.CreateWindow(windowDescription, gWindow))
    {
        Stop(true);
        return 1;
    }
    window = gWindow->GetNativeWindowInfo();
#endif
    rhi::DeviceDescription description;
    description.Required.PortableRaster = true;
    description.Selection = Selection() == 1   ? rhi::BackendSelection::WebGPU
                            : Selection() == 2 ? rhi::BackendSelection::WebGL2
                                               : rhi::BackendSelection::Auto;
    rhi::StartupInfo failure;
    const auto started = rhi::CreateDevice({}, window, description, gDevice, gSurface, &failure);
    if (started != rhi::DeviceStatus::Ready && started != rhi::DeviceStatus::Pending)
    {
#if !defined(LUDUS_PLATFORM_WEB)
        if (failure.Error == rhi::StartupError::AdapterUnavailable)
        {
            return 0;
        }
#endif
        Stop(true);
        return 1;
    }
#if defined(LUDUS_PLATFORM_WEB)
    emscripten_set_main_loop(Tick, 0, 1);
#else
    for (usize i = 0; i < 500 && !gDone; ++i)
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
