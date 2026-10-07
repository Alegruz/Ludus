# Platform and rendering

Platform owns the native window or browser canvas. Input supplies normalized
keyboard records. GraphicsRhi owns the graphics session, frames and resources
through engine descriptions and opaque handles. Applications compose these
owners without exposing native backend types in installed headers.

## Keep dependency direction visible

Platform depends on Input for keyboard vocabulary; Input does not need to know
about a Wayland window or browser canvas. The application routes input into UI
and gameplay. Runtime Ui has no Platform, RHI or Text dependency; adapters join
those systems where the application needs them.

| Surface | Current responsibility | Important limit |
| --- | --- | --- |
| Platform | Linux/Wayland or browser canvas integration; headless support where provided | A compile-time target is not a guarantee of a validated desktop backend |
| GraphicsRhi | Native Vulkan; browser WebGPU or WebGL 2 | Bounded public rendering slice, not a complete scene renderer |
| Ui | Portable runtime UI core | Qt desktop tools have a separate UI owner |
| Text | Native FreeType/HarfBuzz-backed text facilities | Browser dependency bootstrap is still pending |

See [platform targets and compatibility](../guides/platform-targets.md) before
choosing a host or assuming an operation is available.

## A frame belongs to one graphics session

Serialize RHI calls on the main thread. `Start` and `GetStartup` establish the
session; browser readiness is asynchronous. Keep the Platform window/canvas
alive until RHI shutdown completes. Resource APIs use this lifecycle rather
than the legacy synchronous native entry points.

1. Service platform/input and observe startup status.
2. Set the frame target between frames.
3. Begin a frame and inspect the result.
4. On Ready, use the acquired extent/attachment encoding and render.
5. End only a frame that began Ready.

Zero dimensions skip acquisition. Timeouts or outdated surfaces can skip a
frame; device loss and unrecoverable surface errors stop the session. Resize
normally retains resources, but a native surface-format change requires restart.
Do not turn a skipped frame into a gameplay fault or attempt to end it.

## Backend choice is an explicit policy

Browser Auto attempts the pinned WebGPU path before one eligible WebGL 2
fallback. A forced backend reports its own failure. Native Vulkan needs a usable
loader/device at runtime; Wayland requires the documented maintenance extensions
for safe resize/shutdown. Query capabilities and validate requirements instead
of inferring support from the backend name.

GPU-visible storage needs backend retirement proof. CPU task completion and an
elapsed frame count are not substitutes. RHI shutdown invalidates resource IDs
and pending callbacks before releasing backend state; stale callbacks must not
publish into a later session.

The public slice is declared in `rhi.h` and `render.h`; application shader builds
use the exported `ludus_compile_shader` helper. Private smoke/probe WGSL pipelines
remain sample/test boundaries.

Read the [RHI module guide](https://github.com/Alegruz/Ludus/blob/main/modules/graphics/rhi/README.md),
[public rendering contract](../../development/fullscreen-rendering.md)
and [runtime UI design](../../architecture/ui.md).
Continue with [world-to-presentation ownership](world.md).
The [terrain proposal](terrain.md) describes how a future mesh/texture rendering
slice can consume editable ground without owning physical terrain or generation.
