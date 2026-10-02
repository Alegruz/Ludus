# Ludus math implementation tasks

Status: all stages pending. This document plans implementation; no checkbox
certifies work performed by the design handoff. Read [requirements.md](requirements.md),
[design.md](design.md) and the embedded
[Gems research](../../../docs/architecture/math-research.md) first.

Complete each stage's code and relevant tests before advancing. Preserve earlier
gates after subsequent changes. Missing hardware/tool access blocks only its
dependent acceptance gate; finish independent work and record the gap. Ordinary
implementation choices do not require another user approval. Scope changes need
technical evidence and a written decision, not silent contract weakening.

## M0 Freeze conventions and numerical evidence

Requirements: MA01–MA05, MA09, MA11, MA14.

- [ ] Re-read AGENTS.md, steering and actual build/export policy; inventory existing
  edits and consumers. Confirm there is no conflicting math implementation since
  this design was written. Preserve audio/shader/text/input/browser work.
- [ ] Add a proposed Math decision record using the next available ADR number;
  freeze the design conventions, status/output contracts and v1 API inventory.
  Do not reuse ADR 0010 or assume another number is still free.
- [ ] Record narrow ADR 0003 `<cmath>`/`<limits>` implementation allowances and
  explain why they introduce no exceptions, allocation or heavy public header.
- [ ] Define deterministic analytic/adversarial fixtures and independent numerical
  oracles. Fix supported ranges, units, error metrics and initial tolerance policy.
  Reference float32/float64 IEEE assumptions; document unsupported FP environments.
- [ ] Prototype and check the supplied perspective/orthographic matrices, frustum
  row extraction and quaternion action conventions through independent equations.
  Include noncommuting composition, 180-degree and near-opposite cases.
- [ ] Plan the small isolated Vulkan/WebGPU matrix-payload fixture using the actual
  available shader/build path. Existing smoke clear/triangle paths are not camera
  support. Record pending GPU access without expanding RHI or borrowing private
  probe handles into public APIs.
- [ ] Define effective precise compiler options and consumer checks. Public inline
  arithmetic must not be advertised as independent of the caller's flags.

Exit: one conventions/API/accuracy record and reproducible fixtures, with no
unresolved sign/storage/domain ambiguity. Source research is already complete;
do not ask the user for the private PDFs or repeat an open-ended literature survey.

## M1 Module scalar and vector foundation

Requirements: MA01–MA06, MA14.

- [ ] Add the static module, installed target/header file sets, native test target,
  root native/web build inclusion and separate browser numeric fixture.
- [ ] Implement concrete vector values, layout tests, constants, scalar helpers,
  exact equality, named Hadamard, dot/cross, length/distance and checked normalization.
- [ ] Implement scalar/vector lerp, smoothstep and checked exponential approach;
  document endpoints/extrapolation/domains and libm behavior.
- [ ] Apply precise `.cpp` options, supported fast-math header rejection and
  engine no-exception policy. Verify actual compilation flags.
- [ ] Validate huge/tiny/signed-zero/nonfinite vectors, aliasing, unchanged failure
  outputs and independent expected values. Probe actual calls for allocation.
- [ ] Build native and pinned Wasm; native unit/sanitizer checks, format/tidy and
  standalone/include gates pass for this stage. Add a native installed consumer.

Exit: usable scalar/vector SDK on both targets, with no heavyweight include,
global runtime state or normalization division-by-zero path.

## M2 Rotations matrices and transforms

Requirements: MA02–MA08, MA14.

- [ ] Implement Quaternion identity, axis-angle, composition, conjugate, unit-vector
  rotation, robust TryRotationBetween, and shortest-path nlerp/slerp.
- [ ] Implement Matrix3/4 layout/action/multiplication/transpose and largest-component
  checked rotation conversion; reject nonrotation bases explicitly.
- [ ] Implement fixed-stack scaled-pivot inverses with rcond/residual policy,
  two-sided checks after narrowing, representability and transactional outputs.
- [ ] Implement Affine2/3 action/composition/inverse, checked normal transforms,
  TransformTRS conversion, homogeneous Matrix4 export and perspective divide.
- [ ] Test quaternion/matrix action agreement, opposite signs, equal/opposite/near-
  opposite directions, 180-degree nonprincipal axes, interpolation endpoints and
  branch transitions against independent oracles.
