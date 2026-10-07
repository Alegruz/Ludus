# Physics systems Gems and research review

**Status:** Design evidence, reviewed 2026-10-07 against `origin/main` at
`1e2227c`. No runtime implementation or benchmark is added by this review.
The [physics architecture](physics-systems.md) recorded its initial decision
table before searching [game-dev-gems-toc.md](../../references/game-dev-gems-toc.md).
The revised architecture incorporates the decisions below.

## Reading scope and evidence

The requested index was searched for physics, collision, rigid bodies,
constraints, queries, convex shapes, inertia, integration, sleep and transforms.
Relevant Gems articles were selected for concrete effects on maintainability,
robustness and debugging. The index also catalogs other books; they are not
silently treated as Gems articles or evidence of current performance.

Game Programming Gems 4 is scanned. PDF pages 247-268 were rendered and OCR
read; visual inspection verified authors, chapter headers, printed page numbers
and the mesh-edge discussion. Other selected chapters provide extractable text;
visual checks also confirmed the sleep test and swing/twist equation. All PDF
page numbers below are physical one-based pages; printed pagination is distinct.
Bookmark/index offsets are not reliable enough to infer printed pages.

The PDFs and TOC are in the ignored local `references/` library. The links below
require that library; fresh clones and offline documentation bundles do not
contain the books. No scans, excerpts, OCR output, CD source or historical
binaries are published with the proposal. Scratch reading material stays under
ignored `out/`. Summaries describe selected ideas; the complete contracts and
engineering departures are Ludus decisions.

Thanks to the credited authors for these techniques. Implementation work must
retain concise attribution near code materially informed by a reading, with
this review linked for details, as required by [AGENTS.md](../../AGENTS.md).
No algorithm or sample code was copied.

## Changes to the initial architecture

| Verified reading | Concrete improvement | Acceptance evidence |
| --- | --- | --- |
| Constraints in Rigid Body Dynamics | Named row/anchor frames; finite motor impulse limits and diagnostic saturation; explicit positional correction | Off-center anchors, force-limited motors, redundant rows and correction-energy tests |
| Fast Contact Reduction for Dynamics Simulation | Mesh adjacency cooking, compatible patch reduction, support-area retention and feature-based persistence | Tessellation-independent rest, seam sliding, opposing-normal separation and cache invalidation |
| Moments of Inertia for Common Shapes | Authored/body/COM frame separation and compound parallel-axis mass contract | Asymmetric compound, rotated inertia, overlapping mass-volume policy |
| A Jitter-Tolerant Rigid Body Sleep Condition | Motion-envelope qualification, angular observability, dwell/reset semantics | Jitter, rotation about each axis, support removal and full-turn alias guard |
| Fast Generic Ray Queries for Games | Shared gameplay query service with explicit static/dynamic coverage and collector guarantees | Query versus brute-force oracle, mixed scenery and local/world distance invariance |
| XenoCollide: Complex Collision Made Simple | Small convex representation seam; keep overlap/penetration distinct from distance/manifold needs | Degenerate support, bounded convergence and full query result semantics |
| Efficient Collision Detection Using Transformation Semantics | Named rigid frames and asset-baked scale; no generic matrix inverse per test | Local/world round trips, rejected shear/negative scale and normal/distance tests |
| Rotational Joint Limits in Quaternion Space | Quaternion sign equivalence, swing/twist convention, 180-degree singularity handling | Limit boundary/continuity tests and matching inspector visualization |
| Volumetric Hierarchical Approximate Convex Decomposition | Offline decomposition limits and gameplay clearance validation | Bounded hull count/complexity, cook diagnostics and authored passage tests |

The strongest additions are explicit contracts and adversarial scenes. The
review does not justify replacing a qualified backend, adding every historical
algorithm, or claiming that all proposed optimizations have been measured.

## Constraints in Rigid Body Dynamics

