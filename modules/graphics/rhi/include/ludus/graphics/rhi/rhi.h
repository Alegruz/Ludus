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
    WebGPU,
    WebGL2,
    /// Native macOS renderer using Metal for Cocoa and headless targets.
    Metal
};
/// Browser backend policy. Native accepts only Auto, selecting Vulkan on Linux and Metal on macOS.
/// Auto attempts WebGPU (including its compatibility retry) and, only on a
/// capability/startup failure, makes exactly one WebGL 2 attempt. A forced
/// selection fails explicitly rather than silently switching backends.
enum class BackendSelection : ludus::foundation::uint8
{
    Auto,
    WebGPU,
    WebGL2
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
    BackendUnavailable,
    RequirementsUnsatisfied,
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
    Failed,
    Skipped
};
// Bounded per-backend attempt diagnostics for an Auto session. Each attempt
// records whether it ran and its final error; the engine never asserts the GPU
// itself is unsupported. QA reads these; ordinary player text does not.
struct AttemptInfo final
{
    bool Attempted = false;
    StartupError Error = StartupError::None;
};
// Effective limits of the implemented fullscreen API, not raw adapter limits.
// Available only while Ready; zero in all other states. Sizes are bytes.
// A dimension limit is an upper bound, not a reservation or surface guarantee.
struct DeviceCapabilities final
{
    ludus::foundation::uint32 MaxFrameDimension2D = 0;
    ludus::foundation::uint32 MaxUniformBufferSize = 0;
    ludus::foundation::uint32 UniformBufferSizeAlignment = 0;
    ludus::foundation::uint32 MaxShaders = 0;
    ludus::foundation::uint32 MaxUniformBuffers = 0;
    ludus::foundation::uint32 MaxPipelines = 0;
    ludus::foundation::uint32 MaxDrawsPerFrame = 0;
};
// Copied at Start; zero imposes no additional requirement. Negotiation completes
// before Ready and before callers can create resources. Auto checks every attempt.
// Validates enabled limits; this slice retains WebGPU's default device policy.
struct DeviceRequirements final
{
    ludus::foundation::uint32 MinFrameDimension2D = 0;
    ludus::foundation::uint32 MinUniformBufferSize = 0;
};
enum class RequirementFailure : ludus::foundation::uint8
{
    None,
    FrameDimension2D,
    UniformBufferSize,
};
struct StartupInfo final
{
    // The backend actually selected for this session (not merely requested).
    Backend SelectedBackend = Backend::Vulkan;
    StartupState State = StartupState::Idle;
    StartupError Error = StartupError::None;
    ludus::foundation::uint32 MaxTextureDimension2D = 0;
    BackendSelection Requested = BackendSelection::Auto;
    AttemptInfo WebGpu;
    AttemptInfo WebGL2;
    DeviceCapabilities Capabilities;
    DeviceRequirements Requirements;
    // First unmet requirement of the final attempt; None for other failures.
    RequirementFailure UnmetRequirement = RequirementFailure::None;
};
// Duplicate Start is Busy until Shutdown, including failed/lost sessions.
// The two-argument form selects Auto on the browser and Vulkan natively.
[[nodiscard]] StartStatus Start(const ApplicationInfo& appInfo, const WindowInfo& windowInfo) noexcept;
// Explicit browser backend selection. Native rejects forced browser backends.
[[nodiscard]] StartStatus
Start(const ApplicationInfo& appInfo, const WindowInfo& windowInfo, BackendSelection selection) noexcept;
[[nodiscard]] StartStatus Start(const ApplicationInfo& appInfo,
                                const WindowInfo& windowInfo,
                                BackendSelection selection,
                                const DeviceRequirements& requirements) noexcept;
[[nodiscard]] StartupInfo GetStartup() noexcept;
// Surface settings for the next frame. Zero dimensions skip acquisition.
// Call only between frames after Ready; dimensions must fit negotiated limits.
// Native Vulkan negotiates/clamps the requested extent against surface limits.
struct FrameTarget final
{
    ludus::foundation::uint32 Width = 0;
    ludus::foundation::uint32 Height = 0;
    ludus::foundation::float64 Red = 0;
    ludus::foundation::float64 Green = 0;
    ludus::foundation::float64 Blue = 0;
    ludus::foundation::float64 Alpha = 1;
};
[[nodiscard]] FrameStatus SetFrameTarget(const FrameTarget& target) noexcept;
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
