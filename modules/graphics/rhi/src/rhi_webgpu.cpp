#include "internal/backend.h"
#include "internal/lifecycle.h"
#include <ludus/foundation/base/core.h>
#include <ludus/foundation/logging/log_format.hpp>
#include <ludus/graphics/rhi/rhi.h>

#include <string_view>

#include <emscripten.h>
#include <webgpu/webgpu.h>

namespace ludus::graphics::rhi::backend
{
namespace
{
using namespace ludus::foundation;
constexpr logging::LogCategory LOG_RHI{"RHI"};
WGPUInstance gInstance = nullptr;
WGPUAdapter gAdapter = nullptr;
WGPUDevice gDevice = nullptr;
WGPUQueue gQueue = nullptr;
WGPUSurface gSurface = nullptr;
bool gConfigured = false;
bool gCompatibility = false;
uint32 gMaxDimension = 0;

// Validate the foreign target before the port's assertion-based surface API.
// No callback borrows this selector or application storage.
// clang-format off
EM_JS(int32, CanCreateSurface, (const char* selector), {
    try {
        if (typeof navigator.gpu == 'undefined') return -1;
        const canvas = document.querySelector(UTF8ToString(selector));
        return canvas instanceof HTMLCanvasElement &&
            canvas.isConnected && !!canvas.getContext('webgpu') ? 1 : 0;
    } catch (_) { return 0; }
});
// clang-format on

void* Userdata(uint32 token) noexcept
{
    // C API userdata carries an opaque numeric generation, never a memory address.
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    return reinterpret_cast<void*>(static_cast<usize>(token));
}
uint32 Token(void* userdata) noexcept
{
    return static_cast<uint32>(reinterpret_cast<usize>(userdata));
}
void Diagnose(WGPUStringView message) noexcept
{
    if (message.data != nullptr)
    {
        usize length = message.length < 2048 ? message.length : 2048;
        if (message.length == WGPU_STRLEN)
        {
            length = 0;
            while (length < 2048 && message.data[length] != '\0')
            {
                ++length;
            }
        }
        LUDUS_LOG_WARN(LOG_RHI, "WebGPU: {}", std::string_view(message.data, length));
    }
}
void Configured(WGPUPopErrorScopeStatus status,
                WGPUErrorType error,
                WGPUStringView message,
                void* userdata,
                void*) noexcept
{
    const uint32 token = Token(userdata);
    if (!internal::Current(token))
    {
        return;
    }
    if (status != WGPUPopErrorScopeStatus_Success || error != WGPUErrorType_NoError)
    {
        Diagnose(message);
        internal::Fail(token, StartupError::SurfaceUnavailable);
        return;
    }
    internal::Complete(token, StartupError::None, gMaxDimension);
}
void DeviceReady(WGPURequestDeviceStatus status,
                 WGPUDevice device,
                 WGPUStringView message,
                 void* userdata,
                 void*) noexcept
{
    const uint32 token = Token(userdata);
    if (!internal::Current(token))
    {
        if (device != nullptr)
        {
            wgpuDeviceDestroy(device);
            wgpuDeviceRelease(device);
        }
        return;
    }
    if (status != WGPURequestDeviceStatus_Success || device == nullptr)
    {
        if (device != nullptr)
        {
            wgpuDeviceDestroy(device);
            wgpuDeviceRelease(device);
        }
        Diagnose(message);
        internal::Fail(token, StartupError::DeviceUnavailable);
        return;
    }
    gDevice = device;
    gQueue = wgpuDeviceGetQueue(device);
    WGPULimits limits = WGPU_LIMITS_INIT;
    WGPUSurfaceCapabilities capabilities = WGPU_SURFACE_CAPABILITIES_INIT;
    if (wgpuDeviceGetLimits(device, &limits) != WGPUStatus_Success ||
        wgpuSurfaceGetCapabilities(gSurface, gAdapter, &capabilities) != WGPUStatus_Success ||
        capabilities.formatCount == 0 || gQueue == nullptr)
    {
        wgpuSurfaceCapabilitiesFreeMembers(capabilities);
        internal::Fail(token, StartupError::SurfaceUnavailable);
        return;
    }
    if (limits.maxTextureDimension2D == 0)
    {
        wgpuSurfaceCapabilitiesFreeMembers(capabilities);
        internal::Fail(token, StartupError::DeviceUnavailable);
        return;
    }
    gMaxDimension = limits.maxTextureDimension2D;
    WGPUSurfaceConfiguration configuration = WGPU_SURFACE_CONFIGURATION_INIT;
    configuration.device = device;
    configuration.format = capabilities.formats[0];
    // W4 configures a minimal surface; W5 owns resize and per-frame textures.
    configuration.width = 1;
    configuration.height = 1;
    configuration.presentMode = WGPUPresentMode_Fifo;
    wgpuDevicePushErrorScope(device, WGPUErrorFilter_Validation);
    wgpuSurfaceConfigure(gSurface, &configuration);
    gConfigured = true;
    wgpuSurfaceCapabilitiesFreeMembers(capabilities);
    WGPUPopErrorScopeCallbackInfo callback = WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT;
    callback.mode = WGPUCallbackMode_AllowSpontaneous;
    callback.callback = Configured;
    callback.userdata1 = Userdata(token);
    (void)wgpuDevicePopErrorScope(device, callback);
}
void AdapterReady(WGPURequestAdapterStatus status,
                  WGPUAdapter adapter,
                  WGPUStringView message,
                  void* userdata,
                  void*) noexcept;
void RequestAdapter(uint32 token) noexcept
{
    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    options.compatibleSurface = gSurface;
    options.featureLevel = gCompatibility ? WGPUFeatureLevel_Compatibility : WGPUFeatureLevel_Core;
    WGPURequestAdapterCallbackInfo callback = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    callback.mode = WGPUCallbackMode_AllowSpontaneous;
    callback.callback = AdapterReady;
    callback.userdata1 = Userdata(token);
    (void)wgpuInstanceRequestAdapter(gInstance, &options, callback);
}
void AdapterReady(WGPURequestAdapterStatus status,
                  WGPUAdapter adapter,
                  WGPUStringView message,
                  void* userdata,
                  void*) noexcept
{
    const uint32 token = Token(userdata);
    if (!internal::Current(token))
    {
        if (adapter != nullptr)
        {
            wgpuAdapterRelease(adapter);
        }
        return;
    }
    if (status != WGPURequestAdapterStatus_Success || adapter == nullptr)
    {
        if (adapter != nullptr)
        {
            wgpuAdapterRelease(adapter);
        }
        if (!gCompatibility)
        {
            gCompatibility = true;
            RequestAdapter(token);
            return;
        }
        Diagnose(message);
        internal::Fail(token, StartupError::AdapterUnavailable);
        return;
    }
    gAdapter = adapter;
    WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    // No optional features or elevated limits: negotiate browser defaults and
    // publish actual device dimensions after a validated surface configuration.
    descriptor.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    descriptor.deviceLostCallbackInfo.userdata1 = Userdata(token);
    descriptor.deviceLostCallbackInfo.callback =
        [](WGPUDevice const*, WGPUDeviceLostReason reason, WGPUStringView lostMessage, void* lostData, void*) noexcept {
            if (internal::Current(Token(lostData)))
            {
                Diagnose(lostMessage);
                internal::Fail(Token(lostData),
                               reason == WGPUDeviceLostReason_FailedCreation ? StartupError::DeviceUnavailable
                                                                             : StartupError::DeviceLost);
            }
        };
    descriptor.uncapturedErrorCallbackInfo.userdata1 = Userdata(token);
    descriptor.uncapturedErrorCallbackInfo.callback =
        [](WGPUDevice const*, WGPUErrorType, WGPUStringView errorMessage, void* errorData, void*) noexcept {
            if (internal::Current(Token(errorData)))
            {
                Diagnose(errorMessage);
                internal::Fail(Token(errorData), StartupError::Validation);
            }
        };
    WGPURequestDeviceCallbackInfo callback = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
    callback.mode = WGPUCallbackMode_AllowSpontaneous;
    callback.callback = DeviceReady;
    callback.userdata1 = Userdata(token);
    (void)wgpuAdapterRequestDevice(adapter, &descriptor, callback);
}
} // namespace
Backend Kind() noexcept
{
    return Backend::WebGPU;
}
StartupError Start(const ApplicationInfo&, const WindowInfo& window, uint32 token) noexcept
{
    if (window.System != ludus::platform::WindowSystem::WebCanvas || window.CanvasSelector == nullptr)
    {
        return StartupError::InvalidWindow;
    }
    const auto available = CanCreateSurface(window.CanvasSelector);
    if (available < 0)
    {
        return StartupError::AdapterUnavailable;
    }
    if (available == 0)
    {
        return StartupError::InvalidWindow;
    }
    gInstance = wgpuCreateInstance(nullptr);
    if (gInstance == nullptr)
    {
        return StartupError::InstanceUnavailable;
    }
    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector source = WGPU_EMSCRIPTEN_SURFACE_SOURCE_CANVAS_HTML_SELECTOR_INIT;
    source.selector = { .data = window.CanvasSelector, .length = WGPU_STRLEN };
    WGPUSurfaceDescriptor descriptor = WGPU_SURFACE_DESCRIPTOR_INIT;
    descriptor.nextInChain = &source.chain;
    gSurface = wgpuInstanceCreateSurface(gInstance, &descriptor);
    if (gSurface == nullptr)
    {
        return StartupError::SurfaceUnavailable;
    }
    gCompatibility = false;
    RequestAdapter(token);
    return StartupError::None;
}
void Shutdown() noexcept
{
    // The facade invalidates callback tokens before this can trigger device loss.
    if (gConfigured && gSurface != nullptr)
    {
        wgpuSurfaceUnconfigure(gSurface);
    }
    gConfigured = false;
    if (gQueue != nullptr)
    {
        wgpuQueueRelease(gQueue);
    }
    if (gDevice != nullptr)
    {
        wgpuDeviceDestroy(gDevice);
        wgpuDeviceRelease(gDevice);
    }
    if (gAdapter != nullptr)
    {
        wgpuAdapterRelease(gAdapter);
    }
    if (gSurface != nullptr)
    {
        wgpuSurfaceRelease(gSurface);
    }
    if (gInstance != nullptr)
    {
        wgpuInstanceRelease(gInstance);
    }
    gQueue = nullptr;
    gDevice = nullptr;
    gAdapter = nullptr;
    gSurface = nullptr;
    gInstance = nullptr;
    gMaxDimension = 0;
}
bool Initialize(const ApplicationInfo&) noexcept
{
    return false;
}
bool ConnectWindow(const WindowInfo&) noexcept
{
    return false;
}
bool InitializeRendering() noexcept
{
    return false;
}
void ShutdownRendering() noexcept {}
bool BeginFrame() noexcept
{
    return false;
}
bool EndFrame() noexcept
{
    return false;
}
FrameStatus Begin() noexcept
{
    return FrameStatus::Unsupported;
}
FrameStatus End() noexcept
{
    return FrameStatus::Unsupported;
}
} // namespace ludus::graphics::rhi::backend
