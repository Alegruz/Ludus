# Public rendering: fullscreen and portable raster

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

Set `LUDUS_SLANG_COMPILER` to the pinned host tool. Linux/browser builds also
require `LUDUS_SPIRV_VALIDATOR`; macOS builds emit MSL directly.
Python 3.10+ is required. Configure/build never downloads tools. Slang 2026.1.2
compiles both targets; SPIRV-Tools validates Vulkan 1.1 SPIR-V at build time. The
manifest records exact expanded commands, emitted entries, profiles, sizes and
artifact hashes. WGSL receives actual browser shader/pipeline validation through
resource creation; successful compilation alone does not establish runtime proof.

### macOS Metal

Run `./scripts/shader-probe bootstrap` on macOS to acquire the SHA-256-verified
Slang 2026.1.2 release for arm64 or x86_64. Configure with
`-DLUDUS_SLANG_COMPILER="$PWD/out/shader-tools/slang/bin/slangc"`.
`ludus_compile_shader` emits separate `<name>.vertex.metal` and
`<name>.fragment.metal`, reflection JSON, a manifest, a depfile and the same
generated `Vertex()` / `Fragment()` descriptions used by other backends.
Source, include and define changes rebuild the artifacts; no-op builds do not.
The macOS header supplies `Msl` and `MslEntry`. Source is compiled as MSL 2.3
during `CreateShader`; entries and stages are checked against the actual library.
Pipeline reflection rejects used textures/samplers/storage or buffers outside
read-only buffer 0, and checks occupied bytes against the declared uniform size.

`Start` is synchronous and selects Metal under native Auto. It accepts a borrowed
Cocoa window/content view or Headless, exposes a conservative 16384 texture limit
on Mac2 / Apple4 devices and later, and caps uniforms at 16 KiB. Cocoa uses a
`CAMetalLayer` with BGRA8Unorm, two drawables and bounded acquisition; hidden,
minimized, zero-size or unavailable drawables skip. Keep the Platform window alive
through shutdown. RHI restores its previous layer when disconnected.
Headless uses a private BGRA8Unorm render target. Both report Unorm attachments.
Two command-buffer-fenced uniform slots protect CPU/GPU reuse; submitted buffers
retain resources after handle destruction. Shutdown drains submitted work and
aborts open frames. Command-buffer errors fail the session at a subsequent frame
boundary, requiring shutdown/restart; there are no facade mutations on GPU threads.

Tests verify clear/readback, live Cocoa presentation, shader and binding errors,
changing uniforms, 40-frame bursts, resize, maximum-sized uploads and restart.
GPU cases explicitly skip when no Metal adapter is present; policy/window checks
still execute. `LUDUS_TEST_COCOA=1` enables the WindowServer presentation case.
Clang 18 ASan cannot start on the local macOS 26.6 host; macOS 14 CI executes
the sanitizer suite. This slice provides the existing fullscreen API; general
meshes, compute, textures, depth and blending remain future work.

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
Uniforms are 16..16384 bytes in multiples of 16. The 16 KiB ceiling permits
full-resolution simulation snapshots while staying within OpenGL ES 3.0's
minimum uniform-block limit. Shader reflection and runtime admission share this
ceiling. CPU staging remains fixed (128 KiB per active backend for eight slots);
updates allocate nothing. Eight resources of each kind are
available; exceeding the pool or exhausting never-reused IDs fails explicitly.

CPU layouts are application contracts. Read separate SPIR-V, WGSL and Metal reflection
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
explicitly rather than switching. Native accepts only Auto, selecting Vulkan on Linux and Metal on macOS.
`StartupInfo::SelectedBackend` reports the committed backend, and `WebGpu`/`WebGL2`
carry bounded per-attempt errors for QA. The GLSL ES artifact requires
`LUDUS_SPIRV_CROSS` at build time (ADR 0013/0013); otherwise the browser build is
WebGPU-only. `apps/smoke` is the worked example of a backend-agnostic renderer
driving this through the public API. See ADR 0014 and
`docs/development/webgl-fallback-sandbox-handoff.md`.

## Readiness and ownership

### Explicit R0 device ownership

