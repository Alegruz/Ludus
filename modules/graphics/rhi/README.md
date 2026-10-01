# Graphics RHI

Link `Ludus::GraphicsRhi` and include `<ludus/graphics/rhi/rhi.h>`.
The build selects a private Vulkan backend on native platforms and the pinned
Emdawnwebgpu C API backend for Emscripten. Public headers expose no backend handles.

Call `Start(app, window)` once and poll `GetStartup()` on the main thread. Native
startup completes synchronously; browser startup returns Pending and advances
through adapter, device and validated surface configuration without blocking.
Ready means these resources exist, not that frames have been implemented on web.
The device's negotiated MaxTextureDimension2D is published after configuration;
W5 will use it for canvas resize and rendering. No optional WebGPU features or
increased limits are requested. Core adapter failure retries compatibility mode.

Start is Busy until Shutdown, including a failed or lost session. Shutdown is
idempotent and invalidates callbacks before releasing backend handles. Tokens
never wrap; delayed adapter/device callbacks release returned handles without
mutating the next session. A callback never retains application memory. Serialize
all calls on the main thread; this singleton API is not worker-safe. The platform
window/canvas must remain alive until RHI shutdown. Shutdown cancels the engine's
interest in a pending browser request; it does not cancel the browser promise.

BeginFrameStatus/EndFrameStatus return NotReady before readiness, InvalidState
for unpaired frame operations, and Unsupported for web rendering until W5.
The bool wrappers remain for source compatibility. Native Initialize,
ConnectWindow and InitializeRendering are retained as synchronous conveniences;
do not mix them with Start. The smoke app uses Start, and the installed SDK
consumer compiles and queries the new API. The new exported functions require
rebuilding downstream binaries; the old entry-point signatures are unchanged.

Conan pins native volk/Vulkan headers; no Vulkan SDK or GPU is required to build.
A loader and usable Wayland/Vulkan device are required for native startup.
Emscripten builds resolve their separate pinned port through --use-port, without
Conan/Volk. The test provider is linked only into the lifecycle probe, and never
replaces browser navigator.gpu.

## Vulkan diagnostics

Implementation files that format `VkResult` in assertions must include
`internal/vulkan_diagnostics.h` before the assertion call sites. Both
`LUDUS_LOG_*` and formatted assertion macros then accept the result directly
with `{}`, preserving its signed numeric code (including unknown result codes).
The adapter stays private to RHI; Foundation and the installed SDK do not gain
a Vulkan dependency from diagnostic formatting.
