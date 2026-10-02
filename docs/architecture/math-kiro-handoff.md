# Kiro handoff for Ludus math

Start with **Prompt 1** after these five files are in Kiro's checkout:

- `.kiro/specs/math/requirements.md`
- `.kiro/specs/math/design.md`
- `.kiro/specs/math/tasks.md`
- `docs/architecture/math-research.md`
- `docs/architecture/math-kiro-handoff.md`

The package chooses an allocation-free `Ludus::FoundationMath` static module,
concrete readable values, precise scalar algorithms, explicit numerical failures,
affine composition, reverse-Z projection helpers, primitive queries, PCG32
streams and measured batch optimization. The research embeds eleven private
Gems chapter findings plus modern corrections and primary references. Kiro needs
no PDFs, book figures, historical CD code or online access for the design.

This handoff contains no engine implementation or measured speed claim. All
implementation, sanitizer/build/SDK, accuracy, browser and GPU gates are pending.
The default completion includes a documented choice to retain scalar code if
no SIMD kernel earns its complexity through real measurements.

## Prompt 1 Implement the complete sequence

```text
Implement Ludus's math system from the repository specification. Read AGENTS.md
and applicable .kiro/steering/ first, then:
- .kiro/specs/math/requirements.md
- .kiro/specs/math/design.md
- .kiro/specs/math/tasks.md
- docs/architecture/math-research.md

Execute M0 through M6 in order, with small reviewable changes and production-path
tests. This authorizes implementation, not another architecture survey. Revalidate
the current checkout and preserve unrelated edits, audio/shader/text/input specs,
Platform/RHI and existing native/browser smoke/probes. Do not stop after making
a plan or ask for approval between ordinary stages. Record technically justified
corrections without silently weakening the requirements.

Create static Ludus::FoundationMath under modules/foundation/math/, depending
only on FoundationBase. Export lightweight self-sufficient headers and retain
the installed SDK boundary. Use C++23, Ludus fixed-width aliases, noexcept and
nodiscard status/out-parameter failures, no engine exceptions, no allocations,
no logging/profiling dependency, no initialization or mutable global service.
Keep Math out of Base/core.h and the foundational PCH. Document the narrow
standard-math implementation allowance in ADR 0003 before using it.

Implement the concrete vector/quaternion/matrix/affine/TRS vocabulary. Freeze
right-handed +Y-up/-Z-forward, column-vector action and column storage, Hamilton
xyzw quaternions and A*B applying B first. Use readable scalar code and private
cpp numerical algorithms. Never expose SIMD registers, generic dimension/scalar
templates, expression templates, swizzle proxies or backend macros in the API.

Enforce checked finite/domain/representability validation and unchanged outputs
on failure, including in-place aliases. Distinguish geometric miss from invalid
input. Assertions are not substitutes for runtime checks, particularly with
resumable assertion behavior. Document per-operation tolerance units; no universal
epsilon, fuzzy operator equality or blanket float determinism claim. Check actual
precise compiler flags and consumer settings; never enable global fast math.

Implement scaled robust normalization, stable shortest-arc rotations including
opposite/near-opposite directions, largest-component matrix conversion, shortest-
path nlerp/slerp, fixed-stack scaled-pivot checked inverse with condition and
two-sided narrowed-result residuals, and inverse-transpose normal transforms.
TRS is authoring data; compose Affine3 so nonuniform-scale hierarchy shear is
retained. Do not silently normalize bad matrices or return identity inverses.

Implement checked view/reverse-Z projection/framebuffer helpers; canonical clip
depth is [0,1]. Include finite/infinite perspective and finite orthographic.
Treat infinite-far homogeneous w=0 as a direction, not a point to divide.
Document depth clear/comparison and one graphics-owned Vulkan Y adaptation.
Math must not add renderer/depth features to RHI or alter existing smoke behavior.

Implement normalized planes, explicit empty/degenerate AABBs, conservative affine
bounds, bounded parametric rays, stable sphere roots, constrained closest segment
points, and canonical frustum extraction/classification. Handle zero/signed-zero
ray components, tangency, inside starts, endpoint minima and inactive infinite-far
planes. Use double intermediates where specified, conservative culling allowances,
and independently validated domains; do not claim exact topological predicates.

Implement double world-minus-origin subtraction before checked float narrowing;
no origin manager. Keep CPU compact layout separate from explicit shader payloads
and padding. Implement caller-owned PCG32 XSH-RR with specified seeding, reference
known answers, unbiased bounds, deterministic float mapping and versioned state.
Retain adapted source licensing/provenance. Invalid calls do not consume RNG state;
bounded rejection is not a hard deadline guarantee or security randomness.

Implement scalar span batches with complete prevalidation/output preflight,
transactional outputs, stable order and explicit alias/overlap contracts. Optimize
only a demonstrated hotspot through a private kernel. Measure end-to-end validation,
packing and dispatch; keep scalar if benefit is insufficient. No mandatory ISPC,
AVX baseline, -march=native, relaxed Wasm SIMD or runtime backend framework.

The PDFs are intentionally unavailable. Use the committed chapter findings and
algorithm contracts; do not request PDFs, historical CD files or invent findings.
Defer physics/IK/curves/compression/noise/exact predicates/CSG/BVH/lockstep and
general scene/editor systems. They are not prerequisites for the specified module.

Complete independent analytic/adversarial/metamorphic tests, failure/alias/allocation
checks, pinned warning/format/tidy and ASan/UBSan, header budgets, installed native
and Wasm consumers, actual browser numerical execution, real isolated Vulkan and
WebGPU shader-payload fixtures, and benchmark evidence. Mocks/Node/shader compilation
cannot certify GPU execution. If hardware is unavailable, finish independent work,
record the pending named gate and keep affected checkboxes unchecked.

Update tasks/evidence honestly. Report changed paths, API/build/example usage,
actual accuracy/performance results, adopted/deferred optimizations and precise
remaining limitations. Do not push, merge or deploy unless separately instructed.
```

