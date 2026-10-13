# Save, validate and build render graphs

RG0 implements the offline graph contract in the installable `ludus` host tools.
It saves reusable graph assets, validates explicit configurations and camera
output routes, and prints a deterministic serial plan. The same Python backend
is available to future editor jobs; no Qt or engine checkout is required after
installation. See [host tools](host-tools.md) for installation and the
[architecture](../architecture/render-graph-authoring.md) for ownership and later
stages. RG1 adds explicit offline shader/schema builds and immutable packages,
using the same CLI/backend. Renderer admission remains RG2.

**RG0 plans are contract diagnostics, not executable renderer packages.** They
do not compile shaders, evaluate scene draw-item coverage, negotiate device
capabilities or render pixels. Those checks are reported as pending. RG1 builds shader packages; RG2 owns
Renderer/RHI integration. `DefaultOpaque` and
`RendererStatic` name the first planned raster contract, not verified arbitrary
mesh/material compatibility or a newly delivered depth-only GPU pipeline.

## Create a saved graph and import it

From a checkout, use `./scripts/ludus`; after installation use `ludus`:

```bash
./scripts/ludus render-graph new out/my-render-graphs
./scripts/ludus render-graph validate out/my-render-graphs/main.lurender.json
./scripts/ludus render-graph dump out/my-render-graphs/main.lurender.json
./scripts/ludus --json render-graph dump out/my-render-graphs/main.lurender.json
```

`new` creates a new directory containing `rasterize.lurender.json` and
`main.lurender.json`. Existing directories/files/symlinks are never replaced.
Sources are staged and validated before publication; failure cleans the owned
staging directory. This creates graph sources, not a game project, SDK, build
tree or shader package. No command downloads tools or starts the renderer.

The root has Scene, Camera, color/depth Target, imported Subgraph and Present
nodes. The camera's target input names its designated presentation texture;
its view and the scene feed the imported graph. The raster result, rather than
the unwritten target, feeds Present. Its import is an ordinary editable source
record:

```json
{
  "id": "raster",
  "kind": "Subgraph",
  "asset": "rasterize.lurender.json",
  "configuration": "ColorAndDepth"
}
```

The asset path is relative to the graph package root, not to the importing file
or current working directory. By default the package root is the root graph's
directory; `--root path/to/package` supports root documents in a subdirectory.
Imports cannot use absolute paths, `..`, URLs or symlinks escaping that root.
Each file owns a distinct asset ID. Repeated imports of the same file are
separate instances with shared asset/configuration identity and scoped node IDs.

`validate` and `dump` only read files. They validate the entire declared import
closure's interfaces and reject bad unselected configurations too. Initialization
and active camera routing are evaluated for the selected root configuration.
Source bytes are rechecked before a result is returned; a changed source rejects
the candidate. A caller can retain its previously validated plan unchanged.

## Offscreen and depth-only roots

```bash
./scripts/ludus render-graph new out/offscreen-graphs --output offscreen
./scripts/ludus render-graph dump out/offscreen-graphs/main.lurender.json
./scripts/ludus render-graph new out/depth-graphs --output depth
./scripts/ludus render-graph dump out/depth-graphs/main.lurender.json
```

All three creation modes save the identical `RasterizeGraph` source asset. The
presentation and offscreen-color roots import its `ColorAndDepth` configuration;
only their targets and terminal operations differ. The depth root explicitly
imports `DepthOnly` and exports a depth texture, with no color port/attachment.

Consuming only Depth from a `ColorAndDepth` import is legal: Color's consumer is
optional, but the authored color work/attachment remain in the plan. It never
selects or creates `DepthOnly`. Authors must expose another configuration and
users must select it explicitly. Changing a selected configuration does not
repair incompatible connections automatically. Unknown configurations or
connections to unavailable ports fail validation.

Inspect a reusable library without pretending its unbound inputs are a closed
execution graph:

```bash
./scripts/ludus render-graph validate out/my-render-graphs/rasterize.lurender.json --library
```

This returns `InterfacesOnly`, listing all declared configurations and ports.
It does not prove concrete resource initialization or camera routes. For a closed
root with additional authored configurations, use `--configuration Name` on
`validate` or `dump`; omission chooses the asset's declared default. Imports
always contain an explicit configuration name. There is no connection-driven
automatic selection in RG0.

## Version 1 source format

Sources are bounded UTF-8 JSON. Duplicate fields, nonfinite constants, unknown
fields/versions, duplicate identities and unsupported node kinds are errors.
Text editors modify the saved source directly; future editor UI will use this
same document representation. Generated plans are not a second authored source.

