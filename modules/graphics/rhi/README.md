# Graphics RHI

Link `Ludus::GraphicsRhi` and include `<ludus/graphics/rhi/rhi.h>`.
The build selects a private Vulkan backend on native platforms and the pinned
Emdawnwebgpu C API backend for Emscripten. Public headers expose no backend handles.

Call `Start(app, window)` once and poll `GetStartup()` on the main thread. Native
startup completes synchronously; browser startup returns Pending and advances
through adapter, device and validated surface configuration without blocking.
Ready means the device and surface exist. SetFrameTarget supplies framebuffer
dimensions and a linear clear color for browser rendering. Apply the negotiated
MaxTextureDimension2D to the Platform window before reading its framebuffer size.
No optional WebGPU features or increased limits are requested. Core adapter failure retries compatibility mode.

Start is Busy until Shutdown, including a failed or lost session. Shutdown is
idempotent and invalidates callbacks before releasing backend handles. Tokens
never wrap; delayed adapter/device callbacks release returned handles without
mutating the next session. A callback never retains application memory. Serialize
all calls on the main thread; this singleton API is not worker-safe. The platform
window/canvas must remain alive until RHI shutdown. Shutdown cancels the engine's
interest in a pending browser request; it does not cancel the browser promise.

BeginFrameStatus/EndFrameStatus return NotReady before readiness, InvalidState
for unpaired frame operations, and Skipped for zero-sized or temporarily
unavailable browser frames. Only end a frame whose begin returned Ready.
SetFrameTarget returns InvalidState during an open frame, for dimensions above
the device limit, or for nonfinite/out-of-range color components (each is 0..1).
The browser backend reconfigures on resize, acquires a texture/view, encodes a
clear pass, submits and releases all per-frame handles. Browser presentation is
automatic after submission from a requestAnimationFrame tick. Generic acquisition
errors stop the session with RenderingUnavailable; validation/device loss stops
rendering and requires Shutdown/Start. Timeout skips; outdated surfaces
reconfigure at the next nonzero frame. Lost surfaces stop the session rather than
retrying an invalid surface. The pinned browser port currently only emits
SuccessOptimal or Error, so those recoverable statuses are future-facing.
Native SetFrameTarget returns Unsupported; Vulkan keeps its existing frame path.
The bool wrappers remain for source compatibility. Native Initialize,
ConnectWindow and InitializeRendering are retained as synchronous conveniences;
do not mix them with Start. The smoke app uses Start, and the installed SDK
consumer compiles and queries the new API. The new exported functions require
rebuilding downstream binaries; the old entry-point signatures are unchanged.

Conan pins native volk/Vulkan headers; no Vulkan SDK or GPU is required to build.
A loader and usable Wayland/Vulkan device are required for native startup.
Emscripten builds resolve their separate pinned port through --use-port, without
Conan/Volk. Controlled test providers are linked only into the lifecycle/frame
probes, and never replace browser navigator.gpu. The W5 frame probe owns its WGSL and render
pipeline; a private interop header borrows device/format/active pass for that
smoke scope only. These handles are not installed or exposed in public headers.
The probe releases its pipeline before shutdown and ignores stale validation
callbacks. A general resource/pipeline API belongs to a later stage.

## Vulkan diagnostics

Implementation files that format `VkResult` in assertions must include
`internal/vulkan_diagnostics.h` before the assertion call sites. Both
`LUDUS_LOG_*` and formatted assertion macros then accept the result directly
with `{}`, preserving its signed numeric code (including unknown result codes).
The adapter stays private to RHI; Foundation and the installed SDK do not gain
a Vulkan dependency from diagnostic formatting.
