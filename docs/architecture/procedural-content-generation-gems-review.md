# Procedural content generation Gems and research review

Status: reference evidence for the proposed
[procedural content generation architecture](procedural-content-generation.md).
The chapter revisions are Ludus adaptations. The conference/journal section
is a planned research queue tied to the architecture's P0-P5 delivery gates;
venue discovery is separate from reading and qualifying a specific technique.

## Baseline and review scope

The baseline used versioned declarative recipes, native typed stages, semantic
plans before geometry, offline cooking, bounded runtime refinement, addressed
randomness, immutable candidates, stable overrides and a serial debugger path.
It separated domain generators from world, renderer, animation and dynamics
owners. That baseline was recorded before the procedural chapter review.

Discovery used [game-dev-gems-toc.md](../../references/game-dev-gems-toc.md),
searching procedural generation, terrain, noise, vegetation, roads, walls,
clothing and vehicles. Selected chapter text was then read. Printed pages and
one-based physical PDF pages are reported separately; the source index is a
locator, not evidence for a chapter's technical claims.

Thanks to the authors credited below for the ideas and concrete failure cases.
No chapter implementation is copied. Local books, extracted text and rendered
reference pages remain outside the public documentation tree. Published GPU Gems
chapter pages supply public links for those sources.

## Changes to the baseline

| Concern | Baseline | Refinement incorporated in the architecture |
| --- | --- | --- |
| Regeneration | Stable object IDs and versioned recipes | Preserve child/property identity, pin old bases and reject ambiguous address packing |
| Terrain mesh LOD | Hierarchical cells with seam validation | Start with reusable tile topology and explicit supported transition cases |
| Noise | Versioned fields | Select improved-Perlin reference semantics and validate derivatives, period and filtering |
| Volume boundaries | Declared neighborhood halos | Include normal, shading and displacement support; qualify extraction separately |
| Roads | Parent-planned networks and local refinement | Separate grade/cross-slope validation and reversible grading layers from base terrain |
| Walls and settlements | Typed assembly over semantic plans | Treat protected territory, movement and buildability as distinct inputs |
| Vegetation | Fields, scatter and instancing | Resolve cross-layer occupancy; share interactive placements across rendering and collision |
| Terrain presentation | Optional GPU detail | Keep clipmap/LOD residency separate from authoritative terrain and collision |
| Garments | Offline fitting and separate dynamics | Cook explicit connectivity/render bindings and retain a CPU simulation reference |

The common core remains small. These refinements strengthen data contracts and
domain operators; they do not require a general solver, geometry-shader backend,
new language, commercial DCC runtime or engine-wide procedural object hierarchy.

## Regeneration and nested identity