| Record | Required fields and contract |
| --- | --- |
| Asset | `version: 1`, `id`, `default`, `configurations`; default names a declared entry |
| Configuration | `id`, `inputs`, `outputs`, `nodes`, `edges`; optional `layout` |
| Interface input | `id`, `type`; each required input is bound by the containing Subgraph node |
| Interface output | `id`, `type`, `source`, `required`; source is `node.port`; required means the imported output must have a consumer |
| Connection | `from`, `to`, each `node.port`; `input.port` denotes a configuration input and cannot be a destination |
| Layout | `positions`, containing unique `{node, x, y}` records for existing configuration nodes; coordinates are bounded integers |

IDs are 1..63 ASCII characters: first a letter, then letters, digits, `_` or `-`.
`input` is reserved as a node ID. Node/input/output IDs are unique within their
respective configuration tables; configuration IDs are unique within an asset.
Texture outputs of a root configuration are explicit Export roots. Other library
outputs become roots only when a containing graph consumes or exports them.
Root execution configurations cannot contain unbound interface inputs.

| Node kind | Additional fields | Inputs / outputs |
| --- | --- | --- |
| Scene | None | Outputs `scene: Scene`; declares a future scene-snapshot binding |
| Camera | `active`, `role` (`Presentation` or `Offscreen`), `target_type` | Input `target` of the specified texture type; output `view: View`. An inactive camera may leave target unbound and needs no route, but cannot drive a raster pass |
| Target | `type`, `format`, `extent: [width, height]`, `samples`, `imported`, `initialized`, `presentation` | Outputs `texture` of the specified texture type; names physical resource version 0 |
| Raster | `mode`, `vertex_layout: RendererStatic`, `material: DefaultOpaque`, `depth_load`, `depth_store`; also `color_load`, `color_store` for ColorAndDepth | Inputs Scene/View/DepthTexture, plus ColorTexture for ColorAndDepth; outputs written depth/color versions |
| Subgraph | `asset`, `configuration` | Selected asset interface defines its input/output ports |
| Export | `type` | Input `texture` matching ColorTexture or DepthTexture; external output root |
| Present | None | Input `color: ColorTexture`; root for the written version of an imported presentation target |

The Raster input names are `scene`, `view`, `depth`, and (when available) `color`.
Outputs are `depth` and `color`. Types are `Scene`, `View`, `ColorTexture` and
`DepthTexture`; interface outputs and Export nodes accept only texture types.
Color targets use `RGBA8Unorm`; depth targets use `Depth32Float`. The initial
profile admits one sample and width/height each in 1..16384 pixels. These bounds
describe RG0's contract profile, not a hardware-capability guarantee. Unsupported
formats/samples/layouts/material families reject rather than invent variants.

All Boolean fields require JSON `true`/`false`; Boolean values are not integers.
Only imported targets may declare initialized previous contents. A presentation
target must be an imported color texture. Active presentation cameras require a
color target flagged for presentation; offscreen cameras require a target without
that flag. A valid active route must carry the camera's rendered result to Present
or Export on its designated physical resource. Presenting an already initialized
target without rendering the camera does not satisfy that requirement.

## Initialization, versions and order

Raster attachment load is `Clear`, `Load` or `Discard`; store is `Store` or
`Discard`. Clear initializes the complete attachment; Load requires defined prior
contents. Arbitrary scene geometry after Discard does not establish complete
initialization. Store preserves defined contents; Discard invalidates them even
after a clear. Undefined outputs cannot be exported/presented. Color/depth extents
and sample counts must match in a ColorAndDepth pass.

Writes produce a new logical version of the same physical target. Two passes
cannot independently write from the same preceding version: connect the first
pass's output to the second pass's input. Exports/Present must consume the final
physical version. A texture connection does not copy previous contents to another
physical allocation. Copies, history, resolve, stencil and sampled-texture/compute
nodes are outside RG0's profile and are rejected as unknown kinds/fields.

Dependencies determine order; the earliest ready node in the authored node array
breaks ties. Imported nodes expand at that position through their own deterministic
order. Within-frame cycles and recursive imports are errors. The dump preserves
nested instance paths, source asset/configuration, scene/view identity, technique,
attachment versions and initialization, dependencies and observable roots.

Two cameras contributing to one target need explicit ordering and preservation.
A later full Clear removes the earlier camera's contribution and its final route
fails. An explicitly authored Load technique receiving the previous version can
preserve both. Viewport rectangles/compositing remain later Renderer integration;
RG0 never infers merging or blending from two output connections.

