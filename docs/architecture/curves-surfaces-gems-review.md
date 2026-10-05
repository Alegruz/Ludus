# Curves and surfaces: Gems and research review

Review date: 2026-10-04. Repository baseline: `52e29ce`.
The [architecture](curves-surfaces.md) now has a G1 math kernel implementation;
the later phases remain proposed. See the [kernel evidence](math-evidence/curves-surfaces.md).

The initial architecture was written before reading the chapters discovered
through [game-dev-gems-toc.md](../../references/game-dev-gems-toc.md).
It selected Bezier kernels, immutable preparation, separate domains/caches and
consumer adapters. The review then read relevant chapter contents and updated
the final contracts. The preserved baseline and final revision table are in
the architecture document.

## Evidence and selection

The ignored local `references/` tree contains the PDFs linked below. Page
numbers are one-based physical PDF pages; printed numbering is recorded
separately. Bookmarks and the combined index located articles but were not
treated as evidence of algorithms.

Game Programming Gems 2, Graphics Gems 1, GPU Pro 1 and GPU Zen 2 provide
extractable text. Game Programming Gems 3 and 4 and Graphics Gems 5 are scans:
selected chapters were rendered and read through OCR, with chapter identity and
key claims visually checked. Graphics Gems 1's copy emits structural extraction
warnings; the cited chapter text is readable, and its page/printed numbers were
checked from the extracted page headings. No companion-CD implementations were
reviewed or copied. These are original paraphrases and Ludus adaptations.

The index was searched across curve, spline, Bezier, NURBS, arc length, frame,
patch, subdivision and tessellation entries. The selected readings cover actual
numerical/architectural decisions. Water, implicit fields and general terrain
are adjacent systems, not justification for one universal curve/surface module.
Supporting modern sources were checked online through primary author/project
sites.

## Resulting changes

| Reading | Useful finding | Concrete architecture consequence |
| --- | --- | --- |
| Gravesen, length of Bezier curves | Chord/polygon bounds differ from faster error estimates | Whole-path length interval, prefix uncertainty and strict bounded inversion |
| de Figueiredo, adaptive curve sampling | Deterministic probing can alias; random probing is heuristic | Full control-hull/interior tests for strict profiles; bounded deterministic refinement |
| Lowe, nonuniform splines | Segment duration rescales derivatives; C2 constraints form a solve | Domain-aware tangents/derivatives, explicit timed C2 mode, cursor lookup |
| Dougan, transport frame | Frames depend on earlier frames; Frenet/fixed-up have singularities | Bake history, correct random samples to tangent, explicit restarts and loop policy |
| Brownlow, patch normals | Interpolated control normals are an approximation | Separate shading normal from analytic geometric normal |
| Schneider, nearest point/root finder | Cubic distance stationarity is generally degree five; endpoints matter | Global closest search with achieved distance gap; Newton only improves candidates |
| Bunnell, adaptive subdivision tessellation | Shared edge data avoids rounding seams and topology cracks | Canonical edge/corner positions and synchronized parameter splits |
| Khoury/Dupuy/Riccio, compute tessellation | Compact keys and GPU pipelines have capacity/precision/seam conditions | GPU as gated backend; bounded buffers and explicit adjacency invariants |
| Hemingway, GPU NURBS | Rational geometry and trimming introduce distinct data/operations | Preserve rational fidelity; treat trim loops as a separate extension |

The closed-loop frame correction, specific transactional owners, numerical
certificate requirements, bicubic-to-triangle bound, publication protocol and
module boundaries are Ludus decisions. None of these articles specifies the
complete modern architecture.

## Length of Bezier curves