Include `ludus/graphics/rhi/device.h` for the first explicit R0 contract slice.
It links through the same `Ludus::GraphicsRhi` target and shares the facade's
single session, authoritative resource registry, callback isolation and backend
teardown. Every explicit operation validates its device owner before delegating.
Frame operations also validate the borrowed surface owner. A headless target is
a surface in this slice; independent device-only/offscreen resource APIs remain
future work. Serialize all operations on the main thread.

```cpp
namespace rhi = ludus::graphics::rhi;
rhi::DeviceHandle device;
rhi::SurfaceHandle surface;
rhi::DeviceDescription request;
request.Limits.MinUniformBufferSize = 48;
request.Preferred.Compute = true;
rhi::StartupInfo failure;
const auto started = rhi::CreateDevice(app, window, request, device, surface, &failure);
// Ready or Pending owns both outputs. Otherwise inspect started and, for a
// synchronous backend startup failure, failure. Admission preserves all outputs.
if (started == rhi::DeviceStatus::Ready || started == rhi::DeviceStatus::Pending)
{
    rhi::DeviceInfo info;
    const auto polled = rhi::GetDeviceInfo(device, info);
    if (polled == rhi::DeviceStatus::Ready)
    {
        // Use device-aware CreateShader/CreateUniform/CreatePipeline, GetStatus,
        // UpdateUniform and DrawFullscreen overloads; check every result.
        // Compute remains false: preferences cannot enable unimplemented APIs.
    }
    (void)rhi::DestroyDevice(device);
}
```

