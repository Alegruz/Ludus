# Public fullscreen rendering

Link `Ludus::GraphicsRhi` and include `rhi.h` and `render.h` under
`ludus/graphics/rhi/`. This slice supplies one vertex/fragment pipeline, one
uniform binding and one non-indexed fullscreen triangle per frame. Ludus owns GPU
creation, binding, upload synchronization, submission and cleanup. Applications
own shader source, upload layout, colors, simulation, camera and UI.

## Shader build and upload contract

After `find_package(Ludus CONFIG REQUIRED)`, compile an application-owned shader:

```cmake
add_executable(fullscreen main.cpp)
target_link_libraries(fullscreen PRIVATE Ludus::GraphicsRhi)
ludus_compile_shader(TARGET fullscreen NAME display SOURCE shaders/display.slang
    VERTEX vertexMain FRAGMENT fragmentMain
    INCLUDES shaders DEFINES DISPLAY_SCALE=1 DEPENDS generated_settings.slang)
```

Set `LUDUS_SLANG_COMPILER` and `LUDUS_SPIRV_VALIDATOR` to the pinned host tools.
Python 3.10+ is required. Configure/build never downloads tools. Slang 2026.1.2
compiles both targets; SPIRV-Tools validates Vulkan 1.1 SPIR-V at build time. The
manifest records exact expanded commands, emitted entries, profiles, sizes and
artifact hashes. WGSL receives actual browser shader/pipeline validation through
resource creation; successful compilation alone does not establish runtime proof.

### Optional GLSL ES 3.00 (WebGL 2) backend artifact

Set `LUDUS_SPIRV_CROSS` to the pinned build-time SPIR-V -> GLSL ES translator
(`config/spirv_cross_toolchain.json`, SPIRV-Cross `vulkan-sdk-1.4.313.0`) to also
emit GLSL ES 3.00 for the WebGL 2 fallback. With it unset, SPIR-V/WGSL builds are
unchanged. When set, each stage's validated SPIR-V is translated to
`<name>.<stage>.essl`, the browser build of the generated header carries both WGSL
and GLSL ES, and `ShaderDescription` exposes `GlslEs` / `GlslEsEntry` (which link
through `main`). The manifest adds `glsl_es_entries`, a `glsl_es_layout` derived
independently from the emitted std140 block, the `300 es` profile and translator
provenance. See ADR 0013 and `docs/development/webgl-shader-feasibility-evidence.json`.

Author vertex inputs with `SV_VulkanVertexID` / `SV_VulkanInstanceID` (not
`SV_VertexID` / `SV_InstanceID`): the latter require base-vertex/instance
(`SPV_KHR_shader_draw_parameters`), which has no GLSL ES / WebGL 2 equivalent and
the translator rejects. For a non-indexed, zero-base-vertex draw the values are
identical on Vulkan, WebGPU and WebGL 2.

The generated `display.h` provides
`ludus::shaders::display::Vertex()` and `Fragment()` descriptions, with explicit
backend entry names and minimum uniform sizes from each target's reflection.
Include it in application implementation code. Only the selected target's bytes
are embedded in the application. Shader description storage is borrowed during
CreateShader only. Raw descriptions can instead supply SPIR-V words and WGSL
text with separate explicit entry names. Use validated artifacts; runtime shader
creation does not replace full offline SPIR-V validation.

The shader has set/group **0**, binding **0**, one uniform block visible to both
stages, vertex-index-generated positions, and location 0 RGBA fragment output.
No vertex attributes, textures, samplers, storage, compute, blending, depth,
culling, multiple draws, dynamic offsets or push constants are provided.
Uniforms are 16..4096 bytes in multiples of 16. Eight resources of each kind are
available; exceeding the pool or exhausting never-reused IDs fails explicitly.

CPU layouts are application contracts. Read separate SPIR-V and WGSL reflection
and emitted layout; use explicit padding and C++ standard-layout/offsetof/size/
alignment assertions. Different targets may need different upload structs. The
helper carries occupied size to prevent undersized bindings; it does not infer
CPU field types or assert that packing matches. The diagnostic's independently
checked fields are resolution float2 at 0, elapsed float at 8, direction float3 at
16 and tint float4 at 32: 48 bytes, alignment 16 for each target, padding at
12..15 and 28..31. These measurements apply to that shader only (ADR 0010).