Russ Smith, section 3.4, *Game Programming Gems 4*.
Read PDF pages 247-257, printed pages 241-251.
[Chapter](../../references/Game%20Programming%20Gems%204.pdf#page=247).

The chapter explains building velocity constraints from scalar Jacobian rows,
including anchor shifting, joints and force-limited motors. It discusses drift
correction and the instability risk of overly aggressive correction.

**Adopt:** inspectable row semantics, explicit anchor-to-COM conversion and
finite motor authority. **Adapt:** the public API remains typed joint descriptors;
impulse limits use the actual solver duration, with units and saturation exposed.
**Do not copy:** the ODE binding or its numerical tuning into another backend.
Raw custom-row callbacks stay private until a real feature needs them.

## Fast Contact Reduction for Dynamics Simulation

Ádám Moravánszky and Pierre Terdiman, section 3.5, *Game Programming Gems 4*.
Read PDF pages 258-268, printed pages 253-263.
[Chapter](../../references/Game%20Programming%20Gems%204.pdf#page=258).

The chapter combines mesh feature preprocessing, contact grouping/reduction and
feature-key persistence. It emphasizes useful contact spread, deepest penetration
and correspondence across steps for warm starting.

**Adopt:** cooked internal-edge information, patch compatibility and persistence
qualification. **Adapt:** full collider incarnations/revisions, material boundaries,
conservative nonmanifold handling and backend-specific manifold capacities.
**Defer:** cube-map or k-means clustering until profiling identifies that problem.
**Do not copy:** the chapter's five-point selection as a universal cap, an unchecked
64-bit feature hash as identity, or aggressive concave-vertex removal without
proof for our mesh sidedness and topology.

## Moments of Inertia for Common Shapes

Eric Lengyel, chapter 14, *Game Engine Gems 1*.
Read sections 14.1-14.2 and the start of 14.3, PDF pages 287-293,
printed pages 259-265.
[Chapter](../../references/Game%20Engine%20Gems%201.pdf#page=287).

The chapter develops COM and inertia tensors, their frame translation and
primitive mass properties. Its translation formula assumes the original tensor
is about COM, an important precondition when assembling compounds.

**Adopt:** explicit mass frames, rotated tensors and parallel-axis composition.
**Adapt:** checked cooker arithmetic, tensor conditioning validation and an
explicit policy for overlapping mass volumes. **Do not infer:** that additive
compound leaves describe a geometric union's mass. Remaining primitive derivations
are useful implementation reading, not claimed to have been fully reviewed here.

## A Jitter-Tolerant Rigid Body Sleep Condition

Eric Lengyel, chapter 23, *Game Engine Gems 2*.
Read PDF pages 401-403, printed pages 385-387.
[Chapter](../../references/Game%20Engine%20Gems%202.pdf#page=401).

The test accumulates world-space motion bounds for COM and two noncollinear
body-fixed points. Their extent over a dwell window permits small jitter while
observing rotational motion. Bounds reset after excessive motion.

**Adopt:** qualify sleep with both translation and rotation, and expose the
window/threshold/reset decisions. **Adapt:** island wake dependencies, size-aware
sample offsets and a high-angular-motion veto to prevent sampling aliasing.
**Do not assume:** sampled envelopes detect arbitrary continuous trajectories or
that a historical one-second window is the right game tuning. Compare against
the chosen backend before introducing a second sleep algorithm.

## Fast Generic Ray Queries for Games

Jacco Bikker, section 2.2, *Game Programming Gems 7*.
Read PDF pages 160-174, printed pages 127-141.
[Chapter](../../references/Game%20Programming%20Gems%207.pdf#page=160).

The chapter treats rays as a general gameplay tool and examines static and
dynamic acceleration structures with different build/update costs.

**Adopt:** one clear physics query service with complete mixed-scene coverage,
local-space geometry and workload measurement. **Adapt:** use the selected
backend's dynamic hierarchy and cooked static BVHs first. Any/closest/all,
initial overlap, epoch and incomplete-result contracts are Ludus additions.
**Reject:** transferring its rays-per-second figures, pointer-tag packing or
claims of a universally best kD-tree to current hardware. Packet/SIMD queries
follow measurements of actual coherent and incoherent ray batches.

## XenoCollide: Complex Collision Made Simple

Gary Snethen, section 2.5, *Game Programming Gems 7*.
Read PDF pages 198-211, printed pages 165-178.
[Chapter](../../references/Game%20Programming%20Gems%207.pdf#page=198).

Support mappings give varied convex shapes a small representation interface;
Minkowski Portal Refinement provides geometric overlap/penetration reasoning.

**Adopt:** convex shape extensions should not require duplicating every pair
algorithm in the public architecture. **Adapt:** qualify the backend's own
narrowphase, feature and numerical-failure behavior. **Do not infer:** that MPR
alone provides closest distance, a stable multi-point manifold or rotational CCD.
Keep it a comparison candidate if an identified convex failure warrants one;
do not add it alongside working GJK/EPA simply because the chapter is readable.

## Efficient Collision Detection Using Transformation Semantics

José Gilvan Rodrigues Maia, Creto Augusto Vidal and Joaquim Bento Cavalcante-Neto,
section 2.6, *Game Programming Gems 7*.
Read PDF pages 212-223, printed pages 179-190 including the intervening/end pages.
[Chapter](../../references/Game%20Programming%20Gems%207.pdf#page=212).

The chapter exploits known transform structure rather than treating every
matrix as arbitrary. Its model includes a particular scale/rotation/translation
composition with nonzero scales.

**Adopt:** explicitly named frames and checked transform assumptions.
**Adapt:** runtime collision instances are rigid; scale and reflection are baked
and validated offline. **Do not copy:** its inverse formulas for hierarchies that
introduce shear, or normalize local rays in a way that changes world distances.
The initial runtime restriction keeps the implementation and debugger smaller.

## Rotational Joint Limits in Quaternion Space

Gino van den Bergen, chapter 10, *Game Engine Gems 3*.
Read PDF pages 116-131, printed pages 123-138.
[Chapter](../../references/Game%20Engine%20Gems%203.pdf#page=116).

The chapter discusses rotation parameterization, swing/twist limits and more
general quaternion-space limit volumes, including limitations of independent
rotation coordinates.

**Adopt:** quaternion equivalence and a named relative-orientation/axis convention.
**Adapt:** explicitly handle the zero denominator of the swing/twist decomposition
near a half turn, with continuity tests and matching limit visualization.
**Defer:** anatomical quaternion-volume constraints and their projection solvers.
A shoulder is not fully modeled by independent swing/twist bounds; the typed
backend joint is the initial mechanic, not a claim of anatomical fidelity.

## Volumetric Hierarchical Approximate Convex Decomposition

Khaled Mamou, chapter 11, *Game Engine Gems 3*.
Read PDF pages 133-148, printed pages 141-156; introduction and algorithm discussion.
[Chapter](../../references/Game%20Engine%20Gems%203.pdf#page=133).

The chapter uses a volumetric representation and hierarchical partitioning to
trade convex-approximation quality against representation complexity.

**Adopt:** decomposition belongs in offline cooking, with explicit quality and
hull-count/complexity controls. **Adapt:** validate narrow passages, support
surfaces and holes in gameplay units; volume error alone is not a bound on local
clearance or contact error. Save cooker/settings revisions. **Do not select:**
a historical V-HACD implementation without comparing currently maintained tools
at implementation time. Artists can provide authored convex proxies first;
automatic decomposition is optional, not a runtime dependency.

## Relevant leads deferred after index inspection

The following are useful entries in the requested TOC, but their chapter contents
were not reviewed for this change. No detailed algorithmic claim or adoption is
based on their titles alone.

| Index entry | When to read and what it might improve |
| --- | --- |
| Miguel Gomez, Integrating the Equations of Rigid Body Motion; Using Implicit Euler Integration for Numerical Stability, Gems 1 | If free-spin, stiffness or damping tests identify an integrator problem |
| Recursive Dimensional Clustering: A Fast Algorithm for Collision Detection, Gems 2, 2.7 | Broadphase candidate density/tree-update bottlenecks after measurement |
| Compressed Axis-Aligned Bounding Box Trees, Gems 2, 4.4 | Static-mesh memory/traversal pressure, with conservative-bound verification |
| Vehicle Physics Simulation for CPU-Limited Systems; Writing a Verlet-Based Physics Engine, Gems 4, 3.2-3.3 | A vehicle or positional-constraint consumer with its own fidelity/cost requirements |
| Fast Collision Detection for 3D Bones-Based Articulated Characters, Gems 4, 5.14 | Ragdoll/character collision shapes and animation-to-physics synchronization |
| Exact Buoyancy for Polyhedra; Real-Time Particle-Based Fluid Simulation with Rigid Body Interaction, Gems 6, 2.5-2.6 | Fluid-solid coupling after the existing field sampling contract is established |
| Rahul Sathe and Dillon Sharlet, Fast Rigid-Body Collision Detection Using Farthest Feature Maps, Gems 7, 2.3 | Measured convex support-search cost on a workload that repays extra memory/cooking |
| Improved Numerical Integration with Analytical Techniques; What a Drag: Modeling Realistic Three-Dimensional Projectiles; Approximate Convex Decomposition for Real-Time Collision Detection, Gems 8, 2.5-2.6 and 2.8 | Analytic force models or a specific cook-quality problem |
| Richard Tonge, Ben Wyatt and Ben Nicholson, PhysX GPU Rigid Bodies in Batman: Arkham Asylum, Gems 8, 7.2 | A separate high-count cosmetic GPU workload and platform/readback budget |

The existing [fluid reference review](physics-fluid-reference-review.md) owns its
water readings and scope. General rigid-body design does not overwrite those
small-game or fluid-field choices.

## Current research and follow-up

The architecture also consulted current primary Jolt/PhysX/Box2D documentation,
Catto's Solver2D and temporal-coherence work, and *Small Steps in Physics
Simulation*. Their exact links, attribution and applicability limits are in the
[architecture's evidence sections](physics-systems.md#primary-evidence).
No historical Gems timing is used as a modern budget or proof of SOTA.

After P0-P3 have produced a validated baseline, prioritize experiments by observed
failures: substeps versus iterations at matched cost; stable contact patches
versus mesh tessellation; existing versus envelope-informed sleep; static BVH
cooking quality versus load time; serial versus parallel scheduling; and incoherent
versus coherent query batches. Measure quality, tail latency, memory and debugger
cost together. Adopt only improvements that preserve the declared contracts and
justify their maintenance cost. Robust production behavior is the objective;
algorithm novelty by itself is not an acceptance gate.
