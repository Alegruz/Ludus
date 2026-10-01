#include "internal/renderer.h"
#include "internal/webgpu_probe.h"
#include <ludus/foundation/base/core.h>
#include <ludus/foundation/logging/log_format.hpp>
namespace ludus::smoke::renderer
{
namespace
{
using namespace foundation;
namespace rhi = graphics::rhi;
WGPURenderPipeline gPipeline = nullptr;
uint32 gGeneration = 0;
bool gBuilding = false;
bool gReady = false;
bool gDemoFailed = false;
// WGSL and graphics resources belong to this sample, not the engine.
constexpr const char* SHADER = R"(
@vertex fn vertex(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
    let points = array(vec2f(0.0, 0.7), vec2f(-0.7, -0.7), vec2f(0.7, -0.7));
    return vec4f(points[i], 0.0, 1.0);
}
@fragment fn fragment() -> @location(0) vec4f { return vec4f(1.0, 0.65, 0.15, 1.0); }
)";
void ReleaseDemo() noexcept
{
    // Never reuse a callback generation within a running page.
    if (gGeneration != 0xFFFFFFFFU)
    {
        ++gGeneration;
    }
    if (gPipeline != nullptr)
    {
        wgpuRenderPipelineRelease(gPipeline);
    }
    gPipeline = nullptr;
    gReady = false;
    gBuilding = false;
}
void BuildDemo() noexcept
{
    gBuilding = true;
    const auto device = rhi::backend::ProbeDevice();
    wgpuDevicePushErrorScope(device, WGPUErrorFilter_Validation);
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = { .data = SHADER, .length = WGPU_STRLEN };
    WGPUShaderModuleDescriptor shaderDescriptor = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    shaderDescriptor.nextInChain = &source.chain;
    WGPUShaderModule shader = wgpuDeviceCreateShaderModule(device, &shaderDescriptor);
    WGPUColorTargetState color = WGPU_COLOR_TARGET_STATE_INIT;
    color.format = rhi::backend::ProbeFormat();
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = shader;
    fragment.entryPoint = { .data = "fragment", .length = WGPU_STRLEN };
    fragment.targetCount = 1;
    fragment.targets = &color;
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.vertex.module = shader;
    descriptor.vertex.entryPoint = { .data = "vertex", .length = WGPU_STRLEN };
    descriptor.fragment = &fragment;
    descriptor.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    if (shader != nullptr)
    {
        gPipeline = wgpuDeviceCreateRenderPipeline(device, &descriptor);
    }
    if (shader != nullptr)
    {
        wgpuShaderModuleRelease(shader);
    }
    WGPUPopErrorScopeCallbackInfo callback = WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT;
    callback.mode = WGPUCallbackMode_AllowSpontaneous;
    // Opaque generation, never dereferenced by the foreign callback.
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    callback.userdata1 = reinterpret_cast<void*>(static_cast<usize>(gGeneration));
    callback.callback =
        [](WGPUPopErrorScopeStatus status, WGPUErrorType error, WGPUStringView, void* data, void*) noexcept {
            if (static_cast<uint32>(reinterpret_cast<usize>(data)) != gGeneration ||
                rhi::GetStartup().State != rhi::StartupState::Ready)
            {
                return;
            }
            if (status != WGPUPopErrorScopeStatus_Success || error != WGPUErrorType_NoError || gPipeline == nullptr)
            {
                LUDUS_LOG_WARN(logging::LOG_TEMP, "Smoke WGSL/pipeline validation failed");
                gDemoFailed = true;
                ReleaseDemo();
                return;
            }
            gReady = true;
        };
    (void)wgpuDevicePopErrorScope(device, callback);
}

} // namespace
State Prepare() noexcept
{
    if (gGeneration == 0xFFFFFFFFU || gDemoFailed)
    {
        return State::Failed;
    }
    if (!gBuilding)
    {
        BuildDemo();
    }
    return gReady ? State::Ready : State::Loading;
}
void Shutdown() noexcept
{
    ReleaseDemo();
    gDemoFailed = false;
}
graphics::rhi::FrameStatus Render(const platform::browser::WindowState& window, const Simulation& simulation) noexcept
{
    const float64 pulse = simulation.Phase < 1 ? simulation.Phase : 2 - simulation.Phase;
    const auto target = rhi::SetFrameTarget(
    {
        .Width = window.FramebufferWidth,
        .Height = window.FramebufferHeight,
        .Red = 0.04 + pulse * 0.06,
        .Green = 0.06,
        .Blue = 0.14 + pulse * 0.08,
    });
    if (target != rhi::FrameStatus::Ready)
    {
        return target;
    }
    const auto begun = rhi::BeginFrameStatus();
    if (begun != rhi::FrameStatus::Ready)
    {
        return begun;
    }
    const auto pass = rhi::backend::ProbePass();
    const uint32 minimum =
        window.FramebufferWidth < window.FramebufferHeight ? window.FramebufferWidth : window.FramebufferHeight;
    const float32 size = static_cast<float32>(minimum) * 0.5F;
    const float32 travelX = (static_cast<float32>(window.FramebufferWidth) - size) * 0.5F;
    const float32 travelY = (static_cast<float32>(window.FramebufferHeight) - size) * 0.5F;
    wgpuRenderPassEncoderSetViewport(pass,
                                     travelX * static_cast<float32>(1 + simulation.X),
                                     travelY * static_cast<float32>(1 - simulation.Y),
                                     size,
                                     size,
                                     0,
                                     1);
    wgpuRenderPassEncoderSetPipeline(pass, gPipeline);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    return rhi::EndFrameStatus();
}
} // namespace ludus::smoke::renderer
