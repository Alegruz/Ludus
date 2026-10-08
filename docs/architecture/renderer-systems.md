# Low-level renderer systems architecture

Status: Proposed design, October 7, 2026, audited against `8162d93`.
This is an implementation plan, not a shipped renderer or a measured speedup.
The initial decisions below were saved before opening the Gems article map.
The [Gems review](renderer-systems-gems-review.md) records the subsequent article
readings and the concrete revisions now incorporated below. Existing RHI,
camera, resource and text documents retain their respective detailed contracts.

Build a small data-oriented `GraphicsRenderer` over the existing
[RHI and GraphicsDevice design](rhi-gdi.md). Use immutable frame snapshots,
explicit output views, versioned mesh/material resources, bounded submissions,
and a fixed readable pass sequence. Start with CPU visibility and conventional
forward shading. Add clustered lighting and GPU visibility as independently
measured capability paths; keep a direct-draw reference path for debugging.

“Best” means predictable correctness and useful performance for a specified
workload. It does not mean that one technique wins on desktop, integrated GPUs,
tile GPUs and browsers. An AAA architecture needs scalable contracts and strong
inspection tools; it need not begin with every expensive rendering technique.

## Initial decisions before article review

| Decision | Rationale |
| --- | --- |
| Renderer owns scene policy; GDI owns work planning; RHI owns native state and lifetime | Preserve existing owners rather than introduce a second device wrapper |
| Compact immutable snapshots and packets, one serialized submission owner | Separate scene mutation from resource use and make captures explainable |
| Stable view identity distinct from camera, target, viewport and logical screen | Support split screen, editor previews and offscreen work without global current state |
| CPU frustum culling, material sorting and adjacent instancing first | Small portable correctness oracle and useful initial optimization |
| Forward raster baseline; clustered forward extension | Share opaque and transparent lighting and retain a simple WebGL path |
| Offline shader packages, explicit material templates and immutable instances | Bound permutations and remove shader compilation/name lookup from draws |
| Ordinary ordered passes with declared uses | Precise hazards and pooling without an opaque scheduler |
| Completion-scoped upload/frame storage and transactional version publication | Avoid in-flight overwrites and half-published reloads |
| Linear-light shading/composition with explicit final output encoding | Prevent gamma errors and inconsistent UI blending |
| Triangle-expanded debug lines and painter-ordered glyph/sprite quads | Portable width and ordering with reusable batching |
| Baked lightmaps/probes plus dynamic direct lighting | Useful quality without requiring real-time global illumination |
| Reference modes, bounded traces and workload gates before advanced features | Optimize with evidence while retaining inspectable behavior |

## Repository fit and current limits

Read [AGENTS.md](../../AGENTS.md), [ADR 0003](../decisions/0003-standard-library-usage-policy.md)
and the [foundational include model](foundational-headers.md) before implementation.
Use Ludus aliases and owned containers/algorithms, explicit results, `noexcept`
on infrastructure paths, and small public headers. New borrowed-view APIs belong
to Ludus; the R1 `std::span` exception does not automatically extend to Renderer.
No public native API types, runtime STL caches, exception-based dependency or
global registration constructors are implied by this proposal. Document new
public contracts with Doxygen and source attribution beside affected code.

