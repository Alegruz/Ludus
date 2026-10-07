# Terrain Gems and research review

Review date: 2026-10-06. Repository baseline: `1e6bd1a`.
The resulting designs are [terrain systems](terrain.md) and
[terrain generation](terrain-generation.md); their implementation phases remain
proposed. The terrain design preserves its initial selection and a revision
table so the consequences of this review are explicit.

## Selection and evidence

The baseline was recorded before searching
[game-dev-gems-toc.md](../../references/game-dev-gems-toc.md) for terrain,
heightfield/heightmap, fractal generation, noise, clipmaps, virtual texturing,
erosion, roads and vegetation. The index locates chapters; chapter titles alone
are not evidence of their algorithms or applicability.

Selected readings cover twelve chapters across Game Programming Gems 1–3,
GPU Pro 1–3 and GPU Gems 1–3. GPG 1, GPG 3 and GPU Pro 2 are scanned copies;
their selected pages were rendered and read with macOS Vision OCR, with identity
and key figures/contracts checked visually. GPG 2 and GPU Pro 1/3 supplied
extractable text; selected identity/seam pages were also rendered. GPU Gems
readings used the publisher-hosted author chapters linked below. Supporting
modern sources were checked through author/project and official engine sites.

Local locators use one-based physical PDF pages; printed pages are stated
separately. GPG 1's local copy omits its printed contents and has a different
page offset from other volumes. Chapter starts were checked, rather than inferred
from an offset shared across books. The ignored local reference library is not
part of the public wiki. No companion-CD code was consulted or copied.

## Changes justified by the readings

| Reading | Useful finding | Final design consequence |
| --- | --- | --- |
| Lecky-Thompson, regeneration | Coordinate-controlled generation can replace stored results only under a reproducible recipe | Version/address/numeric manifests; baked bytes for portable authority |
| Shankel, fault formation | Repeated fault operations and smoothing give an editable finite heightfield | Optional authored macro operator; smoothing is not hydraulic erosion |
| Shankel, midpoint displacement | Square steps depend on neighboring diamond values and a chosen edge policy | Domain/order/halo declarations; no independent per-tile diamond-square default |
| Shankel, deposition | Surface relaxation is stateful; caldera edits need connected-region scope | Optional bounded offline stage; explicit domain and work limits |
| Snook, interlocking tiles | Reusable index bodies/links can close different detail boundaries | Complete 16-mask stitched patches, 2:1 balance and corner coverage tests |
| Shankel, heightfield normals | Regular-grid shortcuts rely on shading assumptions | Spacing-aware smooth normals distinct from exact triangle contact normals |
| Asirvatham/Hoppe, clipmaps | Constant footprints and coarser coverage control work; updates have neighborhood dependencies | Wider quadtree coverage, immutable grid footprints, explicit normals/filter halos |
| Perlin, improved noise | Interpolant derivatives and gradient distribution affect artifacts | One smooth basis, specified quintic fade, bounded detail and golden corpus |
| Geiss, density terrain | Volume fields enable overhangs; coarser blocks control count; topology and collisions are separate concerns | Mesh attachments by default; separate density/meshing/physical extension |
| Pintér, outdoor terrain | Procedural soil plus manual paint and mesh details supports artists | Persistent paint, reusable mesh details, IDs separate from weights |
| Yusov, deformable terrain | Normal borders, reconstruction error and coarse-view edits require explicit dependencies | Canonical halos, conservative actual-topology error, finest edits and bottom-up rebuild |
| Chajdas et al., virtual texturing | Streaming fallback and derivative/border filtering are essential | Optional VT gate with resident parents, correct gradients, gutters and slot retirement |

The ownership boundaries, statuses, transactions, random-address packing,
multi-view rules, physical pins, conservative arithmetic requirements and
implementation gates are Ludus decisions. The books do not specify the complete
modern engine architecture.

## Procedural terrain in Game Programming Gems 1

