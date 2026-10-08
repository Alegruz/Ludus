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
meshes, compute, textures, depth and blending are covered by the later R1–R4 slices below.

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
        // Inspect info.Enabled.Compute before choosing the GPU compute variant.
    }
    (void)rhi::DestroyDevice(device);
}
```

Poll once per application tick while Pending; continue only after Ready. Keep
the borrowed Platform window/canvas alive until `DestroyDevice`. Required `Compute` or `IndirectRendering` rejects forced WebGL 2 before
launching startup; other backends validate their enabled compute limits before Ready.
Required `PortableRaster` validates the negotiated bounded
profile before Ready, including each browser Auto attempt; see the
[portable raster contract](#portable-raster-r1). `GetCompiledBackends` reports compiled code, independent of
adapter availability. Supported/Enabled report implemented fullscreen, portable raster and bounded compute operations
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
Admission skips those occupied callback cells so the remaining free slices stay
usable after restart.

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

## Ordered raster graphs (R3)

Include `<ludus/graphics/rhi/graph.h>` and inspect
`DeviceInfo.Enabled.OrderedRasterGraph`. Requiring this feature also requires
portable raster admission. The compiler and direct raster passes use the existing
RHI registry, ownership leases and ordered submission tokens. All calls run on
the owner thread; setup, compilation and execution start between frames.

The first profile is four graphs, sixteen logical resources per graph, thirty-two
ordered raster passes, sixteen declarations per pass, 256 retained draws per
graph, and 512 semantic dependency report entries. Capacity rejection occurs
before frame acquisition. Raster formats remain RGBA8 linear/sRGB, one mip,
one layer and one sample, with one color attachment and private same-size depth.
R4 extends this profile with buffer compute below. General copy passes, MSAA/resolve,
stencil, depth sampling, physical heap aliasing and pass merging remain deferred.

### Author, compile and execute

Create an `OrderedGraph`, import each physical buffer/texture once, reserve new
attachment versions with `NextGraphVersion`, then append `GraphPassDescription`
values in intended execution order. Each pass copies its diagnostic name/source,
resource declarations and complete draw packets, retaining their physical
records. It does not retain callbacks or caller arrays. Declarations must cover
vertex/index slices and every immutable binding entry, including unused extra
bindings that contribute to WebGPU's rendering scope. Samplers are retained by
binding snapshots and have no memory-content hazard declaration.

Buffer declarations carry byte ranges and roles. Reports explicitly identify
conservative whole-buffer hazard tracking; the implementation does not claim
subrange scheduling. Textures declare their complete color image. A pass cannot
sample its own color attachment or declare conflicting uses of one physical
resource. Clear establishes full initialization; Load requires defined prior
contents; Discard followed by partial draws does not establish initialization.
Store Discard invalidates contents. The acquired surface begins cleared by the
existing frame path and must finish with defined stored color for presentation.
Depth Clear uses one; Depth Load requires a stored prior depth image. A read-only
depth pass requires Load and rejects pipelines that write depth. `DepthWrite=false`
allows a depth-testing pipeline in that pass.

Versions identify contents, not new storage. Reads must name the current produced
version at that authored position. A later producer, an overwritten version,
a missing declaration or an undefined load/root fails compilation. In-place
writes produce RAW, WAR and WAW dependencies against the same physical image.
No pass is silently reordered. Present/surface passes, explicit side effects,
and Export/History/Readback roots retain observable work; future-frame history
writes are roots even when this frame never reads them. Culling follows content
dependencies rather than retaining overwritten work solely for WAW/WAR edges.
Reference compilation disables culling while retaining the same validation.

`CompileOrderedGraph` returns a fixed `GraphReport` containing authored names,
source locations, culling flags, first/last live uses, and semantic dependencies
with RAW/WAR/WAW reasons. Pass index 32 denotes an external import/final-use
boundary; resource index 16 is the no-resource diagnostic sentinel. No GPU work
occurs during compilation. Successful compilation freezes setup. Discard a
rejected plan and rebuild if its declarations need changing.

`ExecuteOrderedGraph` rechecks frozen import revisions, packet readiness,
completion capacity and backend pass preparation before acquiring the current
surface. Target skip and preflight failure preserve graph/token for retry. Once
encoding begins, the graph is consumed even on failure; partial native/browser
work faults the session rather than claiming rollback. Accepted execution
publishes an R2 completion token and commits texture validity/use into the same
registry used by direct drawing. Discarding an unexecuted graph drops CPU leases
without GPU work. Shutdown/loss invalidates every graph incarnation.

The fully checked packet authoring is
[`tests/sdk_consumer/ordered_frame.h`](../../tests/sdk_consumer/ordered_frame.h),
used by both native pixels and the installed-SDK consumer. It compiles an unused
color pass, a depth-tested instanced scene, a Load pass with read-only depth,
and a scene-texture composite to presentation. Pipeline `Target` selects the
surface format or a matching RGBA8 offscreen format; dimensions are dynamic.

### Imports, history and transient objects

`GetTextureState` returns the registry's exact accepted-content snapshot:
non-wrapping revision, last submission dependency, semantic use, and color/depth
validity. `ImportGraphTexture` requires that snapshot and a final semantic use.
The compiler and executor reject stale snapshots, including changes made by
accepted direct passes or sampled direct draws. Pending same-queue dependencies
are ordered by GPU commands and barriers, without a CPU completion wait.
History uses persistent owned attachment textures, roots their produced version,
and imports the resulting snapshot on the next execution. The caller decides
which persistent image is the history source/destination; there is no automatic
frame-index rotation. Readback roots preserve a consumer's data; scheduling and
copy-out remain separate R2 buffer services. Texture readback is still deferred.

`TextureDescription.Attachment=true` allocates undefined color/private depth
with an empty upload. Direct `BeginRasterPass` switches attachments inside an
acquired frame with the same load/store checks. Direct frame end makes written
attachments sampleable for subsequent draws and records that final use. R2
command batches remain surface-only; offscreen packet sequences use R3 graphs
or explicit direct passes. Direct draws and R2 batches require a texture's last
committed/draft sampled use to cover its binding visibility. Choose `SampledBoth`
when exporting to arbitrary direct bindings; incompatible final color or narrower
sampled use returns `InvalidState` before a draw/batch submission. Graph execution
can transition these imports according to its declarations.

`CreateGraphTexture` acquires from an eight-object whole-description pool during
setup, before compilation. This early bounded acquisition lets callers build
ordinary immutable texture views/binding snapshots before graph preflight; it
is not interval-based physical aliasing. Every graph resource stays physically
distinct through execution. Pool reuse requires all graph/view/binding/packet
leases to end, validation callbacks to drain, and actual GPU completion. Idle
objects may be evicted when descriptions change, such as resize. Fresh/reused
contents start semantically undefined. The returned texture handle is borrowed:
Destroy rejects it, graph consumption/discard invalidates it, and a later lease
gets a new generation. Destroy owned views/binding sets when no longer needed
or they keep the pool object retained. Persistent imports cannot borrow another
graph's pool image; transient resources cannot escape through roots.

### Backend lowering and acceptance

Vulkan retains the legacy 1.1 render-pass path, privately cached compatible pass
objects and VMA attachment allocation. One mapper translates semantic texture
uses to color-output, shader-stage and depth-test barriers/layout transitions;
there are no ALL_COMMANDS barriers or newer feature requirements. WebGPU closes
rendering scopes before sampling an earlier attachment, uses creation flags and
private depth views, and relies on ordered queue validation/completion rather
than exposing fake barriers. Metal uses separate render encoders with explicit
load/store state. WebGL 2 uses owned FBOs, depth renderbuffers and owner-context
commands. Its portable top-left texture convention requires a private flipped
color blit at stored attachment boundaries; this copy cost is deliberate and
has no performance-improvement claim. Discard still remains semantically
undefined when a browser backend security-clears its storage.

Reference regressions cover packet retention, missing uses, forward/stale
versions, feedback, undefined loads/stores/roots, RAW/WAR/WAW reports, history
roots, direct/graph revision conflicts, retry/partial failure, identity/capacity
and pool retirement across GPU completion and view leases. Native pixel tests
exercise offscreen scene/UI/composite on Metal and Vulkan. The installed browser
SDK harness adds forced WebGPU/WebGL graph/reference variants, validates pixels,
compares complete optimized/reference images, and renders 120 frames. It retains
all R1/R2 failure, fallback and transfer scenarios. Pinned Chromium software-GPU
acceptance remains separate from physical-GPU/hosted-browser acceptance.

## Buffer compute and indirect rendering (R4)

Include `<ludus/graphics/rhi/compute.h>` for compute pipelines and limits, and
`<ludus/graphics/rhi/graph.h>` to execute dispatches. Vulkan, Metal and WebGPU
expose `DeviceInfo.Enabled.Compute` and `IndirectRendering` only when their
negotiated buffer profile is available. Vulkan requires compute support on its
existing graphics queue. WebGL 2 exposes neither. Requiring either feature
checks every Auto attempt before Ready; forced WebGL 2 fails before startup.
Preferences allow an application to select its own CPU/direct-draw variant after
Ready. The engine does not translate or emulate a compute kernel on WebGL 2.

### Buffers, kernels and limits

`BufferRole::Storage`, `StorageVertex` and `StorageIndirect` allocate buffers
with complete initial bytes. The latter two also support vertex/instance input
or indexed indirect arguments, respectively. Their role never changes. Initial
sizes and storage binding lengths are multiples of four; binding offsets obey
`GetComputeCapabilities().StorageOffsetAlignment`. Storage buffers are bounded
by the lower enabled range and 16 MiB. Layouts retain at most four storage
bindings and eight total buffer bindings. Uniform buffers can also be read by
compute. Each storage entry uses `StorageRead` or `StorageReadWrite`,
`RasterVisibility::Compute`, and a reflected minimum element stride. Storage
textures, raster-stage storage access and CPU update/upload of existing storage
buffers remain deferred.

The installed shader helper accepts one compute entry:

```cmake
ludus_compile_shader(TARGET my_renderer NAME cull
    SOURCE shaders/cull.slang COMPUTE computeMain)