### Browser backend selection (WebGPU / WebGL 2)

On the browser, `Start(app, window)` selects `BackendSelection::Auto`: it attempts
WebGPU (including its compatibility retry) and, only on a capability/startup
failure, makes exactly one WebGL 2 attempt. `Start(app, window, selection)` forces
`WebGPU` or `WebGL2` for diagnosis; a forced backend that is unavailable fails
explicitly rather than switching. Native ignores the selection (Vulkan).
`StartupInfo::SelectedBackend` reports the committed backend, and `WebGpu`/`WebGL2`
carry bounded per-attempt errors for QA. The GLSL ES artifact requires
`LUDUS_SPIRV_CROSS` at build time (ADR 0013/0013); otherwise the browser build is
WebGPU-only. `apps/smoke` is the worked example of a backend-agnostic renderer
driving this through the public API. See ADR 0014 and
`docs/development/webgl-fallback-sandbox-handoff.md`.

## Readiness and ownership

1. Start RHI with the Platform window/canvas and poll GetStartup. Continue only
   when Ready. Browser startup is Pending; failures require Shutdown before Start.
2. Create vertex/fragment shaders and one uniform into default handles. Ready or
   Pending transfers ownership of a handle. Poll GetStatus for each; Pending is
   never drawable. Wait for all three Ready before CreatePipeline, then poll it.
   Pending dependencies return NotReady without creating a pipeline; Pending
   creation always supplies an owned output handle.
   Failed creation can retain a Failed handle for inspection/destruction; partial
   backend objects are already released. Invalid descriptions/admission failures
   leave the output unchanged. Never retry into a nondefault handle.
3. Update the complete uniform byte range. It must be updated before first draw.
   Upload storage is copied immediately, so application stack data is safe.
4. SetFrameTarget from framebuffer dimensions, then BeginFrameStatus. Skipped
   opens no frame: retain resources and retry next tick. After Ready,
   GetFrameInfo reports actual pixels and Unorm/Srgb attachment conversion. Update
   resolution from that extent before the draw if native negotiation changed it.
5. DrawFullscreen only with a Ready pipeline, once inside that frame. UpdateUniform
   is allowed before the first draw, including after begin; updates after draw
   are rejected. EndFrameStatus submits. Do not end a skipped/failed begin.
6. Between frames destroy pipeline, then shaders/uniform. Dependencies of Ready
   or Pending pipelines return InUse. A failed pipeline holds no dependencies.
   Shutdown can replace individual destruction and also abort an open frame.

Copied handles refer to the same ownership; destroying one invalidates every
copy. Destroy does not clear your local value: assign `{}` before reusing it as a
creation output. Stale/default/previous-session handles return InvalidHandle.
IDs are monotonic across shutdown/restart, and asynchronous callbacks carry only
IDs in process-owned storage. They neither retain application pointers nor write
into a reused resource slot. Serialize all operations on the main thread.

Vulkan allocates/map-persistently uploads two uniform slots at creation; draw
copies the latest CPU shadow into the slot whose frame fence has completed.
Resource destruction waits for GPU use; presentation fences retire WSI objects
before resize/shutdown. WebGPU writes are ordered with submissions and submitted
commands retain references when host resources are released. The engine creates
no shader/buffer/pipeline resources or heap allocations per draw. Existing
backend frame encoder/texture objects continue to follow the frame lifecycle.

## Errors, resize and restart

Inspect each returned status and GetStartup after failures. Failed or DeviceLost
sessions release resources and require Shutdown/Start plus recreation and upload.
A zero-size target skips without invalidating resources. Frame acquisition
outdated/timeout conditions skip; native suboptimal presentation schedules resize.
Native attachment format changes fail rather than reuse an incompatible pipeline.
GetFrameInfo outside an open frame returns zero dimensions.

Fragment coordinates have a top-left origin; positive viewport heights/no
culling match the diagnostic on both targets. Shader output is numeric RGBA;
Unorm attachments quantize it, while Srgb attachments apply linear-to-sRGB RGB
conversion. GetFrameInfo reports the attachment conversion, so applications can
choose their output convention explicitly. Palette and display choices belong to
the application. Monitor/browser color management is outside these pixel tests.

See `tests/sdk_consumer/render.cpp` for complete exception-free public usage,
layout assertions, skipped frames, resize, dependency rejection and restart.