**Guy W. Lecky-Thompson**, “Predictable Random Numbers,” *Game Programming
Gems 1*, §2.0, printed pp. 133-140; reviewed physical PDF pp. 129-136.
[Local chapter](../../references/Game%20Programming%20Gems%201.pdf#page=129),
[index entry](../../references/game-dev-gems-toc.md#L6713).

The chapter generates nested universe objects and properties from reproducible
sequences and discusses interaction/history as generation inputs. This supports
regenerating a base and storing mutations separately, while preserving identity
through nested refinement.

Retain that principle and use Ludus's existing Philox addressed interface.
Reject global `srand`/`rand`, replaying preceding draws to reach an object,
unchecked coordinate seed arithmetic and modulo range mapping as modern
contracts. Property addresses need stable semantic slots, and a seed does not
identify the algorithm or asset revision. The chapter's arithmetic examples do
not prove unique seed addresses or the period claimed from integer width.

The architecture consequently requires canonical child tuples, checked compact
address mappings, exact base manifests and explicit identity migration. See the
existing [randomness review](randomness-gems-review.md#procedural-generation)
for the shared foundation contract.

## Terrain boundaries and noise

**Greg Snook**, “Simplified Terrain Using Interlocking Tiles,” *Game Programming
Gems 2*, §4.2, printed pp. 377-383; reviewed physical PDF pp. 361-367.
[Local chapter](../../references/Game%20Programming%20Gems%202.pdf#page=361),
[index entry](../../references/game-dev-gems-toc.md#L6834).

The chapter reuses regular-grid index templates and adds linking pieces between
different tile detail levels. Adopt fixed topology and explicit boundary cases
for the first height-field renderer. Ludus restricts adjacent detail differences
to its supported transitions and tests each orientation. It does not adopt the
DirectX 8 sample API, triangle-fan assumptions or historical speed comparisons.
Matching boundaries is an acceptance gate, rather than a visual workaround.

**Ken Perlin**, “Implementing Improved Perlin Noise,” *GPU Gems 1*, chapter 5,
especially §§5.3-5.4.
[Publisher-hosted chapter](https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-5-implementing-improved-perlin-noise),
[index entry](../../references/game-dev-gems-toc.md#L8262).

The improved interpolant removes endpoint second-derivative discontinuities;
the chapter also revises the gradient set to reduce artifacts. Adopt a pinned
reference noise kernel and derivative fixtures. Period, hash/gradient selection,
sampling footprint and CPU/GPU tolerances remain explicit Ludus contracts.
The older texture-based shortcuts are candidates for cosmetic profiles, not
proof of identical authoritative geometry on every platform.

**Ryan Geiss**, “Generating Complex Procedural Terrains Using the GPU,”
*GPU Gems 3*, chapter 1; reviewed §§1.2-1.3, §1.4.1 and the relevant texturing
and application discussion.
[Publisher-hosted chapter](https://developer.nvidia.com/gpugems/gpugems3/part-i-geometry/chapter-1-generating-complex-procedural-terrains-using-gpu),
[index entry](../../references/game-dev-gems-toc.md#L8407).

Density fields enable overhangs and caves; extra samples outside a block support
boundary shading. Adopt a field/extraction separation and support-aware halos.
Ludus defaults to height fields and adds volumes for concrete requirements.
Meshing ambiguity and mixed-resolution joins need independent qualification.
A density field is not necessarily a distance metric. Replace geometry-shader
stream output with whichever measured CPU or future compute backend fits Ludus.

## Terrain presentation and residency

**Arul Asirvatham and Hugues Hoppe**, “Terrain Rendering Using GPU-Based Geometry
Clipmaps,” *GPU Gems 2*, chapter 2; reviewed §§2.1-2.4 and its transition discussion.
[Publisher-hosted chapter](https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-2-terrain-rendering-using-gpu-based-geometry),
[index entry](../../references/game-dev-gems-toc.md#L8326).

Nested regular windows, reused footprints and transition regions provide a
bounded presentation structure for extensive height fields. Reserve clipmaps
as an optional renderer over the same terrain products. Camera movement shifts
visual residency without regenerating semantic terrain or choosing collision
resolution. The chapter's old vertex-texture restrictions and frame rates are
not present-day performance requirements. Tile and clipmap backends must pass
the same boundary and visual-transition fixtures.

## Roads and reversible terrain editing

**Igor Borovikov and Aleksey Kadukin**, “Road Creation for Projectable Terrain
Meshes,” *Game Programming Gems 8*, §4.11, printed pp. 453-461; reviewed physical
PDF pp. 468-476.
[Local chapter](../../references/Game%20Programming%20Gems%208.pdf#page=468),
[index entry](../../references/game-dev-gems-toc.md#L7412).

The chapter combines cost-based centerline refinement with road grade and cross
slope control. It explicitly identifies the terrain scars left when a graded
road moves and proposes a separate road height layer.

Adopt editable centerlines, explicit grade/cross-slope constraints and reversible
grading products. Route layout is upstream of local meshing. Preserve the base
height and recompute bounded influences when roads change. Crossings and
junctions have canonical ports and ownership. Bound local optimization and
retain authored controls; the method's local minima do not guarantee an optimal
network. Its height-map approach does not solve bridges, tunnels or arbitrary
volumetric terrain by itself.

## Architecture and territory boundaries

**Mario Grimani**, “Advanced Wall Building for RTS Games,” *Game Programming
Gems 4*, §4.7, printed pp. 365-372; reviewed physical PDF pp. 366-373.
[Local chapter](../../references/Game%20Programming%20Gems%204.pdf#page=366),
[index entry](../../references/game-dev-gems-toc.md#L7002).

The chapter derives walls from protected territory and handles existing walls,
shorelines and destructible barriers. Its distinction between traversable and
buildable terrain is useful beyond an RTS wall generator.

Adopt separate movement/buildability capabilities, required interior footprints
and connectivity validation before wall geometry. Reuse existing authored
structures through explicit input records. Any greedy perimeter operator has
bounded expansion, deterministic ordering and final acceptance tests. Defer
the full RTS construction policy until a game needs it; local heuristic choices
are not a proof of globally optimal walls, roads or building layouts.

## Vegetation placement and collision

**Josh Klint**, “Vegetation Management in Leadwerks Game Engine 4,” *Game Engine
Gems 3*, chapter 4, printed pp. 53-71; reviewed physical PDF pp. 55-73.
[Local chapter](../../references/Game%20Engine%20Gems%203.pdf#page=55),
[index entry](../../references/game-dev-gems-toc.md#L6020).

The chapter regenerates compact instance placement for rendering and physics,
uses detail representations and identifies overlap among independently generated
vegetation layers as a limitation.

Adopt shared interactive placements, explicit stratum occupancy and ordinary
instance data instead of an entity/matrix hierarchy per plant. CPU/cooked
placements determine collider and visible trunk position. Collision/query
residency follows simulation demand. Cosmetic GPU detail is separate.

Reject an assumption of unlimited, effectively zero per-instance storage: mutable
trees require state, identity and residency budgets. Replace repeating variation
tables where they cause visible repetition. Avoid synchronous visibility-count
readbacks in the default design and qualify future GPU compaction end to end.
Historical hardware results and simple frustum-only culling do not select the
current renderer architecture.

## Garments and live cloth

**Marco Fratarcangeli**, “GPGPU Cloth Simulation Using GLSL, OpenCL, and CUDA,”
*Game Engine Gems 2*, chapter 22, printed pp. 369-383. Reviewed physical PDF
pp. 385-390 and 396-399 for the numerical model, collision, storage, performance
discussion and connectivity extension.
[Local chapter](../../references/Game%20Engine%20Gems%202.pdf#page=385),
[index entry](../../references/game-dev-gems-toc.md#L5910).

The chapter compares execution backends for a rectangular mass-spring cloth and
separates particle attributes into buffers. It identifies explicit connectivity
as the route toward arbitrary shapes.

Adopt separate rest/connectivity data, live state and render data, with a CPU
reference. Garment panels, seams, fitting, layering and render bindings require
additional content contracts. The rectangular spring grid does not supply them.
Use the modern XPBD qualification described in the architecture when dynamics
is required. The old backend rankings and numerical accuracy assertions do not
choose Ludus's solver or establish collision robustness.

## Current production cross checks

These sources informed the initial baseline or its modern boundaries; they are
distinct from the subsequent Gems chapter review:

| Primary source | Idea used and Ludus boundary |
| --- | --- |
| Epic Games, [Using PCG Generation Modes](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-pcg-generation-modes-in-unreal-engine) | Multiple generation scales and residency sources inform parent plans and local refinement; scheduling cannot change the base. |
| Epic Games, [Using PCG with GPU Processing](https://dev.epicgames.com/documentation/unreal-engine/using-pcg-with-gpu-processing-in-unreal-engine?lang=en-US) | Group GPU work and account for transfer boundaries; GPU execution remains optional. |
| SideFX, [Introduction to PDG and TOPs](https://www.sidefx.com/docs/houdini/tops/intro.html) and [Custom File Tags and Handlers](https://www.sidefx.com/docs/houdini/tops/custom_tags.html) | Explicit work/dependencies and cache validation inform cooking; no Houdini runtime is required. |
| Pascal Müller, Peter Wonka, Simon Haegler, Andreas Ulmer and Luc Van Gool, [Procedural Modeling of Buildings](https://doi.org/10.1145/1179352.1141931), ACM TOG 25(3), 2006, pp. 614-623 | Hierarchical CGA shape rules inform envelope/facade assembly; gameplay interiors have additional constraints. |
| Paul Merrell, [Example-Based Model Synthesis](https://paulmerrell.org/wp-content/uploads/2021/06/model_synthesis.pdf), I3D 2007, pp. 105-112 | Example-based local arrangement is an optional operator; whole-layout gameplay validation remains necessary. |
| Robert Bridson, [Fast Poisson Disk Sampling in Arbitrary Dimensions](https://www.cs.ubc.ca/~rbridson/docs/bridson-siggraph07-poissondisk.pdf), SIGGRAPH 2007 sketch | A bounded finite-region sampling candidate; independent active-list runs do not establish streamed-cell consistency. |
| Epic Games, [Create a MetaHuman Parametric Outfit Asset](https://dev.epicgames.com/documentation/metahuman/create-a-metahuman-parametric-outfit-asset) | Body/cloth pairing and re-evaluation of dependencies inform fit families and cache invalidation. |
| Miles Macklin, Matthias Müller and Nuttapong Chentanez, [XPBD: Position-Based Simulation of Compliant Constrained Dynamics](https://matthias-research.github.io/pages/publications/XPBD.pdf), MiG 2016, pp. 49-54, DOI 10.1145/2994258.2994272 | Explicit compliance informs a potential dynamics solver; generation produces assets and does not own live cloth. |

This is a selection of applicable techniques, not a claim that historical Gems
or one current tool establishes a universally best PCG system. The architecture
sets reproducibility, correctness, authoring and cost gates for alternatives.

## Conference and journal research plan

Use the implemented serial generator and its failure/quality corpus as the
comparison baseline. Begin with GDC production workflows, SIGGRAPH modeling
research and the PCG Workshop's control/evaluation work. Use specialist venues
when a domain or measured bottleneck calls for them. This order reflects Ludus's
needs, not an academic ranking or a commitment to attend an event.

The linked 2026 programs identify concrete discovery sources. Follow the relevant
edition when the work begins. The questions and gates below are Ludus's proposed
applications of the venues' scope; they are not claims that a venue supplies a
ready-to-use engine subsystem.

### Conferences mapped to delivery slices

| Venue and discovery source | Ludus question and owner | Delivery gate for a candidate improvement |
| --- | --- | --- |
| [GDC technical talks](https://www.gdcvault.com/play/1024120/GPU-Based-Run-Time-Procedural) | P1 authoring and P5 placement: can artists preserve intent while a generator fills local detail, and can production capture/replay explain failures? Recipe document, environment adapter and coordinator own these tasks. | Complete lock/paint/undo/rejection tasks with recorded intervention count, correct explanations and edit latency; preserve static/dynamic ownership and save identity. |
| [SIGGRAPH and SIGGRAPH Asia](https://www.siggraph.org/siggraph-events/conferences/), [technical papers](https://s2026.siggraph.org/program/technical-papers/) | P2 architecture and P3-P4 asset families: can shape grammars, example-based synthesis or geometric construction improve valid variety within our authored envelope? Domain kernels and cooker own the algorithm. | Compare valid output diversity and artist effort at equal hard constraints; qualify topology, correspondence, numerical profile and source/license provenance. |
| [PCG Workshop at Foundations of Digital Games](https://www.pcgworkshop.com/database.php) | P1 controls and P2 playable layouts: which constraints, mixed-initiative operations and expressive-range measures expose useful variation? Authoring adapter and semantic validators own the policy. | Measure controllability, constraint violations, connectivity and diversity on repeatable fixtures; inspect failed attempts without changing addresses. |
| [Eurographics Symposium on Geometry Processing](https://sgp26.org/) | P3-P4 characters/garments: can remeshing, parameterization, deformation and correspondence improve fit while keeping bindings valid? Character/garment cooker and shared format own the result. | Test topology/seam validity, fit-envelope boundaries, representative poses, UV quality, cook time and mapping errors across render/simulation LODs. |
| [Interactive 3D Graphics and Games](https://i3dsymposium.org/2026/papers.html) | P2-P5 interactive products: which representations or incremental algorithms improve generation/preview latency? Kernel and existing renderer adapter have separate owners. | Compare p95/p99 edits, scratch/result bytes and equal-quality output against the CPU reference, including invalidation and cancellation. |
| [High-Performance Graphics](https://www.highperformancegraphics.org/2026/call-for-participation/) | P5 measured acceleration: is bandwidth, dispatch, compaction, geometry representation or transfer the real cost? Kernel, renderer and RHI owners retain their boundaries. | Measure the complete generation-to-consumption path, including uploads/readbacks and rendering contention; retain WebGL CPU/direct fallback and product parity. |
| [Symposium on Computer Animation](https://computeranimation.org/) | P4 garment consumption: can deformation, cloth or skinning improve motion quality? Animation/dynamics own live state; generation supplies rest data and bindings. | Test fit and collision across pose/LOD/reset/teleport cases and bounded simulation cost; do not turn a visual solver into an authoritative generation dependency. |
| [IEEE Conference on Games](https://cog2026.org/schedule) | P2/P5 optional solvers: can constructive, search-based or learned generation satisfy gameplay constraints more reliably? The selected semantic operator owns the search. | Compare validity, playability and diversity with bounded attempts; validate required global routes separately and keep learned outputs behind the offline acceptance boundary. |

The [terrain research queue](terrain-gems-review.md#post-baseline-research-queue)
remains the owner for terrain-specific hydrology, erosion and LOD investigations.
The [renderer research plan](renderer-systems-gems-review.md#conference-and-journal-research-plan)
owns visibility, shading and device-performance investigations. Cross-domain PCG
work uses their products and evidence rather than maintaining duplicate plans.

### Journals and archive use

| Journal | Research use for Ludus |
| --- | --- |
| [ACM Transactions on Graphics](https://dl.acm.org/journal/tog), with [SIGGRAPH publication guidance](https://www.siggraph.org/inside-siggraph/publications/) | Detailed modeling, geometry, simulation and synthesis papers; distinguish journal and conference tracks and obtain the exact article/version. |
| [Computer Graphics Forum](https://www.eg.org/wp/eurographics-publications/cgf/) | Geometry algorithms and surveys, including Eurographics/SGP proceedings; useful before selecting a garment or mesh-processing algorithm. |
| [Proceedings of the ACM on Computer Graphics and Interactive Techniques](https://blog.siggraph.org/2021/05/nice-to-meet-you-introducing-the-proceedings-of-the-acm-in-computer-graphics-and-interactive-techniques-pacmcgit.html/) | Interactive and performance-oriented techniques; I3D and HPG programs provide article discovery without assuming runtime suitability. |
| [IEEE Transactions on Games](https://www.transactions.games/) | PCG, computational creativity and game evaluation; prioritize evidence about controllability/playability over attractive screenshots alone. |

### Developer control and transparency

Thanks to **Bojan Endrovski, Joris Dormans and Rafael Bidarra**,
*What game developers actually want from procedural level generation tools*,
Foundations of Digital Games 2026, 18 pages,
[DOI 10.1145/3815598.3815682](https://doi.org/10.1145/3815598.3815682),
[author-hosted record and paper](https://publications.graphics.tudelft.nl/papers/848).
Consulted the title page, methodology in section 3, control/preview findings in
section 4.3 and limitations in section 5.4.

Its 120-person survey supports prioritizing creative control, preview and process
transparency. Role categorization, question interpretation and the broad use of
“AI” limit generalization. Use this evidence to motivate an authoring evaluation;
it does not prove Ludus's usability or choose a generator algorithm.

The [P1 authoring evaluation](procedural-content-generation.md#authoring-debugging-and-external-tools)
uses Ludus-selected tasks and measurements. Evaluate those tasks separately with
designers and technical artists.

### Production placement starting point

**Jaap van Muijden / Guerrilla Games**, *GPU-Based Run-Time Procedural Placement
in Horizon: Zero Dawn*, GDC 2017,
[session record](https://www.gdcvault.com/play/1024120/GPU-Based-Run-Time-Procedural).
The session overview describes artist-authored placement rules and runtime
assembly around the player. That overview identifies a useful full-talk reading
for P1/P5; it is not a completed review of the implementation, benchmarks or
collision policy.

During the full reading, compare the authoring-to-placement path with Ludus's
immutable inputs, occupancy, residency and replay contracts. Record where a
technique requires GPU-resident data or a capability beyond our selected profile.
Any adoption retains the coordinator/world/renderer boundaries and passes the
same CPU-reference and interactive-placement gates.

### Qualification and subsequent changes

1. Finish and validate the relevant baseline slice. Record its commit, input and
   output digests, identity/numeric profile, target hardware and quality corpus.
2. Select an observed failure or cost. Retrieve the exact paper/talk and available
   code/data; record authors, title, venue/year, DOI or stable URL, consulted
   sections and license. Venue prestige is not evidence of Ludus suitability.
3. Separate source findings from our hypothesis. Identify the owning stage and
   its dependencies, expected benefit, worst-case bounds and numerical behavior.
4. Implement a small comparison behind the same typed boundary. Include cold/hot
   cost, incremental edits, peak bytes, valid output quality, parity, cancellation
   and author intervention; retain negative fixtures and the serial reference.
5. Accept a focused improvement only when its relevant gate passes. Update the
   canonical owner and implementation attribution, record regressions and explain
   tradeoffs. Defer an inconclusive result without expanding the common core.

Record future completed readings and decisions here; the tables above do not
claim their papers have already been reviewed or that P0-P5 ship today.

## Conditional and excluded techniques

The index also locates fractal terrain generation, terrain reasoning, procedural
texturing, vehicle physics, crowd rendering and plant animation. Their titles
are discovery leads, not additional chapter-text evidence here. Read them when
implementing those kernels or consumer features. Vehicle simulation, sky blending
and rendering-specific optimizations do not replace vehicle assembly, regional
planning or garment fitting contracts.

Defer general-purpose solvers, mandatory visual scripting, runtime unrestricted
topology generation and learned generation services. Introduce them only with a
specific content requirement, a reproducible failure/quality corpus and measured
costs. This preserves the baseline's small common core while allowing demanding
domains to grow independently.