- [ ] Test singular/nearly singular matrices, rescaled matrices, inverse aliasing,
  reflection/negative/zero scales and orthogonal normal action.
- [ ] Show that nonuniform parent scale plus rotated child retains shear in Affine3.
  Do not introduce TRS multiplication/decomposition to make tests pass.
- [ ] Validate native/Wasm numerical corpus and stage regression gates.

Exit: complete small linear-algebra/affine API with documented numerical failures;
no general-purpose matrix templates, forced public SIMD layout or silent repair.

## M3 Projections relative coordinates and random streams

Requirements: MA03–MA05, MA09, MA11–MA12, MA14.

- [ ] Implement checked LookAt, finite/infinite reverse-Z perspective, finite
  reverse-Z orthographic and NDC-to-framebuffer conversion.
- [ ] Cover finite input/domain validation, near/far endpoints, tiny extents,
  extreme coefficients, degenerate up/target and infinite-far w=0 behavior.
- [ ] Implement double relative subtraction before narrowing, explicit component
  range and unchanged outputs on failure. Test 1e9 origins and lost-input limits.
- [ ] Implement PCG32 XSH-RR, explicit seed/selector, next integer, unbiased bounded
  mapping and exactly specified float mapping. Preserve adapted code licensing
  and record exact source provenance/commit if code is copied.
- [ ] Implement versioned state get/restore and invalid-bound/state nonconsumption.
  Verify reference known answers, rejection cases and replay in native/browser.
- [ ] Document that PCG bounded rejection has no hard iteration bound and the math
  library does not offer bitwise deterministic floating simulation.
- [ ] Add README examples for 2D, parent/local composition, camera, relative position
  and logical random stream ownership; validate example snippets through a consumer.

Exit: conventions implemented coherently, random replay frozen, and world/camera
services remain outside Math.

## M4 Primitive geometry and scalar batches

Requirements: MA05, MA10, MA13–MA14.

- [ ] Add normalized Plane construction, signed distance/reflection, finite empty
  Aabb2/3 factories, merge/containment and conservative affine bound transformation.
- [ ] Implement Ray3/float64 RayInterval and structured status-plus-hit queries.
  Cover slab zero/signed-zero components, inclusive intervals, invalid inputs,
  cancellation-resistant sphere roots, tangent and inside-start behavior.
- [ ] Implement constrained segment closest points, all boundary candidates,
  degenerate/parallel cases, double intermediates, deterministic ties and honest
  near-parallel/contact stability limits.
- [ ] Implement canonical frustum extraction, explicit finite/infinite mode,
  active-plane mask and conservative sphere/AABB classification with caller margin.
  Derive and record roundoff allowance including normalized coefficient narrowing.
- [ ] Test frustum classification against direct clip inequalities and sampled
  primitive points/corners. Label conservative false positives; reject false
  negative culling within the agreed supported range.
- [ ] Implement scalar point-transform and sphere-classification batch APIs with
  span sizes, prevalidation, full output preflight, transaction, overlap/alias rules
  and stable output order. No hidden scratch allocation.
- [ ] Test odd counts, type-valid unaligned buffers, failure after an earlier valid
  element, exact in-place transforms, partial overlap, empty inputs and all tails.
- [ ] Run production-path allocation, numerical, native/Wasm and regression checks.

Exit: usable primitive queries and batch seams, without traversal/physics/services
or claims of exact predicates.

## M5 Shader interoperability and performance evidence

Requirements: MA09, MA11, MA13–MA14.

- [ ] Add explicit flat-column export helpers and graphics-owned test payloads,
  with initialized padding, layout/offset/stride tests and source declarations.
  Include asymmetric Matrix4 plus padded Matrix3 and Affine3 transfer cases.
- [ ] Execute an isolated real Vulkan fixture and real browser WebGPU fixture.
  Read back transform results and verify action/order, canonical depth, screen
  Y and winding. Record actual shaders, payloads, commands and output artifacts.
  This may use test-only private pipelines; do not require a new general renderer.
- [ ] Add reproducible native and browser benchmarks: matrix/affine action,
  normalization, quaternion operations, inverse and representative batch sizes.
  Include small batches, steady-state and large memory-bound data.
- [ ] Measure validation/preflight, packing, dispatch and memory traffic in the
  end-to-end scope. Use checksums/errors and repeated results; no sanitized timing.
- [ ] Inspect vectorization reports/generated code and production profiling. Record
  hotspots or the evidence that scalar/compiler-generated code is sufficient.