**Guy W. Lecky-Thompson**, section 4.16, *Real-Time Realistic Terrain
Generation*: [PDF pp. 476–490](../../references/Game%20Programming%20Gems%201.pdf#page=476),
printed pp. 484–498. The chapter covers coordinate-seeded terrain, smoothing,
fault-based shapes and broader procedural world content.

**Adopt:** independently recoverable procedural content and explicit coarse
structure before detail. **Adapt:** persistent stage/domain identities,
addressed Philox, bounded work and a complete recipe/numeric version. The chapter's
ANSI `srand`/`rand` examples are not the engine policy. A seed does not promise
portable floating-point terrain, and procedural regeneration does not replace
authored changes, content validation or shared hydrology.

**Jason Shankel**, section 4.17, *Fractal Terrain Generation—Fault Formation*:
[PDF pp. 491–494](../../references/Game%20Programming%20Gems%201.pdf#page=491),
printed pp. 499–502. Random faults produce sharp elevation changes; decreasing
amplitudes and low-pass filtering soften them.

**Adopt:** a simple optional finite-domain macro operator with visible iteration
controls. **Adapt:** physical units, stable fault IDs and protected authored
regions. The smoothing example is a directional recursive filter; describe its
behavior rather than inheriting the chapter's FIR terminology. It does not
simulate water, sediment or geological uplift.

**Jason Shankel**, section 4.18, *Fractal Terrain Generation—Midpoint
Displacement*: [PDF pp. 495–499](../../references/Game%20Programming%20Gems%201.pdf#page=495),
printed pp. 503–507. Diamond-square alternates refinement stages and reads
neighboring diamond values; the example uses a wrapping boundary.

**Adopt:** explicit refinement order, roughness controls and source-domain edge
policy for an optional authoring stage. **Do not adopt:** running separately
seeded tile instances and assuming shared edges follow. Smooth pointwise noise
is the simpler initial runtime default; diamond-square remains useful as a
baked finite-domain operator.

**Jason Shankel**, section 4.19, *Fractal Terrain Generation—Particle
Deposition*: [PDF pp. 500–503](../../references/Game%20Programming%20Gems%201.pdf#page=500),
printed pp. 508–511. Deposited particles settle against previously deposited
terrain; a connected flood fill restricts a caldera inversion.

**Adopt:** optional localized volcanic/heap authoring and connected-region edits.
**Adapt:** deterministic particle/update order, travel/particle/fill limits and
explicit boundaries. Addressable random numbers do not make a stateful surface
update independent of scheduling. Keep this out of ordinary collision demand.

Thanks to these authors for the regeneration and practical heightfield tools.
Ludus adopts their design ideas, not their sample implementations or their broad
claims about realism/infinity.

## Interlocking grids

**Greg Snook**, GPG 2 section 4.2, *Simplified Terrain Using Interlocking
Tiles*: [PDF pp. 361–367](../../references/Game%20Programming%20Gems%202.pdf#page=361),
printed pp. 377–383. Figure 4.2.3 on physical p. 365 shows the 16 body masks;
the chapter also supplies separate links down to several lower detail levels.

**Adopt:** grid order and reusable index topology, with the finer side doing
the boundary work. **Adapt:** limit neighbors to one level and combine bodies,
links and corners into one complete pattern per mask. This reduces topology
cases and per-patch submissions. Validate every mask, corner, winding and shared
position. The chapter's sample distance selector is not an error certificate;
use cooked errors and an explicit visual quality policy.

## Heightfield normals

**Jason Shankel**, GPG 3 section 4.2, *Fast Heightfield Normal Calculation*:
[PDF pp. 333–337](../../references/Game%20Programming%20Gems%203.pdf#page=333),
printed pp. 344–348. Physical p. 335 explicitly discusses the equal-quad-face
assumption; p. 336 simplifies the neighbor-height expression.

**Adopt:** exploit regular spacing rather than accumulating arbitrary mesh faces.
**Adapt:** divide differences by actual X/Z spacing, normalize safely and keep
the shading normal separate from triangle contact geometry. Unequal spacing,
steep faces and triangulation defeat an interpretation as the exact average
normal of any mesh. Canonical neighbor halos, not clamped tile borders, feed
the calculation.

## Geometry clipmaps

**Arul Asirvatham and Hugues Hoppe**, GPU Gems 2 chapter 2, *Terrain Rendering
Using GPU-Based Geometry Clipmaps*, sections 2.1–2.4:
[publisher-hosted chapter](https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-2-terrain-rendering-using-gpu-based-geometry).

Nested grids reuse footprints, refresh moving strips and transition toward
coarser geometry. Upsampling and normal preparation need neighboring data.
**Adopt:** constant grid footprints, wider coarse coverage and explicit update
support. **Defer:** moving toroidal per-view caches until a flight/continuous
terrain benchmark justifies them. Their prefiltered pyramid differs from the
selected nested geometric decimation. Fine/coarse transitions need an explicit
seam policy. The historical packing, filter choices and throughput results are
not portable Ludus contracts.

The alternative is also grounded in **Frank Losasso and Hugues Hoppe**,
*Geometry clipmaps: Terrain rendering using nested regular grids*, ACM TOG
23(3), 2004 ([author project](https://hhoppe.com/proj/geomclipmap/)).

## Smooth noise

**Ken Perlin**, GPU Gems 1 chapter 5, *Implementing Improved Perlin Noise*,
sections 5.2–5.4:
[publisher-hosted chapter](https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-5-implementing-improved-perlin-noise).

**Adopt:** quintic interpolation with zero endpoint first/second derivatives
and a balanced gradient basis. **Adapt:** one specified 2D scalar reference with
addressed gradients, world-domain limits and frequency controls. A smooth basis
does not prevent aliasing after unlimited fBm/warping. Version the result and
keep shading detail separate from authoritative geometry. CPU/GPU agreement
needs explicit comparisons, not identical function names.

## Density terrain

**Ryan Geiss**, GPU Gems 3 chapter 1, *Generating Complex Procedural Terrains
Using the GPU*, sections 1.2 and 1.6:
[publisher-hosted chapter](https://developer.nvidia.com/gpugems/gpugems3/part-i-geometry/chapter-1-generating-complex-procedural-terrains-using-gpu).

**Adopt:** density fields are a distinct representation for overhangs, and wider
coarse blocks can reduce block-management cost. **Defer:** GPU-only density
authority and the sample geometry-shader path. The described overlapping/fading
LOD approach is not a watertight physical transition mesher. Endpoint density
tests can miss a thin feature crossed between them; a generic density is not a
distance bound for safe stepping. A future volume system needs its own topology,
transition and swept-collision contract. Mesh attachments solve the initial
local cave requirement with less new infrastructure.

## Artist control and surface materials

**Ferenc Pintér**, GPU Pro 2 chapter II.3, *Large-Scale Terrain Rendering for
Outdoor Games*: [PDF pp. 96–112](../../references/GPU%20Pro%202.pdf#page=96),
printed pp. 77–93. Physical p. 101 discusses local paint and warns that numeric
soil-index interpolation introduces intermediate soil types.

**Adopt:** procedural base soil, persistent manual painting, mesh details,
source/import workflow and matching materials at joins. **Adapt:** explicit
material identities and weights, ordinary texture arrays, full filtered mip
chains and bounded palettes. **Do not adopt:** interpolation of numeric IDs or
truncating the mip chain to hide wrapping seams. Its mesh-based representation
is a credible alternative for static canyon terrain, not proof that all editable
ground should be mesh-only. Console timings remain historical examples.

## Deformation and derived dependencies

**Egor Yusov**, GPU Pro 3 chapter I.2, *Real-Time Deformable Terrain Rendering
with DirectX 11*: chapter [PDF pp. 24–49](../../references/GPU%20Pro%203.pdf#page=24),
printed pp. 13–38. Reviewed architecture/error/normal sections 2.1–2.4 and
material/modification sections 2.5–2.6, physical pp. 24–45, plus implementation
and performance discussion. Physical p. 33 explains border normals; pp. 44–45
explain modifications while a coarse node is displayed.

**Adopt:** account for reconstruction error, neighboring normal samples,
material identity, and propagation to coarse levels. Edits must reach the
finest representation before bottom-up rebuilding. **Adapt:** CPU-authoritative
source, immutable candidates, atomic physical publication, canonical halos at
all prepared levels and conservative errors against the actual triangle/stitch
surface. **Defer:** predictive compressed hierarchies, GPU-authoritative edits,
hardware tessellation and GPU decode. The historical UNORM-filter differences
reinforce validating decode behavior; they do not establish current device rules.
Skirts and bilinear error estimates do not substitute for interior seam or
physical-topology correctness.

## Virtual texture prerequisites

**Matthäus G. Chajdas, Christian Eisenacher, Marc Stamminger and Sylvain
Lefebvre**, GPU Pro 1 chapter III.4, *Virtual Texture Mapping 101*:
[PDF pp. 198–207](../../references/GPU%20Pro%201.pdf#page=198),
printed pp. 185–194. Physical pp. 200–204 cover fallback/page handling;
pp. 204–205 explain gradients, gutters and compression-related border seams.

**Adopt for an optional backend:** bounded ancestor-first updates, pinned parent
fallback, derivative calculation before cache remapping, filter-sized gutters
and aligned compression blocks. **Adapt:** revision-bound pages, multi-view
demand, GPU completion before slot reuse and byte budgets. The sample's LRU,
tile dimensions, JPEG transcode and frame timings are not defaults. Page-count
limits alone do not bound variable decode or upload work. Ordinary textures
remain the initial terrain material path.

## Catalogued extensions not used as algorithm evidence

| Candidate | Reason to revisit |
| --- | --- |
| GPG 3, *Methods for Dynamic, Photorealistic Terrain Lighting* | Renderer lighting/shadow feature; does not determine canonical terrain or collision |
| GPG 4, *Terrain Occlusion Culling with Horizons* | Specialized heightfield visibility; benchmark after baseline culling and validate holes/dynamic geometry |
| GPG 6, *GPU Terrain Rendering*; GPU Pro 2, hardware tessellation | Backend optimization; current public RHI lacks the general resources and feature contract |
| GPG 7 clipmapping/large texture chapters; ShaderX terrain geomorphing | Optional cache/transition choices; no extra renderer required before measured need |
| GPG 8, *Road Creation for Projectable Terrain Meshes* | Road adapter extension after source stamp and geometry preparation contracts |
| ShaderX5, *Interactive Hydraulic Erosion on the GPU* | Separate accelerated solver after a CPU reference, domain/stability and conservation tests |
| GPU Pro 1, *Destructible Volumetric Terrain* | Separate product direction if pervasive digging becomes a requirement |

These titles were located in the index but their chapter contents were not used
to assert an algorithm here. The selected chapters already justify the concrete
seam, generation, authoring and backend refinements. Supporting recent author
research and Epic documentation are cited at the relevant decisions in the
architecture, with research extensions separated from shipped capability.

## Post-baseline research queue

Status: deferred evaluation plan, agreed on 2026-10-06. Implement and validate
[the selected T0–T5 baseline](terrain.md#performance-and-delivery-gates) first.
The baseline includes the Gems-driven refinements already specified above.
Research-driven improvements follow that delivery; this queue adds no baseline
algorithm, representation, backend or acceptance dependency. T6 remains optional.

Before opening an improvement experiment, record the baseline's accepted targets,
representative terrain/source fixtures, correctness results, image captures,
cook/edit/streaming tail times and peak memory. Resolve baseline acceptance
failures through the existing milestones. Completing this reading queue is not
a prerequisite for implementing those milestones.

### Venues to consult

The priority below is a Ludus engineering judgment, based on the venues' scope
and the concrete terrain work linked here. Consult papers, supplementary
material and author code where available; a venue's reputation alone is not
implementation evidence.

| Priority | Venue | Evaluation focus |
| --- | --- | --- |
| 1 | Eurographics and **Computer Graphics Forum**, especially [State-of-the-Art Reports](https://diglib.eg.org/collections/c400c5c1-b64e-4618-8802-baf78ce50641) | Generation, erosion, authoring control and measurable terrain descriptors |
| 2 | [SIGGRAPH / SIGGRAPH Asia](https://www.siggraph.org/siggraph-events/conferences/) and **ACM Transactions on Graphics** | Multiscale terrain detail, physical generation, rivers and alternative representations |
| 3 | GDC Programming, starting with [Far Cry 5 terrain rendering](https://www.gdcvault.com/play/1025261/Terrain-Rendering-in-Far-Cry) | Shipped-engine LOD, stitching, streaming and integration with cliff geometry |
| 4 | [I3D: Interactive 3D Graphics and Games](https://i3dsymposium.org/) | Interactive rendering and real-time algorithms |
| 5 | [High-Performance Graphics](https://highperformancegraphics.org/2026/call-for-participation/) | Geometry/LOD, compression, bandwidth and GPU execution after baseline profiling |

### Reading and experiment order

These are candidates for future full technical review, not additional adopted
algorithms. Bibliographic records, abstracts and author descriptions informed
selection. The earlier design already flags analytical erosion and contour
input as conditional extensions; their inclusion here does not expand that
initial implementation scope.

1. **Eric Galin, Eric Guérin, Adrien Peytavie, Guillaume Cordonnier, Marie-Paule
   Cani, Bedrich Benes and James Gain**, *A Review of Digital Terrain Modeling*,
   Computer Graphics Forum 38(2), Eurographics 2019
   ([publisher record](https://diglib.eg.org/items/47f03175-46e8-44e8-94ef-663bef354d1c)).
   Review the representation/generation landscape against the completed baseline
   and identify a specific unmet need before proposing another subsystem.
2. **Oscar Argudo, Eric Guérin, Hugo Schott and Eric Galin**, *Terrain Descriptors
   for Landscape Synthesis, Analysis and Simulation*, Computer Graphics Forum
   44(2), Eurographics 2025
   ([publisher record](https://diglib.eg.org/items/a7833e48-561c-4607-a704-458bf0a7f3f0)).
   First improvement candidate: choose useful slope, drainage and accessibility
   diagnostics, test their runtime/cook cost and assess whether cheaper proxies
   preserve the decisions artists or placement consumers need to make.
3. **Hugo Schott, Eric Galin, Eric Guérin, Adrien Peytavie and Axel Paris**,
   *Terrain Amplification using Multi-scale Erosion*, ACM Transactions on Graphics
   43(4), SIGGRAPH 2024
   ([author project](https://aparis69.github.io/public_html/projects/schott2024_Erosion.html)).
   Evaluate an optional offline amplification stage against the baseline's
   authored macro shapes, hydrology, detail quality, edit control and cook cost.
4. **Petros Tzathas, Boris Gailleton, Philippe Steer and Guillaume Cordonnier**,
   *Physically-based Analytical Erosion for fast Terrain Generation*, Computer
   Graphics Forum 43(2), Eurographics 2024
   ([publisher record](https://diglib.eg.org/items/0a412929-2e29-4bcb-a597-1a4b2cc558e2)).
   Review network/elevation coupling, convergence and boundary behavior before
   comparing it with the baseline solver. Keep it an offline stage experiment.
5. **Benoît Huftier, Hugo Schott, Eric Galin, Oscar Argudo, Adrien Peytavie and
   Eric Guérin**, *Terrain Synthesis and Authoring based on Iso-Contours*,
   Computer Graphics Forum 45(2), Eurographics 2026
   ([author project](https://h-schott.github.io/p/isos/)).
   Prototype a source/import adapter; compare editing effort and reconstruction
   quality while retaining the established heightfield cooker and runtime.
6. **Jeremy Moore / Ubisoft**, *Terrain Rendering in Far Cry 5*, GDC Programming,
   2018 ([session](https://www.gdcvault.com/play/1025261/Terrain-Rendering-in-Far-Cry)).
   Review the production GPU LOD/culling/stitching and cliff integration choices
   against measured T3/T5 bottlenecks. Historical platform timings are not Ludus
   budgets; preserve the CPU reference and legal coverage contract.

### Admission of an improvement

Each experiment names one baseline limitation, the consulted sections and a
falsifiable expected benefit. Run it on the same fixtures and targets as the
baseline; report quality/error, tail latency, total live/scratch/retired bytes,
authoring cost and implementation/debugging complexity as applicable. Include
negative cases and retain the baseline comparison path.

Promote a result only after its relevant seam, hole, collision, reproducibility,
publication, bounded-work and resource-lifetime checks pass. Document tradeoffs,
new prerequisites, source attribution and any asset migration in a separate
reviewed change. A renderer speedup cannot silently change canonical geometry;
a generation improvement cannot silently change existing recipes or saves.
Inconclusive or unfavorable experiments remain research notes and do not become
mandatory runtime complexity. Mark readings and experiments complete only when
their evidence exists; the queue currently records no completed experiment.
