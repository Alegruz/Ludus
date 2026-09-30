#include <ludus/foundation/base/types.h>

#include <charconv>
#include <cmath>
#include <expected>
#include <format>
#include <string>
#include <system_error>

#include <emscripten.h>
#include <emscripten/html5.h>
#include <webgpu/webgpu.h>

using namespace ludus::foundation;

namespace
{
// This probe deliberately links no engine runtime. Handles outlive asynchronous
// requests and animation callbacks; a reload destroys the browser's module.
struct Probe final
{
    WGPUInstance Instance = nullptr;
    WGPUAdapter Adapter = nullptr;
    WGPUDevice Device = nullptr;
    WGPUQueue Queue = nullptr;
    WGPUSurface Surface = nullptr;
    bool Failed = false;
    bool CompatibilityRequested = false;
    uint32 Frames = 0;
};

Probe gProbe;

// clang-format off
EM_JS(void, showStatus, (const char* state, const char* message), {
    const status = document.getElementById('status');
    status.dataset.state = UTF8ToString(state);
    status.textContent = UTF8ToString(message);
});

EM_JS(void, reportFrame, (uint32 frames), {
    document.getElementById('status').dataset.frames = String(frames);
});
EM_JS(void, appendDiagnostic, (const char* label, const char* message, usize length), {
    document.getElementById('diagnostics').textContent +=
        UTF8ToString(label) + ': ' + (message && length ? UTF8ToString(message, length) : '(no message)') + '\n';
});
// clang-format on

void diagnose(const char* label, WGPUStringView message) noexcept
{
    // The API permits both explicit lengths and null-terminated views.
    // Bound display output without requiring allocation or trusting termination.
    constexpr usize maxLength = 4096;
    const usize length = message.length < maxLength ? message.length : maxLength;
    appendDiagnostic(label, message.data, length);
}

void fail(const char* message) noexcept
{
    gProbe.Failed = true;
    showStatus("failed", message);
}

[[nodiscard]] std::expected<std::string, std::errc> checkLibraryFeatures() noexcept
{
    char buffer[32] = {};
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), float64{1.25});
    if (result.ec != std::errc{})
    {
        return std::unexpected(result.ec);
    }
    const std::string converted(buffer, result.ptr);
    if (converted != "1.25" || std::format("{:.2f}", float64{1.25}) != converted)
    {
        return std::unexpected(std::errc::invalid_argument);
    }
    return std::format("C++23 library checks passed ({}); requesting a WebGPU adapter…", converted);
}

EM_BOOL renderFrame(float64 time, void*) noexcept
{
    if (gProbe.Failed)
    {
        return EM_FALSE;
    }

    WGPUSurfaceTexture surfaceTexture = WGPU_SURFACE_TEXTURE_INIT;
    wgpuSurfaceGetCurrentTexture(gProbe.Surface, &surfaceTexture);
    if (!surfaceTexture.texture || (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
                                    surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal))
    {
        if (surfaceTexture.texture)
        {
            wgpuTextureRelease(surfaceTexture.texture);
        }
        fail("WebGPU could not acquire a canvas frame. Reload to retry.");
        return EM_FALSE;
    }

    WGPUTextureView view = wgpuTextureCreateView(surfaceTexture.texture, nullptr);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gProbe.Device, nullptr);
    if (!view || !encoder)
    {
        if (view)
        {
            wgpuTextureViewRelease(view);
        }
        if (encoder)
        {
            wgpuCommandEncoderRelease(encoder);
        }
        wgpuTextureRelease(surfaceTexture.texture);
        fail("WebGPU could not create frame resources.");
        return EM_FALSE;
    }

    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color.view = view;
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Store;
    color.clearValue.r = 0.25 + 0.2 * std::sin(time * 0.001);
    color.clearValue.g = 0.12;
    color.clearValue.b = 0.45;
    color.clearValue.a = 1.0;

    WGPURenderPassDescriptor passDescriptor = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDescriptor.colorAttachmentCount = 1;
    passDescriptor.colorAttachments = &color;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDescriptor);
    if (pass)
    {
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder, nullptr);
        if (command)
        {
            wgpuQueueSubmit(gProbe.Queue, 1, &command);
            wgpuCommandBufferRelease(command);
            reportFrame(++gProbe.Frames);
        }
        else
        {
            fail("WebGPU could not finish frame commands.");
        }
    }
    else
    {
        fail("WebGPU could not begin the clear pass.");
    }
    wgpuCommandEncoderRelease(encoder);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(surfaceTexture.texture);
    // Browser WebGPU presents after the animation callback returns. Calling
    // wgpuSurfacePresent here aborts in the pinned Emdawnwebgpu implementation.
    return gProbe.Failed ? EM_FALSE : EM_TRUE;
}