Repeated instances reuse the same authored technique identity; the plan reports
the selected unique technique IDs. This is not a shader binary or device pipeline
count. Moving nodes through `layout` changes source hashes but not the semantic
digest or serial plan. Plans and source labels are deterministic after relocation
under the same package-relative paths.

## Bounds, errors and verification

Each asset is at most 128 KiB; a closure contains at most 32 files, 2 MiB of
source bytes and eight levels including the root. Each asset has at most eight
configurations; each configuration admits eight input/output ports, 128 nodes
and 512 edges. Expansion admits at most 1024 nodes and 256 serial operations.
The validator rejects capacity excess before publishing a plan.

CLI success exits 0; contract/source failures exit 1. `--json` returns schema 1
with `plan`, `library` or `created`; failure returns `error.code` and
`error.message`. Stable graph error codes have prefix `RenderGraph` and suffix
`Schema`, `Version`, `Capacity`, `Source`, `SourceChanged`, `Configuration`,
`DuplicateIdentity`, `Port`, `TypeMismatch`, `UnboundInput`, `UnboundOutput`,
`UnusedRequiredOutput`, `Recursion`, `Cycle`, `Initialization`,
`AttachmentMismatch`, `MultipleWriters`, `StaleVersion`, `CameraTarget` or
`CameraRouteMissing`. Messages identify the source/configuration or nested node
path; diagnostics are capped at 4 KiB of UTF-8. Directory-creation conflicts use
the existing host-tooling errors.

Run the source and CLI regressions with:

```bash
PYTHONPATH=scripts/python python3 -m unittest test_render_graph test_ludus_cli -v
```

The project-SDK CI job also installs the wheel and exercises saved/imported
presentation and depth roots outside the checkout. The macOS host-tooling suite
runs the graph regressions. RG0 assertions cover contracts and provenance; GPU
pixel, shader compilation and scene-coverage evidence must come from their later
stages and cannot be inferred from this validator's success.


## Build explicit shader packages (RG1)