Poll once per application tick while Pending; continue only after Ready. Keep
the borrowed Platform window/canvas alive until `DestroyDevice`. Required `Compute` or `IndirectRendering` returns Unsupported before
launching startup. Required `PortableRaster` validates the negotiated bounded
profile before Ready, including each browser Auto attempt; see the
[portable raster contract](#portable-raster-r1). `GetCompiledBackends` reports compiled code, independent of
adapter availability. Supported/Enabled report implemented fullscreen and portable raster operations
only after Ready; they make no claim about raw adapter features. The copied
request retains its preferred features, including unavailable ones. Effective
limit requirements apply to every Auto attempt through the existing negotiation.

Use `SetFrameTarget(device, surface, target)`, `BeginFrame(device, surface)` and
`EndFrame(device)` for explicit frames. Skipped opens no frame. The output form
`GetFrameInfo(device, surface, info)` preserves `info` on invalid ownership,
pending startup or a closed frame. Resource overloads preserve the existing
fullscreen ownership, failed-resource inspection and InUse rules; this slice
does not introduce deferred destruction or a second resource registry.

Device owners never wrap/reappear across restart; callback attempt tokens remain
separate so fallback retries keep the same device owner. Stale/foreign devices
and surfaces return InvalidHandle without changing another session. Device loss
stops explicit work with DeviceLost, clears enabled capabilities and invalidates
resources; the owned lifecycle record remains queryable until destruction.
Published asynchronous failure likewise still requires `DestroyDevice`. A
synchronous startup failure preserves null outputs, copies optional startup
diagnostics, tears down and leaves the session idle. Successful `DestroyDevice`
clears its device argument; clear stale surface/resource variables yourself before
reusing them as creation outputs.

Use either the explicit lifecycle or the facade lifecycle for a session.
Duplicate starts are rejected. The compatibility `Shutdown` still cancels the
shared session, making all explicit handles stale; it cannot leave an explicit
owner pointing to a later facade session. Existing facade-only callers retain
their behavior.

The reference-backend lifecycle suite tests admission, copied requirements,
output preservation, cancellation, late completions, fallback generations,
cross-owner misuse, loss and non-wrapping owner exhaustion. General typed
resource slot/generation contracts, broader feature/format/limit negotiation,
encoders and deferred retirement are subsequent slices of the
[RHI/GDI architecture](../architecture/rhi-gdi.md#implementation-phases-and-acceptance).

### Compatibility facade

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
culling match the diagnostic on all targets. Shader output is numeric RGBA;
Unorm attachments quantize it, while Srgb attachments apply linear-to-sRGB RGB
conversion. GetFrameInfo reports the attachment conversion, so applications can
choose their output convention explicitly. Palette and display choices belong to
the application. Monitor/browser color management is outside these pixel tests.

See `tests/sdk_consumer/render.cpp` for complete exception-free public usage,
layout assertions, skipped frames, resize, dependency rejection and restart.

## Portable raster (R1)

Include `ludus/graphics/rhi/raster.h` and link the same `Ludus::GraphicsRhi`
component. The first portable raster profile supplies immutable buffers,
sampled textures, complete views, samplers, reflected stages, binding layouts,
binding snapshots and indexed/instanced triangle pipelines on Vulkan, Metal,
WebGPU and WebGL 2. It uses the explicit device and existing acquired frame
surface/headless color target. It does not introduce a second device session.
Request `DeviceDescription::Required.PortableRaster = true` when this workload
is mandatory. Compute and indirect rendering remain unavailable.

`GetRasterCapabilities` reports effective allocation, texture dimension,
uniform offset/range, record and per-frame draw bounds. Limits are verified on
the negotiated device/context, independently of the backend name. Creation
requires a ready device and a null output; it is forbidden in an open frame.
Only Ready or Pending publishes ownership. WebGPU creation returns Pending while
both validation and out-of-memory scopes drain; poll the typed `GetStatus`
once per tick. A dependency that is still pending returns NotReady without
publishing an output. A published failed resource must still be destroyed.
All operations are serialized on the device's owner thread.

The initial profile is deliberately bounded:

| Facility | Contract |
| --- | --- |
| Records | 16 of each kind, including detached records retained by dependencies, validation or submitted work |
| Buffers | Complete immutable initial bytes, at most 64 MiB or the lower enabled limit; Vertex, Index16, Index32 or Uniform role |
| Uniforms | Bound range at most 16 KiB, range size a multiple of 16, offsets aligned to the queried limit (at least 256); allocation may be larger than a bound range |
| Textures | RGBA8 linear or sRGB sampling, at most 4096 in each dimension or the lower enabled limit; one mip/layer/sample |
| Uploads | Complete top-left RGBA8 rows, explicit pitch divisible by four; caller bytes consumed/copied before return; aggregate upload at most the maximum buffer size |
| Views | Complete original-format sampled views; no reinterpretation, partial sampled range or attachment view |
| Bindings | One group/set, at most eight unique binding numbers below eight; uniform ranges, sampled 2D textures and non-comparison samplers |
| Vertex input | At most two streams, per-vertex or divisor-one per-instance, stride 4..256 divisible by four; eight unique float2/3/4 attributes |
| Draw | Triangle list, Index16/Index32, zero base vertex/first instance, 1024 draws per frame; fixed primitive-restart indices are rejected |
| State | No culling, one color target, one sample; optional less-than depth test/write and premultiplied-alpha blend |

Draws use canonical clip depth [0,1], Y-up geometry and texture UV (0,0) at
the first uploaded row. Vulkan uses a negative-height viewport; the GLSL ES
artifact converts clip depth. The frame's private depth attachment clears to
one. Pipelines remain valid on resize because the session target format stays
fixed. General render targets, explicit pass/encoder objects, updates, resource
texture copy commands, mip generation and storage/compute are later slices.
[R2 lifetime services](#lifetime-services-r2) add retained batches and bounded
buffer upload/readback rings alongside the direct frame API.
Initial Vulkan texture uploads normally use a synchronous setup copy. A timed-out
copy remains Pending and retains staging until its fence signals; poll GetStatus.
This is not a
streaming upload strategy or a performance claim.

### Shader generation and binding snapshots

```cmake
ludus_compile_shader(TARGET renderer NAME mesh RASTER
    SOURCE shaders/mesh.slang VERTEX vertexMain FRAGMENT fragmentMain)
```

The RASTER option emits `RasterShaderDescription`, separate per-stage artifacts,
and a manifest with per-target bindings, vertex inputs, occupied uniform sizes
and member offsets. Tool acquisition and pinning follow the shader workflow
above. Vulkan/Metal layouts are independent: match CPU packing to the selected
artifact and verify offsets/size/alignment. Hand-authored artifacts must supply
independently verified target metadata. Metal pipeline reflection and the actual
linked WebGL program independently reject incompatible used resources/inputs.

Browser generation verifies emitted WGSL and GLSL ES uniform member offsets and
sizes, rather than accepting equal total sizes alone. The first browser parser
accepts flat float/vector uniform members and rejects unsupported shapes.
It compiles WGSL stages separately because pinned Slang 2026.1.2's combined
reflection omits per-stage resource-usage flags. The tool matches emitted named
float inputs to stage reflection and explicitly normalizes their locations;
inter-stage outputs and builtins are not rewritten. GLSL ES varyings are named
from their independently verified SPIR-V location map so ES 3.00 can link stages
by identifier. Mismatched interfaces reject generation. A texture paired with
multiple samplers in one stage is outside this first GLSL profile.

Create a binding layout covering both shader stages, using the union of their
requirements and visibility. `CreateBindingSet` requires exactly one entry per
layout binding, with correct kinds, uniform roles, offsets and accessible sizes.
The snapshot retains its layout and referenced buffers/views/samplers; a view
retains its texture. A pipeline retains stages and layout independently of
binding contents. `DrawIndexed` requires the exact layout identity, not just
structural similarity, and a declared vertex slice for each stream. It checks
every mesh-local index against VertexCount, accessible vertex/instance records,
index bounds and completion capacity before issuing native work.

Create resources outside frames, then set a target, BeginFrame, DrawIndexed and
EndFrame. Check every result. Portable draws and DrawFullscreen cannot share
one frame. The complete public-only example is
[`tests/sdk_consumer/raster.cpp`](../../tests/sdk_consumer/raster.cpp), with its
[application-owned shader](../../tests/sdk_consumer/shaders/raster.slang).

### Destruction and validation evidence

Typed Destroy takes the handle by reference, invalidates public resolution and
zeros that variable. Other copies become stale immediately. Physical release
waits for immutable dependencies, the current frame's CPU leases, successful
GPU completion, and pending backend validation scopes. Resource generations,
callback request IDs and ordered submission ordinals never wrap/reappear.
A capacity failure can therefore occur after public destruction while old work
is still live. Shutdown/loss invalidates all records and drains/drops the native
device; late browser callbacks cannot affect the next session.

Vulkan general allocations use the private MIT-licensed
[Vulkan Memory Allocator 3.3.0](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/releases/tag/v3.3.0),
pinned with header/license hashes in `config/vulkan_memory_allocator.json`.
VMA handles mapped allocation flush alignment. The existing ordered queue's
fences establish resource retirement; Metal uses command-buffer completion,
WebGPU queue work-done callbacks, and WebGL nonblocking sync polling.
The minimum retirement mechanism accompanies the first general resources;
R2 exposes this same retirement ledger through the services below; dynamic
descriptor arenas and texture streaming remain deferred.

The native RHI pixel fixture draws two textured instances, then a farther,
differently tinted background; it checks texture orientation, instancing and
depth after public resource destruction. Installed shader-helper checks compile
and run the public-only SDK consumer, verify per-target interfaces, dependency
rebuilds and binding rejection. The browser SDK harness
`tests/sdk_consumer/raster-browser-test.mjs` checks pixels on forced WebGPU,
forced WebGL 2, Auto and capability fallback, plus invalid shader/pipeline and
device-loss paths in pinned Chromium. Browser software-GPU results are separate
from physical GPU and hosted-browser acceptance. These checks make no throughput
or allocation-performance claim. See the architecture's
[phase evidence](../architecture/rhi-gdi.md#implementation-phases-and-acceptance).

## Lifetime services (R2)

Include `<ludus/graphics/rhi/lifetime.h>` with a ready device that enabled
`PortableRaster`. These services share R1's registry and dependency graph;
there is no second device or ownership ledger. All calls run on the device's
owner thread, between frames unless a query or tracked `EndFrame` says otherwise.

The first service profile has four command batches, 256 draw packets per batch,
four upload slices and four readback slices independently, 256 KiB per slice,
and sixteen independently owned pipeline requests. Query
`GetLifetimeCapabilities`; retained/cancelled work counts against capacity.
Records still count against the R1 per-kind limits. `CapacityExceeded` is
retryable after safe retirement, without waiting inside an admission call.

### Record, finish, submit or discard

`BeginCommands` creates a Recording batch. `RecordDraw` validates and copies
one complete draw, retaining geometry, the binding snapshot, pipeline and their
resource dependencies. No backend command is emitted during recording. Destroy
public handles after acceptance if desired: the retained packet uses the same
physical records. `FinishCommands` closes recording once. Submit the Finished
batch to its device/surface or `DiscardCommands` either Recording or Finished
work; discard emits no GPU work.

```cpp
rhi::CommandBatch batch;
rhi::SubmissionToken completion;
// Check each result; these calls require Ready before advancing.
rhi::BeginCommands(device, batch);
rhi::RecordDraw(device, batch, draw);
rhi::FinishCommands(device, batch);
rhi::SubmitCommands(device, surface, batch, completion);
```

The snippet shows call order; the complete checked consumer is
[`tests/sdk_consumer/raster.cpp`](../../tests/sdk_consumer/raster.cpp).
A skipped target returns `NotReady` and preserves the Finished batch for retry.
Preflight or completion-capacity rejection preserves it too. Once encoding
starts, an error may accompany an accepted partial submission; inspect both
status and completion. A successful submission consumes the batch and shifts
its leases to GPU retirement. A later resize acquires the current target without
changing immutable pipeline identity. Shutdown/loss invalidates batches and
releases their physical dependencies at the backend's safe teardown boundary.

`EndFrame(device, completion)` tracks direct and clear-only frames as well.
`GetStatus(device, completion)` is a zero-wait query: `Pending` means accepted
ordered work is outstanding, `Ready` means GPU execution completed. A complete
higher token covers earlier accepted work on the same queue/session. Tokens are
borrowed, require no release, never wrap, and are invalid across device restart.
GPU completion does not prove presentation or pixel correctness.

### Upload and readback rings

`RequestBufferUpload` copies complete immutable initial bytes before return.
The ticket owns a ring slice and destination buffer; the caller can immediately
reuse its byte storage. Poll its status on later owner/event-loop turns, then
`TakeUploadedBuffer` transfers the Ready buffer into a null output.
`Release(ticket)` cancels publication, preserving staging and the private result
until GPU work and creation validation retire. Uniform/vertex/index role and
size/alignment rules still apply. Index16 uploads pad only the private copy to
four bytes; public size and index validation keep the original byte count.

`RequestBufferReadback` retains a Ready buffer and copies a bounded range to a
separate ring. Offsets and sizes must be multiples of four within the original
buffer size. After status becomes Ready, `CopyReadback` copies into caller
storage; it never exposes a native mapping and preserves destination bytes on
Pending/rejection/failure. Release the ticket even after a terminal request
failure. Destroying the source handle before completion is safe. Logical
cancellation does not recycle an in-flight copy or mapping.

Vulkan uses the existing private VMA adapter, cached mapped staging, narrow
transfer/host barriers and zero-timeout fence queries. Metal uses shared staging
and submitted blits. WebGPU uses copy buffers, asynchronous validation/work-done
and read mapping; mapping success is separate from queue progress. WebGL 2 uses
type-compatible copy buffers, zero-timeout sync queries and `getBufferSubData`
only after completion. That final browser copy can incur IPC/CPU cost; this
profile promises no driver time budget or throughput improvement. Native
shutdown may wait for submitted work; browser callback cells stay occupied
across shutdown until late callbacks drain safely.

### Pipeline requests

`RequestPipeline` copies the description and retains immutable shader/layout
incarnations immediately. Equivalent descriptions share one pending/ready/failed
record; comparison includes every active field, canonical attribute locations,
fixed state and device session, with no pointer/padding/hash identity.
`PollLifetime` launches at most one queued pipeline per call. WebGPU uses
asynchronous driver creation; Vulkan, Metal and WebGL create on the context owner
and may occupy that call. There is no portable worker-thread compilation or
persistent driver-cache promise in this slice.

`GetRequestedPipeline` borrows the Ready pipeline while its request is owned.
Do not Destroy that borrowed handle: release the request. Other requests and
accepted batches retain independent leases. The last request removes the cache
entry; callbacks and CPU/GPU work keep physical records until safe retirement.
Cancelling a queued request before launch creates no backend pipeline. Failures
remain explicit; no backend switch or fallback pipeline is inserted.

Reference tests cover finish/discard/submit, destruction at each boundary,
capacity and delayed completion, cancellation, memory/validation failure and
restart. Native pixel tests render from detached retained records; native
transfer tests compare all four buffer roles. The installed SDK consumer checks
uploads/readbacks, shared pipeline requests, completion and final-frame
resource release on forced WebGPU/WebGL 2 and Auto. The browser harness adds
pipeline, transfer and mapping failures and device loss. Software-GPU browser
results remain separate from physical GPU/browser acceptance.

Texture rings, mutable buffer updates, descriptor suballocation, general
copy/pass APIs and a separate GraphicsDevice module remain later extensions.
