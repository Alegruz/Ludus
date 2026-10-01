#pragma once

#include <ludus/foundation/base/types.h>

#include <ludus/platform/native_window.h>

#include <string>

namespace ludus::graphics::rhi
{
struct ApplicationInfo final
{
    std::string Name;
    ludus::foundation::uint32 Version = 0;
};

using WindowInfo = ludus::platform::NativeWindowInfo;

// One main-thread session. Start never waits for browser requests; poll GetStartup.
// The connected platform window/canvas must remain alive until Shutdown.
enum class Backend : ludus::foundation::uint8
{
    Vulkan,
    WebGPU
};
enum class StartupState : ludus::foundation::uint8
{
    Idle,
    Pending,
    Ready,
    Failed,
    DeviceLost
};
enum class StartupError : ludus::foundation::uint8
{
    None,
    InvalidWindow,
    InstanceUnavailable,
    AdapterUnavailable,
    DeviceUnavailable,
    SurfaceUnavailable,
    RenderingUnavailable,
    Validation,
    DeviceLost,
    GenerationExhausted,
};
enum class StartStatus : ludus::foundation::uint8
{
    Pending,
    Ready,
    Failed,
    Busy
};
enum class FrameStatus : ludus::foundation::uint8
{
    Ready,
    NotReady,
    InvalidState,
    Unsupported,
    Failed
};
struct StartupInfo final
{
    Backend SelectedBackend = Backend::Vulkan;
    StartupState State = StartupState::Idle;
    StartupError Error = StartupError::None;
    ludus::foundation::uint32 MaxTextureDimension2D = 0;
};
// Duplicate Start is Busy until Shutdown, including failed/lost sessions.
[[nodiscard]] StartStatus Start(const ApplicationInfo& appInfo, const WindowInfo& windowInfo) noexcept;
[[nodiscard]] StartupInfo GetStartup() noexcept;
[[nodiscard]] FrameStatus BeginFrameStatus() noexcept;
[[nodiscard]] FrameStatus EndFrameStatus() noexcept;

// Native synchronous conveniences retained for source compatibility. Browser
// builds return false; use Start/GetStartup there. Do not mix the two lifecycles.
[[nodiscard]] bool Initialize(const ApplicationInfo& appInfo) noexcept;
[[nodiscard]] bool ConnectWindow(const WindowInfo& windowInfo) noexcept;
[[nodiscard]] bool InitializeRendering() noexcept;
void ShutdownRendering() noexcept;
// Idempotent; invalidates pending callbacks before releasing backend handles.
void Shutdown() noexcept;
[[nodiscard]] bool BeginFrame() noexcept;
[[nodiscard]] bool EndFrame() noexcept;
} // namespace ludus::graphics::rhi