| Inspected owner | Shipped boundary | Renderer implication |
| --- | --- | --- |
| [R1 portable raster](../development/fullscreen-rendering.md#portable-raster-r1) | Vulkan, Metal, WebGPU and WebGL 2; indexed instanced triangles, immutable resources, one acquired color target and private depth | A small flat/unlit consumer can use it now; general pass attachments, streaming and modern depth policy need explicit extensions |
| [RHI/GDI architecture](rhi-gdi.md) | R0/R1 slices exist; most GDI services and R2–R5 are proposed | Do not treat pass compilation, upload rings or compute as available APIs |
| [Camera systems](camera-systems.md) | C0 evaluation is implemented; advanced rigs/history remain proposed | Consume `CameraSample`; renderer owns projection, jitter, render-origin conversion and history |
| [Text](text-font-rendering-research.md) and [FontSystem](../../modules/text/include/ludus/text/font_system.h) | Native CPU shaping and grayscale rasterization; bounded run contract, no automatic paragraph layout; atlas/GPU work remains pending in the [F0–F6 ledger](../../.kiro/specs/text-font-rendering/tasks.md) | Reuse CPU text; bounded atlas and GPU text adapter need implementation and backend evidence |
| [Resources](resource-management.md) | Content/audio portions exist; general coordinator and graphics streaming are proposed | Do not block the first scene on a universal asset manager |
| [Frame update](frame-update.md), [world](game-world.md), [UI](ui.md) | Separate simulation, presentation and UI ownership | Extraction adapters sit above Renderer; it must not depend on an ECS or UI toolkit |

R1 currently clears depth to one and supports less-than depth. FoundationMath's
intended 3D convention is reverse-Z `[0,1]`. Never silently pair the reverse-Z
camera projection with that state. The first 3D phase must add explicit clear
and compare state, then prove near/far ordering on all supported backends.
Existing R1 consumers keep their documented behavior. Similarly, its 16 records
per kind, RGBA8-only single-mip sampling and one binding group are compatibility
limits, not an adequate streaming/lighting contract.

## Owners and module shape

```text
Game/World/Editor adapters     UI layout       GameplayCamera        Text CPU
             \                   |                  |                  /
              +------ immutable scene, view and overlay inputs ------+
                                      |
                            GraphicsRenderer
                     extraction-free packets and effects
                                      |
                            GraphicsDevice
                   pass plans, frame storage, uploads
                                      |
                              GraphicsRhi
                native objects, accepted uses, completion
                                      |
                     Vulkan / Metal / WebGPU / WebGL 2
```

This diagram is data flow, not permission for RHI to depend on gameplay.
Renderer receives ordinary values and resource leases through application
adapters. GameplayCamera and Text remain independent CPU owners. Start with one
renderer instance per device, multiple views and one owner thread. Proposed
`modules/graphics/renderer/include/ludus/graphics/renderer/` contains small scene,
view, material, debug and frame contracts; private tables, pass implementations,
shader bindings and caches stay in `src/internal/`. Native completion/retirement
remains in RHI and is serviced through GDI; never duplicate its handle registry.

Use ordinary functions and a few private records. Do not add a polymorphic
renderable per object, a public pass class per effect, or a universal render
command bytecode. Pass implementations accept their concrete data and emit RHI
encoder work. The first implementation can record serially; native parallel
recording is an optional private optimization with completion-scoped pools.

Following Donald Revie's separation of representation and rendering context
(*GPU Pro 3*, V.4, sections 4.4–4.8), store complete pass inputs rather than a
mutable global context stack. Material packages explicitly name legal depth,
shadow, color and debug variants. Resolve parameters at cook/load, and bound
variant combinations before admission. Thanks to Revie for this distinction and
the account of configuration/optimization costs; the [review](renderer-systems-gems-review.md)
explains why Ludus does not adopt his general object/XML pipeline.

### Required RHI/GDI extensions

| Needed contract | Owner and migration boundary |
| --- | --- |
| Clear depth, compare/cull state, viewport and scissor | RHI explicit raster state alongside the unchanged R1 compatibility behavior |
| Color/depth attachment views, pass begin/end and resolve intent | RHI encoders; GDI declares uses/imports and preserves shared-target rectangles |
| Scheduled range uploads, mutable instance/constants and R8/mip sampling | RHI validates operations; GDI owns tickets, bounded staging and readiness |
| Reflection/layout growth and ready pipeline requests | Shader tools validate targets; GDI caches/prewarms; Renderer chooses material variants |
| Compute/storage and indirect operations | Independently negotiated RHI capabilities; Renderer has an authored direct/CPU alternative |
| Acceleration-structure descriptions/builds, shader query support and exact retained versions | Optional RHI extension; GDI declares build/read/scratch hazards; Renderer owns ray-scene selection and transport |

Each extension requires native and browser evidence under the pinned toolchains,
including explicit unsupported results for optional features.
Do not increase current R1 limits globally as an incidental renderer refactor.
Keep first consumers within existing limits or explicitly fail their requested
profile. Detailed retention, state and submission rules remain owned by
[RHI/GDI](rhi-gdi.md#resource-and-command-contracts).

## Frame contract and concurrency

1. Service completions, pending resource/pipeline validation and bounded uploads.
2. Admit complete resource candidates at the frame boundary. Acquire exact
   resource versions for this frame; incomplete candidates retain the old version.
3. Accept immutable scene, view and overlay snapshots. Validate identity, ranges,
   finite values and configured capacities before publication.
4. Build render views, CPU visibility/LOD and draw lists from those values.
5. Resolve ready pipelines/bindings and select declared effect variants.
6. Build ordered passes; GDI validates declared subresource uses and plans imports.
7. Record/submit through one RHI owner. On acceptance, transfer retained uses to
   its authoritative ledger; on failure discard unsubmitted reservations safely.
8. Commit temporal state only for accepted render work under the view's policy.
   Presentation outcome is tracked separately; skipped acquisition does not
   advance rendered history. Recycle frame storage only after all CPU and GPU users.

Inputs are copied into owned bounded storage before a submission API returns,
unless it explicitly accepts a transferable lease. Worker tasks receive disjoint
packet ranges and exact leases; their joins are CPU readiness, not GPU retirement.
No callback borrows an application's stack or addresses into a mutable world.
The render snapshot can contain interpolated transforms while simulation remains
fixed-tick. Avoid a dedicated render thread initially: measure extraction,
recording, joins, memory and input-to-display latency before adding buffering.

Creation requires a null output. Invalid description/handle, unsupported feature,
not-ready input, out-of-memory, budget exhaustion and device loss are distinct
outcomes. Failed admission preserves outputs and the previously usable version.
Bounded diagnostics carry source/view/object/pass IDs. Optional debug geometry
may be dropped with a counter; essential scene work must not silently disappear
because a packet buffer overflowed. A frame admission failure returns a status
and preserves the last valid presentation where the surface permits it.

## Primitive submission, visibility and batching

Use a persistent mesh resource plus frame-local instance data. A mesh owns
immutable vertex/index chunks, vertex layout, topology, bounds, submesh ranges
and LOD metadata. A scene item supplies mesh/version, submesh, material/version,
current/previous transform, visibility flags, stable object ID and sort layer.
Visibility flags describe policy; they must not expose native render state.
Large world positions use `float64`; convert to checked origin-relative
`float32` per view. Singular normal transforms, NaNs and unrepresentable bounds
are explicit invalid inputs, with a diagnostic and documented fallback.

Build dense arrays, reuse reserved capacity and keep authoring names out of hot
packets. Initially perform scalar conservative frustum/bounds tests and authored
screen-size LOD with hysteresis. Add spatial indexing, SIMD and occlusion only
after measured need. Animated/deformed bounds must conservatively contain the
posed geometry; delayed visibility must never hide a newly visible object.
Shadow visibility is computed for the light's view independently of main-view
visibility. Streaming hints use visibility but never synchronously load in culling.

Sort opaque packets by pass, coarse depth bucket, pipeline, binding/material,
mesh and stable source ordinal. Compare depth-first versus state-first sorting
on actual scenes; no hardcoded bit allocation establishes an optimal key.
All key collisions resolve with explicit record comparisons. Masked geometry
preserves coverage rules across depth/shadow/color variants. Transparent 3D draws
use stable back-to-front bounds depth with explicit layer overrides; intersecting
surfaces have documented approximation limits. UI and 2D overlays preserve
painter order and merge only adjacent compatible packets.

Instance adjacent draws only when mesh/submesh, layout, pipeline, bindings,
pass state, scissors and blend semantics agree. Instance range limits and
transform handedness/cull mode must be honored. Mesh-local indices and deliberate
buffer slices preserve R1's zero base-vertex/first-instance portable contract;
later native paths may use capabilities without changing scene identity.

Transient triangles/quads use a reserved upload allocator with checked sizes and
per-frame quotas. Persistent terrain, sprites and dynamic meshes use the same
packet path, with geometry preparation in their owners. No per-line/per-glyph
resource creation or virtual call. GPU culling/indirect draws later consume the
same logical scene records and expose the same source IDs; direct CPU rendering
remains available as a comparison mode.

## Views, viewports and virtual screens

Keep these concepts distinct:

| Concept | Meaning and lifetime |
| --- | --- |
| `ViewId` | Renderer-owned stable identity plus generation; one logical output's settings/history |
| Camera sample | Pose/lens input for this frame, not a render target or GPU owner |
| Physical target | Surface image borrowed for the acquired frame, or an owned offscreen texture version |
| Viewport | Top-left physical pixel rectangle and explicit canonical depth range |
| Scissor | Intersection of drawable bounds and clipping rectangles; empty is a no-op |
| Virtual screen | Authored logical extent and fit/fill/stretch/integer-fit policy mapped into a physical viewport |
| Render extent | Shading resolution, optionally scaled independently of presentation/overlay resolution |

Resolve against the **actual acquired extent**, not a requested window size.
Use one mapping for drawing and pointer coordinates: translate to viewport,
subtract fit offsets, divide by scale; reject letterbox bars unless the caller
requests clamping. Keep logical UI units, physical pixels, texture texels and
world units explicit. Round viewport edges once with checked integer arithmetic;
derive neighboring split-screen edges from shared boundaries to avoid cracks.
Scissors use consistent half-open bounds. DPI is input to the logical/physical
mapping and glyph raster size, not a second hidden multiplier.

For logical extent `(Lw,Lh)` and physical viewport `(x,y,W,H)`, fit uses
`s = min(W/Lw,H/Lh)` and centered offset `((W-s*Lw)/2,(H-s*Lh)/2)`.
Map logical point `p` to `(x,y) + offset + s*p`; invert that same mapping for
pointer input. Validate positive finite dimensions before division. Fill uses
the maximum scale; stretch has separate axis scales. The mapping record retains
the final rounded content rectangle so drawing, clipping and hit tests agree.

Zero-sized or hidden views skip work without allocating histories. Fit preserves
aspect with bars; fill crops; stretch is explicit. Integer-fit uses the largest
whole-number scale that fits; when none fits, select fractional fit and expose
that fallback in the mapping record. In a shared target, each view owns a rectangle;
load/store/clear semantics must preserve neighbors. Whole-attachment clears may
not masquerade as per-view clears. Compose offscreen views when scissored clears
or distinct lighting/extents cannot be supported correctly in one pass.

The renderer owns unjittered matrices for culling/picking and jittered matrices
for temporal shading. Use right-handed +Y-up/-Z-forward math, reverse-Z `[0,1]`
after the explicit RHI extension, and per-backend viewport/depth conversion.
History keys include ViewId generation, render extent, projection convention,
origin epoch and effect/layout revision. Reset on camera cuts, restart, incompatible
resize/projection changes and failed history production. Origin rebasing requires
consistent current/previous transforms or reset; it is not a camera teleport.
Dynamic resolution never changes simulation aim or logical UI coordinates.

## Textures, surfaces and GPU memory

Use three separate owners: content texture versions, GDI-managed transient
attachments, and RHI-borrowed acquired surface images. A surface is not an asset.
Texture descriptors declare dimension, format/encoding, extent, mips, layers,
samples and legal usage. Texture views name checked subresource ranges and
compatible interpretation; sampler state is independent. Reject unsupported
combinations before frame recording using per-format capability matrices.

Offline cooking generates semantic mips: color in linear light, normal-map
renormalization, mask coverage preservation where requested, and separate data
texture treatment. Choose compression per target profile, with a verified
uncompressed variant; a backend name never guarantees a codec. Streaming publishes
only initialized mip ranges with legal sampling clamps and exact immutable
binding snapshots. Begin with whole-texture replacement; partial uploads need
RHI range hazards and a residency protocol before they become a feature.

GDI upload tickets distinguish copied source ownership, scheduled destination
work and sampling readiness. Caller bytes may be freed only under that declared
source contract; staging ranges live through their backend consumption obligation.
Budget upload bytes/work per frame and apply backpressure. Never block rendering
on disk, synchronous readback or device-idle to make routine updates convenient.

Separate logical release, CPU lease release, submitted completion and physical
memory retirement. Descriptors/atlas regions cannot be reused while earlier
packets reference them. Report live, pending-upload, transient, cached and
pending-retirement bytes separately. Budgets include temporary old/new overlap
during hot reload, aligned storage and multiple frames in flight.

Start with transient pooling by exact compatible descriptor and completion proof.
Attachment aliasing is a later native path whose lifetime overlap and memory
requirements are validated; distinct WebGPU textures are not promised physical
aliasing. Histories and cached shadows are persistent imports. Every pass states
load/clear/discard and store/resolve intent, resource ranges and readers/writers.
GDI compiles those uses in the author's order; RHI commits state only on accepted
submissions. Undefined transient contents cannot be sampled.

Arseny Kapoulkine's *GPU Zen 2* article 4, sections 4.4–4.6, reinforces this
ordered plan and explicit attachment intent. Thanks for the pipeline/attachment
tradeoffs: precreate the legal material states, and use pass-integrated resolve
when supported and sufficient. A streamed destination enters the pass plan with
a readiness dependency; streaming scheduling itself remains a GDI service.
Ludus does not infer safety from a discard hint or assume all GPUs benefit equally.

Resize creates complete replacement target sets, publishes at a boundary and
retires old sets after use. On device loss invalidate all GPU incarnations and
history, keep recreatable CPU descriptions, and explicitly restart/rebuild.
Use fallback textures/materials only if ready and compatible. An invalid shader
or surface cannot trigger an undocumented backend switch.

## Materials, shaders and pipeline control

A material template declares a small shading family, parameter schema, legal
texture semantics, render domain and static feature options. A material instance
holds immutable values and exact texture versions. Instance edits create a new
candidate and publish transactionally; old packets retain the old bindings.
Begin with unlit, metallic/roughness lit, masked and premultiplied transparent
families. Limit two-sided and specialized effects to justified variants.
Runtime material graphs and arbitrary per-object shader generation are deferred.

Retain Ludus's pinned Slang/tool chain and SPIR-V, MSL, WGSL and GLSL ES artifacts.
Cook explicit reflection with numeric bindings, stage interfaces, parameter
offsets, matrix packing, sizes and alignment. Validate each emitted target;
equal total buffer sizes do not prove matching members. CPU constants use
generated serialization/packing at the boundary rather than accidental C++
struct layout. Shader source/artifact hashes and compiler/schema versions belong
in the cooked package; unsupported profiles reject cooking or select authored
variants, never silently omit fields.

View/Material/Draw are update-frequency conventions, not a promise of three
physical descriptor groups on the present R1 interface. Map logical data to each
profile's verified layout. Cache immutable binding snapshots, allocate dynamic
constants from aligned completion-scoped storage, and intern compatible layout
identities through GDI rather than relying on structurally similar handles.

Pipeline keys include shader versions, layout identity, vertex input, topology,
attachment formats, samples, raster/depth/blend state and declared specialization.
Viewport size and dynamic constants normally do not belong in the key. Hash
collisions compare descriptions. Enumerate the legal static variant space;
record used variants and prewarm before activation. Pipeline readiness is
asynchronous where the backend permits, with a compatible ready fallback or
explicit not-ready status. Cold first-use stalls remain visible telemetry.
Persist native caches only with validated device/driver compatibility; cache
failure is recoverable. Never claim offline shader compilation eliminates all
driver pipeline work. Reload swaps package, layouts, bindings and pipelines as
one validated version set; failed reload leaves the prior set usable.

## Lighting and the default pass sequence

Use a conventional forward baseline with bounded CPU-selected per-object lights.
Selection is stable and deterministic, with a declared maximum and an overflow
counter/quality policy. Add clustered forward on compute/storage-capable profiles
when it outperforms that baseline for many lights. Cluster bounds and light lists
must be conservative, capacity-checked and inspectable; overflow uses a declared
bounded fallback rather than corrupting memory or silently dropping arbitrary
lights. WebGL 2 retains the CPU light-list variant. Deferred shading, visibility
buffers, bindless, mesh shaders and ray tracing are separate options, not startup
requirements; their bandwidth/material/platform costs need workload evidence.

The review of Billeter, Olsson and Assarsson's “Tiled Forward Shading”
(*GPU Pro 4*, II.4, sections 4.4–4.8) adds a correctness rule: transparent lighting
cannot reuse light rejection derived only from opaque occupied depths. Start
with a regular view-space 3D grid covering the declared near-to-lighting-far range
and conservative light-volume intersections, independent of opaque occupancy.
Infinite-far camera projections still require a finite lighting range and a
declared farther-surface fallback. Later opaque-only depth/normal pruning uses
separate lists. Empty sky samples and every MSAA sample need explicit handling.
Thanks to the authors; their historical timings are not Ludus performance evidence.

Harada, McKee and Yang's “Forward+” (*GPU Pro 4*, II.5, sections 5.3 and 5.6)
motivates compact culling bounds separate from shading properties, occupancy
visualization and independent shadow budgets. Use a fixed-stride capped list per
cluster initially. Bounds-check both the local append and total buffer extent;
store the **written** count, never an unbounded attempted count. Saturation sets
an overflow flag and makes the shading variant select a stable bounded fallback
list prepared on the CPU and uploaded before assignment; this needs no synchronous
GPU readback. Async telemetry reports affected clusters and quality reduction.
Unshadowed lights and shadow assignments have independent limits. Compact
prefix-sum lists, light BVHs and 2.5D masks are
later alternatives. Thanks to the authors; do not copy their demonstration's
fixed workgroup/list capacities or make opaque depth masks authoritative for
transparent shading.

Use linear RGB shading, a documented GGX metallic/roughness BRDF with energy
conservation, roughness floors and numerical guards. Define directional light
illuminance in lux and point/spot luminous intensity in candela; author lumens
only through an explicit solid-angle conversion. Radius/cutoff and temperature
conversion are bounded; world distance is in meters. An emissive material does
not automatically illuminate surrounding objects. Exposure belongs to each view;
the initial mode is manual and stable. BRDF details, artistic approximations and
reference fixtures must be specified in the lighting implementation change.

Static direct lighting may be baked into lightmaps, while irradiance probes
approximate indirect diffuse for moving objects; filtered reflection environments
serve indirect specular. Cook versions carry bake-space transforms, UV/chart data,
normalization, scene/dependency hashes and encoding. Keep baked direct and dynamic
direct contributions distinct to avoid double lighting. A moved baked object/light
invalidates its bake or uses a documented dynamic fallback. Lightmap packing,
probe placement and content quality belong to cook tools; rendering only consumes
ready versions. Missing bakes select a declared neutral/ambient fallback.

Dynamic shadows start with one directional cascaded map and a bounded local-light
atlas. Stabilize cascade texel placement, derive splits from unjittered views,
reserve filtering gutters, bound bias in light-space/receiver units and expose
split/bias/occupancy debug modes. Shadow maps may deliberately use conventional
depth with an explicit sampler/compare contract even when the main view uses
reverse-Z. Static shadow reuse keys include caster/material/light revisions;
dynamic casters invalidate affected regions. Point/spot map costs count against
an explicit per-view/frame budget. No cache claims correctness from “light did
not move” alone.

An initial lit view follows this author-visible sequence:

| Stage | Inputs and outputs |
| --- | --- |
| Upload readiness | Exact prepared versions become available before their first use |
| Shadow passes | Light views and conservative caster lists produce depth maps |
| Optional depth prepass | Opaque/masked geometry produces depth; enabled only by measured benefit |
| Optional cluster assignment | View/light bounds produce capped cluster lists |
| Opaque/masked forward | Geometry, direct lights, bakes and environment produce linear scene color/depth |
| Sky and transparent | Declared depth behavior and ordered blending into scene color |
| Scene effects | Authored AO/temporal/other variants with explicit inputs/history, initially disabled |
| Exposure/tone mapping | Scene-linear HDR, when supported, becomes display-linear color |
| Debug/UI/text | Display overlays compose in linear display space at presentation resolution |
| Output | Encode once according to the actual attachment/output contract, then present |

World-space debug may instead compose into scene color **after** temporal effects
and history production, before exposure/tone mapping; use retained scene depth
for testing. Display overlays are display-referred. Tag the domain so neither is
tone-mapped twice and debug primitives do not enter temporal history.
HDR attachments/blending and multisampling require format/capability checks;
the portable baseline has an authored LDR path. Hardware sRGB output and shader
encoding are mutually exclusive. On a nonlinear unorm target lacking linear
blending, composite into a supported linear intermediate then encode once.
Antialiasing starts with a simple spatial variant or verified MSAA path; TAA
is a later feature needing motion vectors, jitter and history rejection tests.

## Ray tracing and path tracing

Ray tracing supplies intersection and visibility queries. Path tracing uses those
queries to estimate light transport through sampled paths. A ray-traced shadow
does not require a path tracer; a path tracer does not require a native ray
pipeline or recursive hit shaders. Treat traversal, transport, sample reuse and
reconstruction as separate private components with shared scene contracts.

| Rendering mode | Role and initial scope | Cost and fallback policy |
| --- | --- | --- |
| Raster | Portable gameplay and direct-draw correctness path | Existing portable profile remains independently usable |
| Hybrid rays | Raster primary visibility plus selected ray visibility, reflections or indirect diffuse | Effects have independent ray/build/history budgets and authored raster/bake/probe alternatives |
| Progressive reference path tracing | Frozen-scene editor/debug captures; direct and indirect transport for supported materials | Accumulate independent samples without denoising or temporal reuse; explicit unsupported status on devices/content outside the profile |
| Real-time path tracing | Optional high-end gameplay mode with moving scenes | Requires measured sampling/reconstruction quality and tail latency; no universal frame-rate promise |

One `GraphicsRenderer` accepts the same scene/view/material versions for these
modes. Each mode composes a concrete pass plan; do not introduce another asset
manager, camera stack or GPU lifetime registry. Hybrid transport replaces a
specified lighting term rather than adding another copy of it. For example,
ray-traced indirect diffuse replaces the selected probe/lightmap indirect term.
Full path tracing evaluates supported emitters and scattering directly; it does
not also add baked lighting, ambient occlusion or prefiltered IBL approximations.
Record any intentionally mixed approximation in the capture's rendering profile.

### Traversal capability and backend boundary

Start with opaque triangle acceleration structures and shader ray queries on
verified native devices. Expose distinct capabilities for structure build/update,
inline queries, dedicated ray pipelines, procedural intersections and motion;
also query limits, alignment, scratch requirements and shader target support.
`Unsupported`, `NotReady`, capacity failure and device loss remain explicit.
Ray hardware, compute availability and bindless support are separate facts.

Khronos's [ray tracing guide](https://docs.vulkan.org/guide/latest/extensions/ray_tracing.html)
distinguishes shared acceleration structures from query and pipeline execution.
Queries fit ordinary shader passes; dedicated stages offer different scheduling
choices. Begin with query-based compute effects and a loop-based path integrator.
Keep nearest accepted hit and early-exit visibility as separate shader operations.
Add native ray pipelines only when measured traversal/shading divergence or a
required intersection program warrants them. Shader binding tables, native
addresses, hit-group indexing and recursion limits then remain backend details;
do not force Metal's model into a Vulkan-shaped public table abstraction.

The existing Vulkan subset does not establish support: negotiate the necessary
extensions/features/dependencies in a separate RHI profile and validate shader
compiler output. [Metal acceleration structures](https://developer.apple.com/documentation/metal/ray-tracing-with-acceleration-structures)
also require device/OS/compiler evidence; the pinned MSL 2.3 raster target is not
proof that a proposed ray shader works. A newer ray target needs an explicit
toolchain/profile change. Ludus's current WebGPU/WebGL paths have no hardware
ray contract. A bounded software BVH compute experiment is possible after compute
support, but it is another implementation with its own cost, not a hardware
capability or a required browser fallback. Do not hide CPU readbacks behind it.

### Scene and acceleration-structure ownership

Renderer's private ray scene references exact mesh, deformation, transform,
material and texture versions. BLAS (bottom-level acceleration structures)
describe geometry; TLAS (top-level structures) instance those geometries. RHI
owns native structures and authoritative retention; GDI schedules their work.

- Reuse one static BLAS per compatible mesh/geometry version across instances.
  Key it by layout, topology, build flags and opacity classification. TLAS records
  contain stable instance IDs, masks, transforms and bounded shading-table indices.
  A material reload that changes opacity may require a different structure version.
- The ray scene includes offscreen shadow casters and reflection/indirect geometry.
  Camera frustum culling of raster packets must not remove ray-visible instances.
  Streaming regions, ray masks, trace distance and proxy/LOD selection are explicit
  quality policies. Reference captures require a frozen complete declared scene;
  missing geometry/residency is a status, not an invisible approximation.
- Raster and rays use the same selected pose and object transforms. Skin/morph
  evaluation completes before building/refitting its BLAS and before raster use.
  Refit only when supported and topology/build flags permit it. Rebuild after
  measured traversal degradation; animated geometry can make repeated refits slow.
  Bound rebuild work and expose deferrals or quality reductions.
- Keep BLAS geometry local and use a declared render origin for TLAS, rays and
  shading. Multiple views can share a compatible TLAS or use different origin
  versions. Validate nonuniform/mirrored transforms and geometric-normal winding;
  singular transforms fail admission. Rebasing invalidates incompatible histories.
- Declare deformation-write to build-read, BLAS-build to TLAS-build and final
  build-write to trace-read dependencies. Scratch reuse waits for completion of
  all overlapping builds, not the end of CPU recording. Cross-queue execution
  requires explicit RHI ownership and completion evidence.
- Compaction or relocation publishes a new ready version after its copy/build
  dependency. Retain every old structure and shading resource until its accepted
  uses complete; TLAS versions retain the BLAS versions they reference. Never
  patch an in-flight TLAS address or replace its shading table in place. Build
  inputs remain alive through builds and longer when later shading reads them.

Start serialized on one queue with static geometry and a full TLAS build when
instances change. Add supported updates, amortized static builds, compaction and
queue overlap individually after measuring build time, traversal time and peak
overlap memory. AS caching is session-local initially; native serialization needs
an explicit device/driver compatibility and content validation contract.

### Material and intersection parity

Cook one surface description into raster evaluation and ray evaluation/sampling
variants. Private shader functions define BSDF evaluation, sample and probability
density with matching lobe weights and conventions; a shared name alone does not
prove equivalence. Validate albedo, roughness, normals, UVs, emission, handedness
and texture encoding with raster/ray AOVs (auxiliary output images).

The first reference profile is RGB, triangle surfaces, diffuse/GGX reflection,
opaque and explicitly alpha-masked materials, finite area emitters, analytic
point/spot/directional lights and an environment. Add transmission with Fresnel,
IOR/medium tracking and total internal reflection under a separate tested profile;
volumes, subsurface scattering, hair and spectral transport are further features.
Raster alpha blending is not a physically specified transmission model. Unsupported
materials make a capture incomplete or fail its requested reference profile;
an error-material preview must never be labeled a reference image.

Retain geometric and shading normals separately, including their front/back-side
meaning. Opacity acceptance uses the declared texture/threshold/LOD policy in
both closest-hit and shadow queries; an opaque early-exit query cannot be reused
for alpha-masked casters without candidate filtering. Two-sided masks and
transmission visibility are distinct policies. Ray texture filtering has no
implicit raster derivatives: initially use an explicit fixed-mip reference policy,
then add verified footprints/ray cones as a named filtering profile. Residency
and mip-policy changes invalidate accumulation; compare like filtering models.

Carsten Wächter and Nikolaus Binder's “A Fast and Robust Method for Avoiding
Self-Intersection” (*Ray Tracing Gems*, chapter 6, sections 6.2–6.3) motivates
barycentric hit reconstruction and scale-aware offsets along the oriented
**geometric** normal. Thanks to the authors. A global `0.001` offset, shading-normal
offset or primitive-ID rejection alone is insufficient. Verify the chosen method
under Ludus's transforms/compiler precision; the chapter itself identifies thin
crevices and extreme transforms as limitations. Define normalized directions,
ray interval units and endpoint handling, including offsetting both endpoints
for area-light visibility segments. Expose offset diagnostics instead of scene
authors tuning a hidden epsilon.

### Progressive reference integrator

Jakub Boksansky and Adam Marrs's “The Reference Path Tracer” (*Ray Tracing Gems II*,
chapter 14, especially 14.3.1–14.3.7) supports integrating an inspectable reference
into the existing renderer. Thanks to them: retain their staged primary-ray/AOV
checks, shared scene data and simple iterative transport, while replacing their
DXR-specific setup with Ludus's optional traversal contract. This is a planned
reference implementation, not a claim that their sample or companion code shipped.

1. Freeze a scene snapshot and all exact resource versions. Generate pinhole
   rays from the same camera/projection and viewport conventions as raster,
   sampling within pixel footprints. Compare unjittered center rays first; camera
   near/far clipping is an explicit primary-visibility policy, distinct from
   secondary-ray ranges. Thin lens and shutter/motion integration are extensions.
2. Carry radiance, throughput, sample state and previous sampling information in
   a bounded iterative loop. At a hit, evaluate emission, sample a light and its
   visibility, then sample the BSDF to continue. An escaped path evaluates the
   environment. Start with one light sample and one continuation per vertex.
3. Combine light and BSDF sampling with multiple importance sampling (MIS).
   Light selection probability multiplies the light's conditional density;
   convert area and directional densities to the same measure before weighting.
   Evaluate the competing density for BSDF-hit emitters/environment and handle
   delta lights/lobes separately. This avoids double-counting emission and
   excessive noise from choosing only one proposal. Zero-light and zero-density
   cases terminate or contribute zero safely, without invalid arithmetic.
4. Begin compensated Russian roulette after a defined minimum depth; divide
   surviving throughput by survival probability. A finite maximum depth truncates
   transport and introduces bias: report it in metadata and perform depth
   sensitivity checks. Accumulation converges to the declared truncated model,
   not an unqualified physical ground truth. Energy clamps, path regularization
   and roughness modifications belong to separately labeled preview profiles.
5. Accumulate scene-linear, unexposed radiance in FP32 with a defined numerical
   accumulation scheme and checked per-pixel sample counts. Exposure/tone mapping
   operate on the displayed mean, never on accumulated samples. For long exports,
   combine bounded sample batches with a higher-precision offline accumulator;
   document rounding and count limits. Export linear HDR plus the profile metadata.

Anders Lindqvist's “Multiple Importance Sampling 101” (*Ray Tracing Gems II*,
chapter 20, sections 20.1.3–20.2) adds the explicit same-measure and matching-PDF
checks. Thanks to Lindqvist; choose and test a documented heuristic rather than
copying the sample's scene/light assumptions. The [Gems review](renderer-systems-gems-review.md#ray-and-path-tracing-follow-up)
records this revision. PBRT 4th edition, [section 13.4](https://pbr-book.org/4ed/Light_Transport_I_Surface_Reflection/A_Better_Path_Tracer),
provides a further check on emission weights and compensated path termination;
Ludus's RGB/bounded profile differs from its broader transport implementation.

Use deterministic random sample addressing by pixel, sample index and dimension,
with a capture seed and versioned sampler. Retries reproduce uncommitted samples;
the accepted-work boundary controls accumulation just as it controls other
histories. Store an accumulation key containing camera, viewport/resolution,
scene/pose, lights, materials/textures, origin, sampler and integrator versions.
Reset on any change that alters the sampled integral; changing only display
exposure need not reset unexposed data. Overlays never enter accumulated radiance.
Raw, independently sampled accumulation is the validation output; a denoised
preview is an additional image. Measure convergence across multiple seeds.
Nonfinite contributions or broken density invariants invalidate a reference
batch and report the offending sample; replacing them with black silently would
bias the result. Capture jobs reserve a bounded residency budget for their pinned
versions and support cancellation, releasing submitted uses through completion.

### Hybrid and real-time transport

Introduce effects in order: selected shadow visibility, then roughness-aware
reflections, then indirect diffuse, each with its own quality and fallback policy.
Raster provides primary visibility and the attributes needed to launch secondary
rays. Hit shading uses the same ray-scene tables; screen-only geometry/material
data is inadequate for an offscreen hit. Assign contributions explicitly so
reflection/probe blending and partial shadow replacement do not double light.

Trace into effect-specific linear signal buffers with validity, normal/depth,
roughness, motion and any required hit-distance guides. Define whether signals
contain albedo, their exposure domain, motion direction/units and guide spaces.
Keep reconstruction behind concrete pass functions with those contracts. Reproject
using actual previous camera/object/deformation data; reject disocclusions and
incompatible material/light/geometry versions. Reflection motion may require hit
surface motion in addition to the primary surface. A stable camera alone does not
make a lighting history valid. Bound history memory separately for each view.

Start with ordinary sampling plus a modest, inspectable temporal/spatial filter.
ReSTIR DI (direct light sampling), GI (indirect paths) and PT (path resampling)
are different upgrades. Reservoir reuse requires specified target densities,
weights, sample identity, visibility revalidation, age limits and bias policy;
do not substitute “reuse the last ray” for an estimator. The May 2026 paper
[ReSTIR PT Enhanced](https://research.nvidia.com/labs/rtr/publication/lin2026restirptenhanced/)
by Daqi Lin, Markus Kettunen and Chris Wyman identifies contemporary work on
reconnection, correlation and reuse cost. Its abstract is discovery evidence,
not a completed algorithm review or a Ludus speedup. Review the full estimator
and validate it against the independent reference before adopting it.

Keep a single loop kernel initially. A wavefront implementation can later split
intersection, hit shading and shadow work into compact queues when divergence
and occupancy measurements justify added dispatches/storage; PBRT's
[chapter 15](https://pbr-book.org/4ed/Wavefront_Rendering_on_GPUs) is a follow-up
source. Queue bounds are correctness contracts: process bounded tiles or return
an explicit incomplete result instead of dropping paths. Sorting, ray reordering,
adaptive sampling and reduced resolution need independent quality/cost evidence.
Fix sampling variance and AS build/traversal costs before adding a general GPU
work scheduler. No vendor denoiser or resampling SDK is a mandatory dependency.

### Ray milestones and evidence

These optional milestones depend on explicit compute/material/attachment and RHI
extensions; they do not claim any existing R1 backend already traces rays.

| Phase | Deliverable | Acceptance |
| --- | --- | --- |
| RT0: scene/query foundation | Optional native capability profile, static triangle BLAS/TLAS, retained tables, primary and visibility queries | Raster/ray instance, position, normal, UV and depth agreement; offscreen hits; build/read/lifetime stress; unsupported devices preserve raster |
| RT1: progressive reference | Frozen RGB surface profile, light/environment/BSDF sampling, MIS, raw accumulation and HDR export | Furnace/Cornell/emissive fixtures, sampling/PDF normalization, multiple seeds/depths, no double lighting; unsupported content and overflow are visible |
| RT2: hybrid effects | Bounded shadows, reflections and then indirect diffuse with reconstruction/fallbacks | Raw and reconstructed images compared to RT1; moving/disoccluded/reloaded scenes, per-view history, build/ray/filter median and tail times |
| RT3: dynamic scene scaling | Skinned/morphed BLAS, measured refit/rebuild, optional compaction and scheduling | Pose parity, changing topology/opacity, extreme/mirrored transforms, thin geometry, streaming/rebase/loss and peak overlap memory |
| RT4: high-end real-time PT | Reviewed resampling, reconstruction and optional wavefront/ray-pipeline variants | Scene suite at fixed resolution/quality budget; temporal artifacts and error versus RT1; measured target-GPU benefit and disabled-mode support |

Track AS resident/scratch/pending bytes, build/refit/compaction costs, traced
ray classes, path-depth distribution, opacity candidates, NaN/invalid-PDF counts,
sample counts, rejected histories and incomplete tiles. Actual hardware traversal
statistics may be unavailable; distinguish estimates from measured counters.
Inspect instance/primitive IDs, barycentrics, geometric/shading normals, ray offsets,
BSDF/light PDFs, MIS weights, direct/indirect/emission AOVs and raw/filtered error.
Selected-pixel path logging is bounded and opt-in. Reference scene fixtures and
statistical image tolerances matter more than reproducing one vendor's exact
floating-point image. No synchronous shipping-frame readback is required.

## Debug drawing, sprites, text and fonts

Debug submissions contain explicit view mask, space, depth mode, color domain,
width units and lifetime. Start with lines, boxes, axes, spheres and labels.
Reserve a frame arena; persistent commands use generation-checked handles and
bounded lifetime, while each worker has a private append buffer with deterministic
merge. Optional overflow drops complete commands and increments counters.

Portable thick lines expand into triangles. Clip world segments against the
near plane before perspective divide; handle zero-length segments, finite joins,
caps and minimum physical width. World-unit and physical-pixel width are distinct.
Depth-tested, always-on-top and two-pass x-ray modes are explicit variants.
Hardware wide lines are optional because API limits and raster rules differ.
Translucent overlays preserve order; opaque depth-tested primitives may batch
under the ordinary compatibility rules. Geometry is shared across views where
possible, but clip/width expansion can be per view. Debug work must not modify
scene visibility, lighting or temporal history.

Text shaping, script/direction runs, cluster identity, font bytes and rasterization
remain with the existing Text owner. UI/paragraph code owns bidi itemization,
fallback, line breaking and carets; renderer never maps one Unicode code point to
one quad. Submit shaped glyph IDs/positions with a font version and **physical**
raster size. Preserve original cluster provenance for inspection.

The first text adapter uses size-specific grayscale coverage, an append-only
bounded R8 atlas, transparent gutters and no mipmaps, plus adjacent instanced
quads in painter order. R8 sampling and streaming atlas writes are explicit RHI
prerequisites; RGBA replication is a declared transitional variant if necessary.
Atlas coordinates stay stable until a version reset; submitted pages/regions
remain leased. Missing/not-ready glyphs have a visible deterministic fallback,
and atlas pressure is reported. Reuse layouts and glyph cache entries; do not
reshape/rasterize static labels every frame. MSDF or analytic outline rendering
is a separate measured option for large/transformed text, preserving shaping and
the application contract. Never assume subpixel LCD rendering survives browser
composition or arbitrary display orientation.

Preserve the selected [GraphicsText ownership](../../.kiro/specs/text-font-rendering/design.md#1-decisions-and-dependency-direction):
GraphicsText owns glyph preparation/cache, CPU atlas shadows and ordered text
lists. Its Renderer adapter contributes generic leased quad packets to the frame
plan; Renderer owns composition, and RHI owns GPU resources. Do not create a second
glyph cache inside Renderer or make CPU Text depend on graphics. The standalone
text milestone and its narrow coverage interface remain a valid independent
consumer; moving its uploads into shared GDI services is an explicit integration
change with the same readiness and lifetime guarantees. Its pinned shader build
path need not be replaced merely to share packet submission.

Sprites and screen quads reuse the submission allocator but keep independent
material/texture semantics. Nine-slice and layout remain UI policy. Batching across
a different scissor, blend mode, atlas page or layer is forbidden. Filtering
gutter/mipmap policy belongs to the atlas's content type; font rules do not
automatically apply to sprites or lightmaps.

The Aurelio Reis chapter (*Game Programming Gems 8*, 1.1) supports separating
fixed quad geometry from glyph-instance data, but its constant-array/console
constraints are historical. Thanks to Reis: measure one-glyph, short-label and
large-overlay cases before choosing a packing/instance path. An optimization
must preserve painter order and the Text shaping contract.

Manny Ko's atlas chapter (*Game Engine Gems 3*, 9.4–9.5) sharpens the cook/runtime
boundary. Thanks to Ko: reserve filtering footprints **before** packing, report
occupancy including gutters, and validate chart seams. Use shelves for mutable
rectangular glyph pages and an offline chart packer for lightmaps; only measured
offline packing loss justifies irregular bitmask packing. Lightmap mip generation
must respect chart adjacency and mip-safe padding (or a clamped LOD policy).
Glyph coverage uses transparent borders; unrelated sprites use their own edge
extrusion. Diffusing unrelated glyphs or colors across atlas cells is invalid.

## Inspection, budgets and optimization gates

Expose counters for extraction/culling/sorting/recording time, visible/culled/LOD
counts, batches/instances, triangles, binding/pipeline switches, upload bytes,
pipeline misses, light-list/shadow overflow, atlas occupancy, peak frame arena,
GPU memory categories and pending retirement. GPU pass timings are optional and
report unavailable distinctly from zero; asynchronous readback never stalls a
shipping frame. Report median and tail frame times with resolution, scene,
adapter/driver/backend/tool pins, frames in flight and cold/warm state.

Give every pass/resource/view a bounded label and each packet a source ID.
Inspect why an object was culled, which pipeline/version bound, exact texture
ranges, light selection, shadow ownership, viewport mapping and history resets.
Provide modes for direct CPU draws, unsorted packets, disabled batching, frozen
LOD, no shadows, constant materials, overdraw, normals, depth, cluster occupancy,
lightmaps, atlas pages and linear/output color checks. Capture the immutable
frame plan and reproducible resource revisions; native GPU captures supplement
that record. Do not build a second command serializer to obtain basic tracing.

Optimization acceptance compares image correctness and memory/latency as well as
throughput. Turn on a new path only after representative scenes show an improvement
at acceptable tail latency and maintenance cost. Keep opt-outs for investigation.
GPU-driven geometry, async compute, virtual textures and virtual shadow maps need
separate proposals with residency, synchronization, capability fallback and
debugging evidence. Ray/path tracing follows the optional RT0–RT4 plan above.
These are modern candidates, not unconditional
definitions of a good low-level renderer.

### Modern technique selection

The [SIGGRAPH 2025 Advances course](https://advances.realtimerendering.com/s2025/)
includes production work on stochastic many-light rendering, ray-traced world
lighting and order-independent transparency. The program establishes relevant
research directions; it does not establish their suitability or cost for Ludus.
The following choices are engineering judgments, with future algorithm reviews
required before implementation.

| Option | Adopt when | Contract required first |
| --- | --- | --- |
| Clustered forward | Many local lights and varied materials make per-object lists expensive | Conservative 3D assignment, checked lists and transparent coverage |
| Deferred/visibility buffer | Expensive opaque shading/overdraw dominates and bandwidth/material indirection wins in comparison | Separate transparent path, material reconstruction, derivative/MSAA rules |
| GPU visibility/indirect | CPU submission/culling dominates after ordinary batching | Stable object IDs, output capacity, conservative occlusion, direct reference mode |
| Bindless/mesh shaders | Verified target limits and workload justify reduced binding or geometry work | Descriptor/version retirement and authored portable shader variants |
| Temporal upscaling/TAA | Shading cost or image quality warrants history complexity | Motion vectors for camera/object/deformation, disocclusion, exposure and jitter conventions |
| Hybrid ray tracing / real-time path tracing | Dynamic scenes need quality beyond bake/probe/direct-light approximations | RT0–RT4: shared ray scene, retained structures, independent reference, reviewed estimators and reconstruction budgets |
| Virtual textures/shadows | Measured working set exceeds conventional residency | Page-table versions, feedback latency, initialization, eviction proof and coarse fallback |
| Async compute | Actual queue overlap improves total time without excessive bandwidth contention | Cross-queue dependencies, ownership and completion proof in RHI |

Optional advanced features must preserve scene/view/material identities and expose
their own diagnostic data. A tiny scene, simple UI or limited GPU should not pay
mandatory constant costs for a technique chosen to scale a different workload.

## Implementation phases and acceptance

These phases complement RHI R0–R5; they do not rename or claim completion of them.

| Phase | Deliverable and prerequisites | Acceptance |
| --- | --- | --- |
| L0: portable scene packets | R1 static flat/unlit meshes with fixed transforms, immutable snapshots, CPU visibility, ordered 2D overlays, error materials; remain within current limits and R1-compatible depth | Public-only SDK fixture on Vulkan/Metal/WebGPU/WebGL; invalid/stale inputs, capacity failure and pixels match an unsorted direct path |
| L1: views and depth | GDI/RHI explicit attachments, clear/compare state and frame retention; orthographic/perspective, virtual screens, offscreen/split views | Reverse-Z ordering, clip/UV orientation, letterbox picking, DPI, zero extent, resize and neighboring view preservation |
| L2: mutable data and overlays | Completion-scoped uploads/updates; dynamic instances, debug triangles, R8 text adapter | In-flight overwrite stress, line near-plane/degenerate cases, painter/scissor order, atlas pressure, CPU text fixtures and loss/restart |
| L3: material/content versions | Cook/reflection/layout expansion, texture formats/mips and pipeline prewarm | Per-target layout fixtures; invalid reload preserves old image; mip/color semantics; bounded cache/upload overlap and deterministic fallback |
| L4: lighting baseline | Lit material, bounded direct lists, environment, shadows, baked assets, linear intermediate/output | Reference BRDF/color tests; no double lighting, shadow invalidation/atlas borders, saturation/overflow and LDR/HDR capability variants |
| L5: measured scaling | Clustered forward, improved culling/LOD, optional native parallel recording | Dense-light and many-object fixtures; direct-path comparisons, overflow correctness, physical GPU median/tail/memory/latency evidence |
| L6: optional advanced effects | Temporal/GPU-driven/virtual-residency plans and the RT0–RT4 ray plan | Capability probes and disabled-path support; image/temporal tests and workload-specific gains before default enablement |

Use empty, single triangle, Cornell box, transparent intersections, split-screen,
many small meshes, many lights, heavy overdraw, animated bounds, streamed textures,
multilingual overlays and repeated resize/reload/loss scenes. Separate software-GPU
CI from physical GPU performance evidence. Required implementation checks are
warning-clean pinned builds, meaningful unit/pixel/SDK tests, ASan/UBSan and pinned
format/tidy; documentation changes run documentation gates. Do not invent FPS
targets or buffer capacities without the supported workload and measured budget.

## Contemporary primary references

- Khronos, [Vulkan Pipeline Cache](https://docs.vulkan.org/guide/latest/pipeline_cache.html)
  and [Pipeline Management sample](https://docs.vulkan.org/samples/latest/samples/performance/pipeline_cache/README.html):
  pipeline creation can remain costly; validate persistent cache compatibility
  and prewarm legal variants. Cache miss behavior must remain visible.
- Khronos, [Render Pass](https://docs.vulkan.org/spec/latest/chapters/renderpass.html)
  and [Synchronization](https://docs.vulkan.org/spec/latest/chapters/synchronization.html):
  explicit attachment preservation and declared hazards inform pass contracts;
  actual operations remain constrained by Ludus's pinned API/toolchain.
- W3C, [WebGPU specification](https://www.w3.org/TR/webgpu/): validate enabled
  features, usage scopes, limits and formats. Timestamp queries and relevant
  indirect features are capability paths, not assumptions about every browser.
- Google Filament authors, [Physically Based Rendering in Filament](https://github.com/google/filament/blob/main/docs/Filament.md.html):
  consult its energy-conserving BRDF and light/exposure conventions when specifying
  L4. The choice of a bounded forward baseline and staged features is Ludus's
  design judgment, not a performance result from Filament.

Thanks to these authors and organizations. Future implementation must cite the
specific consulted algorithm/section beside the code, preserve notices and explain
departures; this architecture's bibliography does not replace code attribution.