**Source:** Jens Gravesen, IV.7, “The Length of Bezier Curves,”
[Graphics Gems 5](../../references/Graphics%20Gems%205.pdf#page=209).
Reviewed PDF pp. 209-215, printed pp. 199-205. Chapter/author and the bound
were checked on rendered PDF p. 209.

The chapter bounds length between the endpoint chord and control-polygon length.
Subdivision tightens the bounds; a weighted combination converges faster as an
estimate. It distinguishes the polygon-minus-chord bound from heuristic
estimates and gives cusp/special-curve examples where those estimates
underestimate error.

**Adopt:** Keep lower/upper enclosures and allocate a whole-path absolute error
budget across leaves. Store uncertainty in prefixes as well as local lengths.
Distance inversion must account for both. For cubic spans the weighted estimate
reduces to the midpoint of chord and polygon lengths.

**Adapt:** Float64 and reviewed conservative arithmetic, explicit precision
floors, total leaf/work/depth limits and deterministic work order. These are
necessary implementation contracts, not a claim that the chapter proves the
behavior of Ludus floating-point code.

**Do not adopt:** A heuristic convergence test as a guaranteed bound, historical
subdivision counts as production budgets, or unconstrained recursion. The
counterexamples justify strict versus estimated result profiles.

## Adaptive sampling of parametric curves

**Source:** Luiz Henrique de Figueiredo, IV.4, “Adaptive Sampling of Parametric
Curves,”
[Graphics Gems 5](../../references/Graphics%20Gems%205.pdf#page=183).
Reviewed PDF pp. 183-188, printed pp. 173-178. Author/title were confirmed in
the rendered contents on PDF p. 3.

The chapter adaptively samples a general curve using local flatness probes and
reuses samples. It explicitly describes aliasing when a deterministic interior
probe misses undulations, and proposes randomized probes near the midpoint as
a practical mitigation. Its applications include rendering and approximate
arc-length parameterization.

**Adopt:** Spend refinement work where geometry requires it; reuse samples and
preserve ordered source parameters.

**Adapt:** For the known polynomial core, use control-hull and complete
subpatch bounds, deterministic subdivision and explicit budgets. A center-point
or edge-only surface test cannot see every interior deviation.

**Do not adopt:** Random sampling as a certificate, wall-clock-seeded cooking,
unbounded recursion or treating a flatness probe as a guaranteed length error.
A future black-box evaluator can offer estimated sampling; it cannot inherit
the polynomial core's strict guarantees without additional derivative/bound
information.

## Nonuniform splines

**Source:** Thomas Lowe, §2.4, “Nonuniform Splines,”
[Game Programming Gems 4](../../references/Game%20Programming%20Gems%204.pdf#page=180).
Reviewed PDF pp. 180-190, printed pp. 171-181. Rendered pp. 180 and 189 checked
identity, derivative scaling and endpoint/lookup discussion.

The chapter separates rounded, smooth and timed nonuniform paths. Its normalized
Hermite spans scale endpoint velocities by segment width; world acceleration
needs a second scaling. Smooth acceleration constraints form a tridiagonal
system, with an iterative smoothing alternative. It also discusses cached
segment lookup, precomputed coefficients and fixed-step forward differences.

**Adopt:** Differentiate geometry, timing and continuity; rescale derivatives
into their declared domain. Prepare domain ends and use a caller-owned cursor
for coherent traversal. Add an optional timed C2 solve with explicit endpoint/
periodic conditions, checked residuals and failure.

**Adapt:** Chord spacing is approximate constant-speed traversal. Use the bounded
arc map when true distance control matters. Prefer a bounded direct solve for
the stated continuity conditions over assuming a few smoothing passes suffice.

**Do not adopt:** A timing-driven tangent damping heuristic while still promising
exact C2 continuity, or forward differences for random/variable-step sampling.
Fixed-grid forward differences remain a benchmark candidate with drift controls.

## Parallel transport frame

**Source:** Carl Dougan, §2.5, “The Parallel Transport Frame,”
[Game Programming Gems 2](../../references/Game%20Programming%20Gems%202.pdf#page=208).
Reviewed PDF pp. 208-212, printed pp. 215-219.

The chapter incrementally rotates a frame with tangent changes, describes
camera and lofting uses, and emphasizes dependence on the previous frame.
Its comparison shows Frenet failures on straight spans/inflections and fixed-up
failures near alignment with the tangent. It suggests coarse frame samples
with quaternion interpolation.

**Adopt:** Prepare frame history once, retain the seed/policy, and give straight
and inflected paths a transported frame.

**Adapt:** Use a modern RMF implementation after numerical tests. Random samples
correct interpolated orientation to the analytic tangent. Frame angular error
is distinct from distance error. Closed-loop holonomy requires an explicit
preserve/compensate/authored-roll policy; that addition is our reasoning beyond
the chapter.

**Do not adopt:** Treating all zero cross-products as a harmless skip, which
confuses parallel and opposite tangents, or inferring bank/physics from transport.
Cusps, zero tangents and reversals need visible singularity handling.

## Fast patch normals

**Source:** Martin Brownlow, §4.3, “Fast Patch Normals,”
[Game Programming Gems 3](../../references/Game%20Programming%20Gems%203.pdf#page=338).
Reviewed PDF pp. 338-341, printed pp. 349-352. Rendered pp. 338 and 340 checked
chapter identity and the accuracy discussion.

This article's proposed optimization interpolates normals attached to the
control mesh using the patch's basis. It explicitly acknowledges that those
normals are approximate and can conceal shading discontinuities after skinning.
It is not an exact analytic-derivative normal algorithm.

**Adopt:** Keep an optional normal field as an independently interpolated
shading attribute, including its authoring/deformation policy.

**Adapt:** Normalize/check the shading result and keep it distinct from
normalized Cross(Su, Sv), which defines the actual geometric normal on a
regular patch. UV coordinates are also distinct from patch parameters.

**Do not adopt:** This approximate field for projection/contact geometry,
surface regularity or proof of continuity. Smoothed lighting cannot weld a
surface or repair a geometric seam. The chapter therefore improves the design
primarily by clarifying a boundary.

## Nearest point on a curve

**Source:** Philip J. Schneider, XI.7, “Solving the Nearest-Point-on-Curve
Problem,”
[Graphics Gems 1](../../references/Graphics%20Gems%201.pdf#page=617).
Reviewed PDF pp. 617-621, printed pp. 607-611.
Supporting article VIII.2, “A Bezier Curve-Based Root-Finder,” in the same PDF,
reviewed pp. 420-427, printed pp. 408-415.
The [publisher's contents](https://www.sciencedirect.com/book/9780080507538/graphics-gems)
also identify Schneider and the printed pp. 408-415 for the supporting article.

The nearest-point article derives the stationary dot-product equation, explains
that a cubic produces a generally fifth-degree polynomial, and evaluates
candidate roots plus endpoints. The root-finder article uses Bernstein form,
control-polygon signs and subdivision rather than depending on one initial
Newton guess, and notes accumulated floating-point error.

**Adopt:** Global candidate coverage, explicit endpoints and bounded search.
V1 uses control-hull distance branch-and-bound with a global achieved gap;
safeguarded Newton only improves the candidate upper bound.

**Adapt:** The stationary equation extends to 3D, but clipping, repeated/endpoint
roots, coincident stationary functions and numerical certification need new
tests. A nonzero degree-five polynomial has at most five distinct real roots;
do not size a general isolator around the nearest-point text's narrower sample
root-count wording.

**Defer:** A shared Bernstein root-isolation subsystem until another query needs
it. Recursion depth or a small parameter step is not by itself a strict
world-distance guarantee.

## Adaptive subdivision tessellation and displacement

**Source:** Michael Bunnell, Chapter 7, “Adaptive Tessellation of Subdivision
Surfaces with Displacement Mapping,” GPU Gems 2.
The index locates the chapter; the
[publisher-hosted author chapter](https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-7-adaptive-tessellation-subdivision-surfaces)
was read, particularly §§7.1.4-7.1.6 and §7.2.1.

The chapter preprocesses patch organization, evaluates limit geometry, chooses
one patch's data for shared edges/corners to avoid floating-point disagreement,
and handles T-junctions. Displacement changes its refinement test.

**Adopt:** Canonical shared position samples, matching boundary subdivision,
topology-aware tessellation and displacement-aware error bounds.

**Adapt:** V1 uses synchronized dyadic rectangular patch grids; a later local
adaptive backend must preserve the same seam invariants. Amplitude and derivative
variation affect different error requirements.

**Do not adopt:** Its pixel-shader/render-to-texture architecture, CPU readback
loop, dated performance comparison or an edge-only test as a general bicubic
interior guarantee. GPU/CPU choice is a Ludus measurement decision.

## Compute tessellation

**Source:** Jad Khoury, Jonathan Dupuy and Christophe Riccio, Chapter 1,
“Adaptive GPU Tessellation with Compute Shaders,”
[GPU Zen 2](../../references/GPU%20Zen%202%20Advanced%20Rendering%20Techniques.pdf#page=15).
Reviewed relevant text in PDF pp. 15-28, printed pp. 3-16: implicit key
representation, pipeline overview, T-junction discussion, results and limitations.

The chapter encodes binary triangle subdivision paths in compact keys, updates
double-buffered GPU arrays, and uses compute/indirect commands for rendering.
It discusses key-width precision, buffer capacities and a T-junction avoidance
rule observed under a stated screen-density configuration.

**Adopt conditionally:** Compact private refinement descriptors and reuse of
static topology are candidates for a later GPU backend.

**Require before integration:** Explicit output/append capacity, overflow
behavior, synchronization and capability tests. A constant-sized key per node
does not make the total number of nodes or output triangles constant.

**Do not adopt:** The empirical distance/edge-size seam condition as proof for
arbitrary views, deformations or displacements. Shared boundaries remain explicit.
The chapter's results are for its hardware, mesh and shader workload, not Ludus.

## GPU NURBS and trimming

**Source:** Graham Hemingway, geometry manipulation Chapter 3,
“GPU-Based NURBS Geometry Evaluation and Rendering,”
[GPU Pro 1](../../references/GPU%20Pro%201.pdf#page=82).
Reviewed background and implementation/trim excerpts from PDF pp. 82-100,
printed pp. 67-85, especially §3.1, §3.3's storage/evaluation description and
§3.4.1-3.4.2. This is a selective reading, not a validation of its full code.

The chapter defines weighted rational curve/surface evaluation and separates
the underlying surface from a trimming profile. Trim preparation includes
inverse projection into surface parameters and a rendering mask.

**Adopt:** Rational source metadata cannot disappear during an allegedly exact
conversion. Trimming is an independent domain/topology problem with boundary/
hole semantics. Position evaluation alone does not complete trimmed geometry.

**Adapt:** V1 can bake explicit approximations; exact retained NURBS needs a
separate homogeneous representation, knot/weight/denominator validation and
fidelity tests.

**Do not adopt:** Texture masking as robust collision/trim topology, unrestricted
Newton inverse projection, old GPU storage limits, or an unqualified precision/
speed claim. Define knot counts and endpoint multiplicities under one tested
index convention rather than translating notation without checking it.

## Current primary sources and SOTA assessment

The primary-source checks below establish the availability/properties of
individual techniques. Their selection and integration are our judgment.
No source makes one architecture universally optimal.

| Source checked on 2026-10-04 | Design implication and limit |
| --- | --- |
| [Yuksel, Schaefer and Keyser: Catmull-Rom parameterization](https://www.cemyuksel.com/research/catmullrom_param/) | Centripetal regular segments avoid cusps/self-intersections within the analyzed family; not a global nonintersection guarantee |
| [Wang, Jüttler, Zheng and Liu: RMF paper record/abstract](https://hub.hku.hk/handle/10722/152386) | Double reflection is a strong prepared-frame candidate; the published convergence statement does not validate a singular Ludus path |
| [Yuksel: A Class of C2 Interpolating Curves, 2020](https://www.cemyuksel.com/research/interpolating_curves/) | Local C2 non-polynomial curves are a meaningful extension; exact cubic conversion is unavailable in general |
| [OpenSubdiv Far overview](https://opensubdiv.org/docs/far_overview.html) | Separate topology refinement, value refinement and stencil/patch evaluation; reuse topology without assuming numerical error stays valid |
| [OpenSubdiv surface/data semantics](https://opensubdiv.org/docs/subdivision_surfaces.html) | Scheme, boundary, crease and face-varying data require dedicated metadata/contracts |
| [SciPy PCHIP documentation](https://docs.scipy.org/doc/scipy/reference/generated/scipy.interpolate.PchipInterpolator.html) | Shape-preserving scalar interpolation is C1 with possible second-derivative jumps; optional cooker technique without a runtime Python dependency |

The RMF paper's full text was not available from the institutional record.
That source supports selection for investigation, not implementation equations.
The Yuksel sources were reviewed through author summaries/formulations; this
task does not claim a new proof or exhaustive comparison of every curve family.

“SOTA” here means preserving modern extension paths and rigorous ownership/
numerical contracts while choosing a small kernel that can be tested and
optimized. A newer formulation with desirable authoring properties is not
automatically the lowest-cost runtime format. The C2 family and established
OpenSubdiv integrations show why extensions deserve explicit fidelity and
dependency gates rather than forced conversion.

## Index-only leads and rejected scope expansion

The index also lists “Subdivision Surfaces for Character Animation” in Game
Programming Gems 3, “High-Performance Subdivision Surfaces” in Game Programming
Gems 7, “Trigonometric Splines” in the same volume, GPU Pro Phong tessellation,
and Eberly's curve/surface chapters. These remain reading leads; this review
does not attribute implementation recommendations to their unread contents.

The scanned “Quick and Simple Bezier Curve Drawing” title/introduction was
encountered while locating the length chapter. Its general-degree
factorial/power approach was not reviewed as an implementation candidate.
The polynomial kernel already has a fixed degree and a bounded reference path.

Water and terrain sources do not warrant adding simulation, arbitrary field
meshing, trimming, robust boolean geometry and subdivision to v1. They are
different product requirements. No third-party implementation or copyrighted
code is added by this design task.

## Evidence still needed

Implement and independently test conservative arithmetic before labeling results
certified. Compare Horner/de Casteljau, cache footprints, direct/BVH projection,
whole-candidate/incremental edits and CPU/GPU preparation on actual native/
browser fixtures. Record source/derived memory and active-plus-candidate peaks.
Historical chapter timings and subdivision counts are not acceptance numbers.

The architecture's implementation phases require warning-clean builds, unit/
sanitizer tests, pinned format/tidy, header/build budgets, SDK and Wasm validation.
This review supplies an implementation handoff and evidence trail, not proof
that those future implementations already pass.