```

The generated `ludus::shaders::cull::Compute()` description selects the native
MSL/SPIR-V or browser WGSL artifact, with target-specific reflection and verified
workgroup dimensions. No GLSL ES compute artifact is generated. The bounded
helper accepts structured-buffer scalar32, vector2 or vector4 elements
(`float32`, `uint32`, `int32`); arbitrary structures, resource arrays and
specialization-controlled local sizes are outside this helper profile.
`CreateRasterShader` accepts its Compute stage, then `CreateComputePipeline`
retains that shader and its exact group-zero layout. Poll Pending creation and
check every result, as with raster resources. Pipeline creation, binding
snapshots and destruction share the existing RHI registry and R2 leases.

Local dimensions are nonzero, at most 256/256/64 along x/y/z, and at most 256
invocations in total, further bounded by enabled device limits. Dispatch counts
are nonzero and at most 65535 on each axis, again clamped to enabled limits.
`GetComputeCapabilities` is authoritative. The renderer checks these limits and
reflection; bounds checks, shader-local synchronization and race freedom are
kernel obligations. There is no indirect dispatch or separate asynchronous queue.

### Ordered dispatches and indirect commands

Set `GraphPassDescription.Dispatch` to one `ComputeDispatch`; omit raster draws
and attachments. The graph copies and retains the packet. Multiple dependent
dispatches need separate authored passes. Declare every buffer binding, including
unused extra entries, with matching byte range and Compute visibility. Read-only
storage names the current version. Read/write storage reserves the next version
with `NextGraphVersion`; it also depends on the preceding contents, preserving
bytes outside the declared write range. Complete creation bytes establish initial
contents. Whole-buffer RAW/WAR/WAW tracking remains conservative; there is no
subrange scheduling or physical aliasing. Export/History/Readback buffer roots
preserve observable compute work. Frozen imports reject accepted writes from
another graph, and successful submissions advance the shared content revision.

An indirect `RasterDraw` supplies a ready `StorageIndirect` buffer and a
four-byte-aligned `IndirectOffset` containing `IndexedIndirectArguments`.
Declare those twenty bytes with `GraphAccessMode::Indirect`. The same packet
still supplies mesh/index/instance slices and nonzero maximum `IndexCount` and
`InstanceCount`, which are validated before execution. The producing kernel
must keep actual counts within those maxima and write zero `FirstIndex`,
`BaseVertex` and `FirstInstance`; packet `IndexOffset` selects the mesh. Zero
actual instance count culls the draw. The GPU arguments are not read back on the
rendering hot path, so kernel correctness is part of this contract. The profile
issues one indexed indirect command per packet, without indirect count buffers.

Vulkan lowers declared buffer uses to compute shader, vertex input, uniform
shader or draw-indirect access/stage barriers on the existing queue. It retains
writer visibility across multiple readers and subsequent R2 readback/copies.
Metal uses tracked resources and separate compute/render encoders; WebGPU uses
storage/vertex/indirect creation flags and ordered compute/render usage scopes.
Neither exposes invented native barriers. Completion, deferred destruction,
preflight retry and partial-encoding failure follow R2/R3.

### Acceptance fixture

The shared [`ordered_frame.h`](../../tests/sdk_consumer/ordered_frame.h) fixture
has an authored CPU culling function and the
[`cull.slang`](../../tests/sdk_consumer/shaders/cull.slang) GPU variant. A single
invocation compacts four candidates into two visible instances and writes one
indexed indirect command. GPU output buffers start at zero. Native tests read
back the generated command through R2 and compare the complete rendered image
against the CPU/direct variant. The installed browser consumer also reads back
the command. Its harness compares compute/reference images to the CPU oracle,
checks WebGL 2 and Auto fallback, rejects required compute on WebGL 2 before any
frame, and preserves the existing readiness/loss/transfer scenarios over 120 frames.

Reference tests cover negotiated failure, layouts/local/dispatch bounds, missing
uses, mutable-buffer RAW/WAR/WAW and history roots, culling, frozen revisions,
retry, retention through completion and failure after encoding begins. Metal and
pinned Chromium software-GPU checks run locally; Linux Vulkan, formal pinned
SPIR-V validation and supported-host sanitizers are CI gates. Browser software
GPU evidence remains separate from physical GPU/browser qualification. This
fixture establishes correctness; it makes no performance improvement claim.