void deviceReady(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void*, void*) noexcept
{
    diagnose("Device request", message);
    if (status != WGPURequestDeviceStatus_Success || !device)
    {
        fail("WebGPU device request failed. Check browser/GPU support and reload.");
        return;
    }
    gProbe.Device = device;
    gProbe.Queue = wgpuDeviceGetQueue(device);

    WGPUSurfaceCapabilities capabilities = WGPU_SURFACE_CAPABILITIES_INIT;
    if (wgpuSurfaceGetCapabilities(gProbe.Surface, gProbe.Adapter, &capabilities) != WGPUStatus_Success ||
        capabilities.formatCount == 0 || !gProbe.Queue)
    {
        wgpuSurfaceCapabilitiesFreeMembers(capabilities);
        fail("WebGPU canvas capabilities are unavailable.");
        return;
    }
    WGPUSurfaceConfiguration configuration = WGPU_SURFACE_CONFIGURATION_INIT;
    configuration.device = device;
    configuration.format = capabilities.formats[0];
    configuration.width = 640;
    configuration.height = 360;
    configuration.presentMode = WGPUPresentMode_Fifo;
    wgpuSurfaceConfigure(gProbe.Surface, &configuration);
    wgpuSurfaceCapabilitiesFreeMembers(capabilities);
    if (!gProbe.Failed)
    {
        showStatus("ready",
                   gProbe.CompatibilityRequested
                       ? "WebGPU is ready (compatibility request). The canvas color should animate."
                       : "WebGPU is ready (core request). The canvas color should animate.");
        emscripten_request_animation_frame_loop(renderFrame, nullptr);
    }
}

void adapterReady(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void*, void*) noexcept;

void requestAdapter(WGPUFeatureLevel featureLevel) noexcept
{
    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    options.featureLevel = featureLevel;
    options.compatibleSurface = gProbe.Surface;
    WGPURequestAdapterCallbackInfo callback = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    callback.mode = WGPUCallbackMode_AllowSpontaneous;
    callback.callback = adapterReady;
    (void)wgpuInstanceRequestAdapter(gProbe.Instance, &options, callback);
}

void adapterReady(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void*, void*) noexcept
{
    const auto label = std::format("{} adapter request (status {})",
                                   gProbe.CompatibilityRequested ? "Compatibility" : "Core",
                                   static_cast<uint32>(status));
    diagnose(label.c_str(), message);
    if (status != WGPURequestAdapterStatus_Success || !adapter)
    {
        if (adapter)
        {
            wgpuAdapterRelease(adapter);
        }
        if (!gProbe.CompatibilityRequested)
        {
            gProbe.CompatibilityRequested = true;
            showStatus("initializing", "Core adapter unavailable; trying WebGPU compatibility mode…");
            requestAdapter(WGPUFeatureLevel_Compatibility);
            return;
        }
        fail("No WebGPU adapter is available in core or compatibility mode. See diagnostics below.");
        return;
    }
    gProbe.Adapter = adapter;
    showStatus("initializing", "WebGPU adapter acquired; requesting a device…");
    WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    descriptor.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    descriptor.deviceLostCallbackInfo.callback =
        [](WGPUDevice const*, WGPUDeviceLostReason, WGPUStringView message, void*, void*) noexcept {
            diagnose("Device lost", message);
            fail("WebGPU device lost. Reload to retry.");
        };
    descriptor.uncapturedErrorCallbackInfo.callback =
        [](WGPUDevice const*, WGPUErrorType, WGPUStringView message, void*, void*) noexcept {
            diagnose("Uncaptured error", message);
            fail("WebGPU reported a validation error. Reload to retry.");
        };
    WGPURequestDeviceCallbackInfo callback = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
    callback.mode = WGPUCallbackMode_AllowSpontaneous;
    callback.callback = deviceReady;
    (void)wgpuAdapterRequestDevice(adapter, &descriptor, callback);
}
} // namespace

int main()
{
    const auto featureCheck = checkLibraryFeatures();
    if (!featureCheck)
    {
        fail("C++23 formatting/conversion checks failed.");
        return 1;
    }
    showStatus("initializing", featureCheck->c_str());
    gProbe.Instance = wgpuCreateInstance(nullptr);
    if (!gProbe.Instance)
    {
        fail("WebGPU instance creation failed.");
        return 1;
    }
    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvas = WGPU_EMSCRIPTEN_SURFACE_SOURCE_CANVAS_HTML_SELECTOR_INIT;
    canvas.selector = { .data = "#canvas", .length = WGPU_STRLEN };
    WGPUSurfaceDescriptor descriptor = WGPU_SURFACE_DESCRIPTOR_INIT;
    descriptor.nextInChain = &canvas.chain;
    gProbe.Surface = wgpuInstanceCreateSurface(gProbe.Instance, &descriptor);
    if (!gProbe.Surface)
    {
        fail("WebGPU canvas surface creation failed.");
        return 1;
    }
    requestAdapter(WGPUFeatureLevel_Core);
    // Emdawnwebgpu keeps pending callbacks alive after main returns.
    return 0;
}