RG1 delivers the bounded scalar/vector uniform raster profile of the
[build contract](../architecture/render-graph-authoring.md#deterministic-shader-and-graph-build).
It generates shared CPU/schema/shader declarations, resource bindings and entry
wrappers; calls the existing SDK shader helper; verifies target layouts and stage
links; and publishes all authored techniques/profiles/recipes as one immutable
package. It does not create device pipelines or execute the diagnostic graph.

Start with an RG0 closed root and create its sibling build source:

```bash
ludus render-graph new out/raster-sources
ludus render-graph new-build out/raster-sources/main.lurender.json --profiles metal wgsl
ludus --json render-graph build out/raster-sources/main.lushader.json out/raster-build \
  --helper /path/to/sdk/lib/cmake/Ludus/shaders \
  --compiler /path/to/pinned/slangc
```

Use the SDK built from this revision or later: its `shaders` directory includes
`compile_graph_stage.py`. The helper remains the owner of target compilation;
the editor/CLI share the build service, rather than introducing another compiler.
The host tools wheel owns the editable starter graph/functions. The source
checkout's `cmake/shaders` directory by itself lacks installed toolchain locks;
use an installed SDK. No command downloads tools, repairs game projects or
changes engine capabilities. See [shader toolchain setup](../decisions/0010-shader-toolchain-feasibility.md).

For the Linux Vulkan/browser chain, explicitly admit its three targets:

```bash
ludus render-graph new out/browser-raster
ludus render-graph new-build out/browser-raster/main.lurender.json --profiles spirv wgsl glsl
ludus render-graph build out/browser-raster/main.lushader.json out/browser-build \
  --helper /path/to/sdk/lib/cmake/Ludus/shaders \
  --compiler /path/to/pinned/slangc \
  --validator /path/to/pinned/spirv-val \
  --spirv-cross /path/to/verified/spirv-cross
```

Targets are `metal`, `spirv`, `wgsl` and `glsl` (ES 3.00). Slang must match the
SDK pin, SPIR-V uses the pinned validator, and GLSL additionally verifies the
SPIRV-Cross source/build identity and binary digest. Metal and WGSL can build on
macOS without a Linux SPIR-V validator. The initial build profile has vertex and
fragment functions, float scalar/vector fields, read-only uniform bindings in
set/group zero, and one color plus depth or depth alone. Matrices, arrays,
texture/storage bindings, compute/ray stages, custom fragment-depth outputs,
MSAA and broader attachment profiles reject; extend them with independently
qualified schema/target fixtures. Transform rows can use explicit `Float4` fields.
The illustrative starter vertex takes clip-space positions; it does not wire an
engine scene or implement scene-material/deformation/coverage parity.

### Source ownership and finite choices

The closed version-1 `.lushader.json` source has these fields:

| Field | Contract |
| --- | --- |
| `version`, `id` | Version 1 and an ASCII identifier |
| `profiles` | Nonempty, unique list of admitted targets; all are built |
| `schemas` | At most 16 named `Uniform`, `Vertex` or `Varying` schemas; at most 16 fields each |
| `programs` | Named stage/function/source records; input/output schemas and explicit uniform bindings |
| `techniques` | Explicit `DefaultStaticOpaque.ColorAndDepth` and/or `.DepthOnly` mappings and legal pipeline recipes |
| `budgets` | Author bounds for configurations, programs, emitted artifacts and pipeline recipes |

Identifiers use 1–63 ASCII letters/digits/underscores and start with a letter;
reserved C++/Slang/wrapper names reject. A field has `id` and `type`
(`Float32`, `Float2`, `Float3`, `Float4`). Vertex/varying fields also declare
`semantic` and `location` in 0–15; ordinary varying locations are unique. Vertex inputs use Float2/3/4.
Vertex output has exactly one Float4 `SV_Position`; other supported semantics
are POSITION, NORMAL, TEXCOORD0–9 and COLOR0–9. System position is not a user
varying link. Field names are stable schema identities in this initial format;
a rename is a schema change, rather than an implicit migration.

A program declares `id`, `stage`, `source`, `function`, `input`, `output` and
`bindings`. Vertex input/output name Vertex/Varying schemas; fragment input names
a Varying schema and output is `Color` or `Depth`. Color functions return float4;
Depth functions return void and may implement authored coverage/discard. Entry
wrappers add stage and target semantics. Uniform bindings declare `id`, `schema`
and a unique `slot` in 0–7. Shader authors write functions using those generated
values, with no manually duplicated structs or resource register declarations:

```slang
float4 PSMain(RasterVarying value)
{
    return float4(material.tint * material.gain, 1);
}
```

Each technique declares `id`, `configuration`, `vertex`, `fragment` and nonempty
`recipes`. The starter manifest shows the entire source format. Every program
belongs to a technique; unknown, implicit or wrong-stage selections fail.
All root graph configurations and their import interfaces are validated, and
selected graph techniques must appear in the build manifest. Both authored
techniques are cooked even if the current root only selects one. Unused Color
never manufactures a depth-only program.

A recipe declares `id`, `color`, `depth`, `samples`, `topology`, `cull`, `front`,
`depth_compare`, `depth_write` and `blend`. The profile admits RGBA8Unorm or None,
Depth32Float, one sample, TriangleList, None/Back/Front culling,
Clockwise/CounterClockwise front face, Less/LessEqual/Equal/Always comparison,
explicit Boolean depth writes and Opaque blend. This describes portable prewarm
requests; device capability and Ready status remain pending. Load/store belongs
to the graph plan, rather than the shader program. Recipe keys compare complete
canonical state, stage identities, vertex schema and target binding layouts;
recipe/node names and camera/extent values do not create extra shader programs.

Budget ceilings are 8 configurations, 32 programs, 128 target artifacts and
64 target pipeline recipes. The author may choose smaller bounds. Root and
import-closure graph configurations also fit the configuration bound, counted
separately from technique configurations. Reports distinguish declared programs,
unique stage identities, target artifacts and unique recipe keys. Deduplication
never permits the authored count to exceed its bound or silently drops a target.

Shader sources and literal quoted includes must remain inside the package root.
Includes are relative to their shader file; graph imports are relative to the
package root. Module imports, dynamic/continued includes, source remapping and
recursive includes reject. Conditional includes are conservatively tracked.
The compiler reads staged captured sources only, and its dependency file is
checked against that closure. Sources are bounded to 64 files/4 MiB, each at
most 128 KiB, with include depth 16; generated artifacts are at most 2 MiB each
and a generation is at most 64 MiB/1024 files. Use `--root` for a containing
package when the sibling root/manifest lives in a subdirectory; program source
paths are relative to that package root, not the manifest directory.

### Generated layouts and publication

`current.json` names a content-addressed directory under `generations/`.
`build-report.json` contains candidate job timing and cache hit/miss counts outside
the immutable generation. Only `current.json` determines activation; a failure
after reporting can leave an unactivated candidate report. Inspect the report's package path:

```bash
ludus --json render-graph verify-package /path/to/raster-build/generations/GENERATION_DIGEST
```

| File/directory | Purpose |
| --- | --- |
| `package.json` | Plans, explicit selections, counts/bounds, program descriptors, complete recipe keys, routes, pending runtime checks and provenance |
| `schemas.json`, `schemas.h` | Logical schema source and generated C++ values/per-program/target uniform packers |
| `programs/KEY/` | Target artifact, raw reflection, verified stage descriptor and generated wrapper |
| `sources/` | Captured graph/build/function/include sources for source-directed diagnostics |
| `integrity.json` | Complete bounded file inventory and SHA-256 identities |

Generated C++ uses Ludus primitive aliases and has no owning runtime containers,
allocation or exceptions. Include `schemas.h`, fill logical values, and call the
appropriate `ludus::render_graph_generated::Pack_PROGRAM_TARGET_BINDING` with
caller-owned native-endian upload bytes. Null/short output returns false and
preserves output; input/output must not overlap. Padding is zeroed, and bytes
beyond the target's occupied size remain untouched. The packer is usable on any
thread when callers provide independent storage; device uploads retain their
own RHI ownership/thread rules. A native C++ struct is never reinterpreted as the
shader buffer: `Float3` followed by `Float32` places the scalar at 16 in Metal
and at 12 in the qualified SPIR-V/WGSL/GLSL profile. Target reflection and emitted
Metal/WGSL/GLSL declarations are checked independently, including field identities
and offsets; inter-stage consumers must match a typed subset of vertex outputs.

Stage cache identities include captured transitive sources, generated wrappers,
used schemas, target/profile and the actual helper/generator/tool/library/lock
identities. Graph layout, camera count, extent and recipe state do not enter stage
identity. Pipeline state changes rebuild packages/recipes and reuse stages;
a changed included function rebuilds only its affected stage/targets. Cached
entries compare full identity and every artifact digest. Corrupt/missing entries
rebuild; a corrupt cooked generation is rejected, never reported ready. Metal
source debug paths are remapped to package-relative owners for clean-build
reproducibility, and saved wrappers/source snapshots retain provenance.

One nonblocking cooperative writer owns a build tree; a second CLI/editor job
reports Busy. Different trees do not share writable compiler state. Cancellation
is supported through the shared API callback and CLI interruption; compiler
process groups are terminated/reaped and private staging is removed. Source/tool
snapshots are rechecked before publication. Compiler/reflection/budget failures,
cancellation, stale sources and failed publication preserve the previous
`current.json`. Cache entries completed by a failed job may be reused later;
they cannot activate a partial package. A completed generation is published
before the pointer is atomically replaced, and old generations remain available.
This guarantees process-level publication, not power-loss durability across all
filesystems; raw edits to cache/generation/build trees are unsupported.

### Acceptance evidence

`test_render_graph_build` covers deterministic clean/warm builds, transitive
invalidation, independent target packing, budgets, strict source boundaries,
corrupt artifacts/descriptors, cancellation/staleness/compiler failures, Busy,
CLI/backend equivalence and unchanged prior publication. Pinned Slang acceptance
can be enabled with `LUDUS_RG1_SLANG`; optional `LUDUS_RG1_CXX` compiles/runs the
generated header. `tests/sdk_consumer/verify_render_graph_build.py` exercises the
installed wheel/SDK helper outside the checkout, clean/warm builds, real include
invalidation, compiler failure, relocated package verification and generated CPU
packing under ASan/UBSan. `render-graph-browser-test.mjs` validates emitted WGSL
and GLSL stages on pinned Chromium's software adapter; this is shader admission,
not a rendering or physical-GPU performance claim.

Local macOS evidence covers pinned Slang Metal/WGSL, pinned Chromium WGSL
admission and Clang 18 generated packing. On macOS 26.6 the local sanitizer
packing executable and an empty Clang 18 sanitizer control both stall at startup;
local ASan/UBSan acceptance is unavailable. CI runs the packing fixture with
sanitizers on its supported macOS/Linux hosts.
CI includes installed macOS Metal/WGSL and Linux SPIR-V/WGSL/GLSL acceptance.
Linux SPIR-V/GLSL evidence must pass that job; it is not inferred from macOS.
DeviceCapabilities, SceneCoverage, PipelineReadiness and GpuExecution remain
explicitly pending even after successful RG1 publication.