- [ ] If justified, prototype only the specific kernel, with private implementation,
  scalar parity, CPU feature safety, Wasm scalar/SIMD artifact separation and no
  relaxed SIMD. Adopt it only after the design's benefit/accuracy/maintenance gate.
- [ ] If no optimized kernel qualifies, record that decision and keep scalar.
  This is a valid completed outcome, not an unfinished SIMD task.
- [ ] Pin any external benchmark comparator's version/license and conventions;
  otherwise report internal before/after only. Never claim universal superiority.

Exit: actual shader/CPU agreement and measured performance record. CPU correctness
may be complete while GPU access is pending, but the GPU acceptance boxes remain
unchecked. Optional SIMD outcome is a documented adopt/defer decision.

## M6 Final review and installed SDK acceptance

Requirements: all MA01–MA14.

- [ ] Re-check production diff against AGENTS.md/steering, include boundaries,
  no engine exceptions, fixed-width aliases, noexcept/status and public API scope.
- [ ] Run the full pinned validation matrix below after the final implementation.
- [ ] Run actual native and browser installed/export consumer fixtures and verify
  no private headers, intrinsics, probe types or new dependency leaks into the SDK.
- [ ] Measure public headers/build budgets. Do not raise a budget without the
  required profile evidence and review note; keep Math out of foundational PCH.
- [ ] Audit failure transactions, resumable assertion paths, invalid casts,
  FP policy, RNG state compatibility and batch alias/tail handling.
- [ ] Update README/API comments and evidence ledger with supported platforms,
  accuracy ranges, benchmark results and precise remaining limitations.

## Pinned validation matrix

Use the existing scripts rather than installing a different toolchain. If the
checkout is already initialized, inspect tool availability before rerunning init.

```bash
./scripts/build linux-clang-debug
./scripts/test linux-clang-debug
./scripts/build linux-clang-development
./scripts/check linux-clang-development --all
./scripts/build linux-clang-asan-ubsan
./scripts/test linux-clang-asan-ubsan
./scripts/install-sdk linux-clang-development
./scripts/profile-build linux-clang-development
./scripts/check-build-budget linux-clang-development
./scripts/build web-emscripten-development
./scripts/build web-emscripten-release
```

Verify current script syntax in the checkout before running; record exact commands
and any script-supported partial-SDK/browser packaging steps used. Format must
use project `scripts/check`, which also enforces multiline initializer braces.
Existing CTest self-sufficiency/foundational include and compile-policy gates must
remain green. Header budgets are global and per-header; existing work may affect
aggregate timings, so compare an unchanged baseline rather than blame Math by
assumption. Validate PCH-on only through the project's supported option/preset.
Native Catch2 re-enables test exceptions; Math engine code remains exception-free.

Builds alone are insufficient for browser arithmetic or GPU interoperability:
also run the browser corpus, Vulkan fixture and browser WebGPU fixture. Record
the actual runner invocation established in M1/M5. Browser release corpus requires
an enabled test fixture target even though native tests are off. If a machine
lacks a compositor/GPU/browser, retain pending gate names and finish other checks.

## Evidence ledger

Add rows as stages are completed; link stable evidence under a new
`docs/architecture/math-evidence/` directory if useful. Do not commit scratch
book extractions, PDFs, generated build output or machine paths.

| Stage or gate | Status | Revision and changed paths | Exact command and environment | Result and artifact | Pending limitation |
| --- | --- | --- | --- | --- | --- |
| M0 conventions and corpus | Pending | — | — | — | No implementation |
| M1 scalar/vector native and Wasm | Pending | — | — | — | No implementation |
| M2 rotation/algebra/affine | Pending | — | — | — | No implementation |
| M3 projections/precision/PCG | Pending | — | — | — | No implementation |
| M4 geometry/batches | Pending | — | — | — | No implementation |
| M5 Vulkan shader fixture | Pending | — | — | — | No GPU verification |
| M5 browser WebGPU fixture | Pending | — | — | — | No GPU verification |
| M5 benchmarks and optimization decision | Pending | — | — | — | No performance measurements |
| M6 complete regression and installed SDK | Pending | — | — | — | No implementation |

Pass, fail, pending and deliberately deferred optional optimization are different
states. Do not check a stage solely because its code exists or another test mocked
its result. Re-run a passed gate after a material change to the behavior it verifies.