## Prompt 2 Implement one milestone

Use this instead of Prompt 1 to control scope. Begin with M0, then substitute the
next stage after reviewing its evidence.

```text
Implement milestone M0 of Ludus's math specification. Read AGENTS.md, applicable
.kiro/steering/, all three .kiro/specs/math/ files and
docs/architecture/math-research.md. The research includes the private PDF findings;
do not ask for the books or historical CD code.

Revalidate the actual checkout, preserve unrelated edits and existing systems,
implement this milestone's concrete work and meaningful verification, and update
its evidence ledger with commands/results, artifacts and pending gates. Respect
the selected conventions, status/output transactions, numeric policy and SDK
boundary. Do not stop at a plan, invent measurements or silently replace failed
contracts with weaker ones. Do not advance beyond this stage, push, merge or
deploy in this turn. Return changed paths, results and readiness for the next stage.
```

## Prompt 3 Audit and repair

```text
Audit Ludus's implemented math system against MA01-MA14 and M0-M6 under
.kiro/specs/math/. Read AGENTS.md, steering, design/research/decision record and
actual production code. Checkboxes are not evidence. Fix concrete scope-relevant
violations and run the checks affected by each repair.

Verify native/Wasm compiler and consumer FP flags; no exceptions/allocations;
header self-sufficiency/budgets and SDK closure; readable fixed layouts; matrix/
quaternion action and noncommuting order; numerical domains/tolerance units;
finite/signed-zero/extreme handling; status versus miss; unchanged outputs and
alias/overlap transactions; resumable assertion safety; and no float-to-int UB.

Verify opposite/near-opposite rotations, interpolation endpoints/hemisphere/fallback,
matrix conversion at 180 degrees, condition/residual inverse checks after narrowing,
shear-preserving affine hierarchy, inverse-transpose normals, near/far projection
and infinite directions, framebuffer/Y/winding, culling conservatism, ray zeros/
tangents/inside intervals, constrained segment minima, empty bounds and outward
rounding, double-origin subtraction order, GPU layout/strides, PCG known answers/
state/bounded mapping and failed-call nonconsumption, batch preflight and tails.

Demand independent oracles and actual native/browser production execution.
Verify real Vulkan/WebGPU packed-matrix fixtures separately from CPU tests, and
check benchmark methodology including packing/preflight and scalar parity. Keep
missing hardware gates pending and optional SIMD deferred when no justified gain
exists. Report actionable findings, repairs, actual evidence and remaining limits.
Do not push, merge or deploy unless separately instructed.
```
