# Graphics RHI

Link `Ludus::GraphicsRhi`. Include `<ludus/graphics/rhi/rhi.h>` for lifecycle and
frames and `<ludus/graphics/rhi/render.h>` for the bounded fullscreen rendering
slice. The selected private backend is Vulkan on native Linux and WebGPU through
the pinned Emdawnwebgpu port on Emscripten. Public descriptions use engine types
and opaque handles; installed consumers need no backend or private headers.

The [public rendering guide](../../../docs/development/fullscreen-rendering.md)
describes resource ownership, asynchronous readiness, frame updates and errors.
The [Sandbox handoff](../../../docs/development/fullscreen-rendering-handoff.md)
contains SDK commands, shader artifacts and validation evidence.

Serialize calls on the main thread. `Start`/`GetStartup` establishes one session;
browser startup is asynchronous. Keep the Platform window/canvas alive through
`Shutdown`. Start is Busy until Shutdown, including failed/lost sessions. Shutdown
is idempotent and invalidates resource IDs and pending callbacks before releasing
backend state. Delayed callbacks never retain application memory or affect a new
session. Legacy synchronous native entry points remain for compatibility; do not
mix the two lifecycles. The resource API requires the Start lifecycle.

Both backends accept `SetFrameTarget` between frames. Zero dimensions skip frame
acquisition. `GetFrameInfo` gives the actual acquired extent and attachment
encoding after a successful begin. Resize retains resources unless the native
surface format changes, in which case the session fails and requires restart.
Frame acquisition timeout/outdated surfaces skip; device loss and unrecoverable
surface/validation failures stop the session. Only end a frame that began Ready.

Native Vulkan uses two fenced command/uniform slots, a graphics/present queue
family, per-image presentation semaphores and maintenance1 presentation fences.
Wayland requires surface/swapchain maintenance1 for safe resize/shutdown; missing
support fails explicitly. Headless windows use an owned offscreen image for
native rendering tests. Building requires only the existing Conan Volk/headers;
running requires a Vulkan loader and usable device. Browser packaging propagates
the existing pinned `--use-port` through the SDK target.

The existing smoke/probe private WGSL interop remains a test boundary. External
applications use the public rendering API and exported `ludus_compile_shader`.
Neither compiler tools nor probe headers are installed as engine dependencies.

## Vulkan diagnostics

Implementation files formatting VkResult use the private
`internal/vulkan_diagnostics.h` adapter. Ordinary diagnostics use `LUDUS_LOG_*`;
Foundation and public headers gain no Vulkan dependency from formatting.
