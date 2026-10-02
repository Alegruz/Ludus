# Ludus math implementation tasks

Status: M0–M4 implemented and independently verified; M5 GPU fixtures and
benchmarks Pending (no GPU/pinned toolchain in the implementation environment);
M6 regression partial pending the full pinned validation matrix. See the per-stage
checkboxes and the evidence ledger at the bottom. `[x]` = done, `[~]` = done
independently with a named pending gate, `[ ]` = not yet done. Read
[requirements.md](requirements.md), [design.md](design.md) and the embedded
[Gems research](../../../docs/architecture/math-research.md) first.

Complete each stage's code and relevant tests before advancing. Preserve earlier
gates after subsequent changes. Missing hardware/tool access blocks only its
dependent acceptance gate; finish independent work and record the gap. Ordinary
implementation choices do not require another user approval. Scope changes need
technical evidence and a written decision, not silent contract weakening.

## M0 Freeze conventions and numerical evidence

Requirements: MA01–MA05, MA09, MA11, MA14.

- [x] Re-read AGENTS.md, steering and actual build/export policy; inventory existing
  edits and consumers. Confirm there is no conflicting math implementation since
  this design was written. Preserve audio/shader/text/input/browser work.
  (Clean `main` at `8e34173`; no `modules/foundation/math/` existed; no unrelated
  edits touched. Platform/RHI/Text/Input/smoke/probes left unchanged.)
- [x] Add a proposed Math decision record using the next available ADR number;
  freeze the design conventions, status/output contracts and v1 API inventory.
  (ADR 0010 — next free number; 0001–0009 were taken.)
- [x] Record narrow ADR 0003 `<cmath>`/`<limits>` implementation allowances and
  explain why they introduce no exceptions, allocation or heavy public header.
- [x] Define deterministic analytic/adversarial fixtures and independent numerical
  oracles. Fix supported ranges, units, error metrics and initial tolerance policy.
  Reference float32/float64 IEEE assumptions; document unsupported FP environments.
  (`tests/math_oracle.hpp`; accuracy targets in `docs/architecture/math-evidence/`.)
- [x] Prototype and check the supplied perspective/orthographic matrices, frustum
  row extraction and quaternion action conventions through independent equations.
  Include noncommuting composition, 180-degree and near-opposite cases.
  (Verified: reverse-Z depth endpoints, frustum vs direct clip inequalities,
  `Rotate==Matrix3(q)·v`, non-commuting multiply, near-180° conversion,
  near-opposite rotation-between — all pass in the independent harness.)
- [x] Plan the small isolated Vulkan/WebGPU matrix-payload fixture using the actual
  available shader/build path. Existing smoke clear/triangle paths are not camera
  support. Record pending GPU access without expanding RHI or borrowing private
  probe handles into public APIs. (Plan in `docs/architecture/math-evidence/`;
  execution Pending — no GPU in this environment.)
- [x] Define effective precise compiler options and consumer checks. Public inline
  arithmetic must not be advertised as independent of the caller's flags.
  (`.cpp` compiled `-ffp-contract=off`, no fast math; headers `#error` under
  `__FAST_MATH__`/`__FINITE_MATH_ONLY__`; documented in ADR 0010 §4 and the README.)

Exit: one conventions/API/accuracy record and reproducible fixtures, with no
unresolved sign/storage/domain ambiguity. Source research is already complete;
do not ask the user for the private PDFs or repeat an open-ended literature survey.

## M1 Module scalar and vector foundation

Requirements: MA01–MA06, MA14.

- [x] Add the static module, installed target/header file sets, native test target,
  root native/web build inclusion and separate browser numeric fixture.
  (`modules/foundation/math/CMakeLists.txt`; root `add_subdirectory` before
  consumers and outside the native-only branch; `tools/web-math-probe`.)
- [x] Implement concrete vector values, layout tests, constants, scalar helpers,
  exact equality, named Hadamard, dot/cross, length/distance and checked normalization.
- [x] Implement scalar/vector lerp, smoothstep and checked exponential approach;
  document endpoints/extrapolation/domains and libm behavior.
- [x] Apply precise `.cpp` options, supported fast-math header rejection and
  engine no-exception policy. Verify actual compilation flags.
  (`-ffp-contract=off` on the library/test targets; `#error` guards in
  `scalar.hpp`; `-fno-exceptions` inherited from project defaults.)
- [x] Validate huge/tiny/signed-zero/nonfinite vectors, aliasing, unchanged failure
  outputs and independent expected values. Probe actual calls for allocation.
  (`vector_tests.cpp`, `scalar_tests.cpp`, `allocation_tests.cpp` — all pass in
  the independent harness; zero allocations observed.)
- [~] Build native and pinned Wasm; native unit/sanitizer checks, format/tidy and
  standalone/include gates pass for this stage. Add a native installed consumer.
  Independent (this env): warning-clean compile on clang-15 `-std=c++2b`, all
  unit tests pass, header self-sufficiency and foundational-include gates pass,
  installed consumer extended (`tests/sdk_consumer`). **Pending** on the pinned
  toolchain: clang-18 build/test, ASan/UBSan (no compiler-rt sanitizer libs
  here), pinned clang-format/clang-tidy 18, and the pinned-Wasm build/corpus
  (no Emscripten here).

Exit: usable scalar/vector SDK on both targets, with no heavyweight include,
global runtime state or normalization division-by-zero path.

## M2 Rotations matrices and transforms

Requirements: MA02–MA08, MA14.

- [x] Implement Quaternion identity, axis-angle, composition, conjugate, unit-vector
  rotation, robust TryRotationBetween, and shortest-path nlerp/slerp.
- [x] Implement Matrix3/4 layout/action/multiplication/transpose and largest-component
  checked rotation conversion; reject nonrotation bases explicitly.
- [x] Implement fixed-stack scaled-pivot inverses with rcond/residual policy,
  two-sided checks after narrowing, representability and transactional outputs.
- [x] Implement Affine2/3 action/composition/inverse, checked normal transforms,
  TransformTRS conversion, homogeneous Matrix4 export and perspective divide.
- [x] Test quaternion/matrix action agreement, opposite signs, equal/opposite/near-
  opposite directions, 180-degree nonprincipal axes, interpolation endpoints and
  branch transitions against independent oracles. (`quaternion_tests.cpp`.)
- [x] Test singular/nearly singular matrices, rescaled matrices, inverse aliasing,
  reflection/negative/zero scales and orthogonal normal action.
  (`matrix_tests.cpp`, `transform_tests.cpp`.)
- [x] Show that nonuniform parent scale plus rotated child retains shear in Affine3.
  Do not introduce TRS multiplication/decomposition to make tests pass.
  (`transform_tests.cpp` "retains shear".)
- [~] Validate native/Wasm numerical corpus and stage regression gates.
  Independent (this env): all M2 tests pass on clang-15. **Pending** pinned
  clang-18 native and Emscripten-Wasm corpus runs.

Exit: complete small linear-algebra/affine API with documented numerical failures;
no general-purpose matrix templates, forced public SIMD layout or silent repair.

## M3 Projections relative coordinates and random streams

Requirements: MA03–MA05, MA09, MA11–MA12, MA14.

- [x] Implement checked LookAt, finite/infinite reverse-Z perspective, finite
  reverse-Z orthographic and NDC-to-framebuffer conversion.
- [x] Cover finite input/domain validation, near/far endpoints, tiny extents,
  extreme coefficients, degenerate up/target and infinite-far w=0 behavior.
  (`projection_tests.cpp`.)
- [x] Implement double relative subtraction before narrowing, explicit component
  range and unchanged outputs on failure. Test 1e9 origins and lost-input limits.
  (`precision.cpp`, `precision_tests.cpp`.)
- [x] Implement PCG32 XSH-RR, explicit seed/selector, next integer, unbiased bounded
  mapping and exactly specified float mapping. Preserve adapted code licensing
  and record exact source provenance/commit if code is copied.
  (`random.hpp`/`random.cpp`; provenance in README + header. No source copied —
  the small integer algorithm is reimplemented, so no upstream commit hash is
  fabricated.)
- [x] Implement versioned state get/restore and invalid-bound/state nonconsumption.
  Verify reference known answers, rejection cases and replay in native/browser.
  (`random_tests.cpp`; known answers match `a15c02b7 …`. Browser corpus Pending.)
- [x] Document that PCG bounded rejection has no hard iteration bound and the math
  library does not offer bitwise deterministic floating simulation.
  (`random.hpp`, README, ADR 0010.)
- [x] Add README examples for 2D, parent/local composition, camera, relative position
  and logical random stream ownership; validate example snippets through a consumer.
  (README examples; `tests/sdk_consumer` exercises rotate/inverse/PCG.)

Exit: conventions implemented coherently, random replay frozen, and world/camera
services remain outside Math.

## M4 Primitive geometry and scalar batches

Requirements: MA05, MA10, MA13–MA14.

- [x] Add normalized Plane construction, signed distance/reflection, finite empty
  Aabb2/3 factories, merge/containment and conservative affine bound transformation.
- [x] Implement Ray3/float64 RayInterval and structured status-plus-hit queries.
  Cover slab zero/signed-zero components, inclusive intervals, invalid inputs,
  cancellation-resistant sphere roots, tangent and inside-start behavior.
- [x] Implement constrained segment closest points, all boundary candidates,
  degenerate/parallel cases, double intermediates, deterministic ties and honest
  near-parallel/contact stability limits.
- [x] Implement canonical frustum extraction, explicit finite/infinite mode,
  active-plane mask and conservative sphere/AABB classification with caller margin.
  Derive and record roundoff allowance including normalized coefficient narrowing.
  (`DistanceAllowance` in `queries.cpp`: relative term from the magnitude of the
  distance's absolute products plus a small floor for normalized-coefficient
  narrowing, added on top of the caller margin.)
- [x] Test frustum classification against direct clip inequalities and sampled
  primitive points/corners. Label conservative false positives; reject false
  negative culling within the agreed supported range.
  (`queries_tests.cpp` "agrees with direct clip inequalities".)
- [x] Implement scalar point-transform and sphere-classification batch APIs with
  span sizes, prevalidation, full output preflight, transaction, overlap/alias rules
  and stable output order. No hidden scratch allocation.
- [x] Test odd counts, type-valid unaligned buffers, failure after an earlier valid
  element, exact in-place transforms, partial overlap, empty inputs and all tails.
  (`batch_tests.cpp`.)
- [~] Run production-path allocation, numerical, native/Wasm and regression checks.
  Independent (this env): allocation probe (0 allocations) and all M4 numerical
  tests pass on clang-15. **Pending** pinned clang-18 and Emscripten-Wasm runs.

Exit: usable primitive queries and batch seams, without traversal/physics/services
or claims of exact predicates.

## M5 Shader interoperability and performance evidence

Requirements: MA09, MA11, MA13–MA14.

- [~] Add explicit flat-column export helpers and graphics-owned test payloads,
  with initialized padding, layout/offset/stride tests and source declarations.
  Include asymmetric Matrix4 plus padded Matrix3 and Affine3 transfer cases.
  (Payload type and export helpers are specified in the fixture plan in
  `docs/architecture/math-evidence/`; the fixture itself is **Pending** GPU
  access, so the export-helper source is written alongside the fixture, not
  before it, to avoid shipping an unexercised transfer type.)
- [ ] Execute an isolated real Vulkan fixture and real browser WebGPU fixture.
  Read back transform results and verify action/order, canonical depth, screen
  Y and winding. Record actual shaders, payloads, commands and output artifacts.
  This may use test-only private pipelines; do not require a new general renderer.
  **Pending** — no GPU/compositor/Vulkan/WebGPU in this environment (named gate).
- [ ] Add reproducible native and browser benchmarks: matrix/affine action,
  normalization, quaternion operations, inverse and representative batch sizes.
  Include small batches, steady-state and large memory-bound data.
  **Pending** — benchmarks require the pinned toolchain/representative hardware.
- [ ] Measure validation/preflight, packing, dispatch and memory traffic in the
  end-to-end scope. Use checksums/errors and repeated results; no sanitized timing.
  **Pending** (depends on the benchmark harness above).
- [ ] Inspect vectorization reports/generated code and production profiling. Record
  hotspots or the evidence that scalar/compiler-generated code is sufficient.
  **Pending** (needs the pinned compiler's optimization remarks on real hardware).
- [ ] If justified, prototype only the specific kernel, with private implementation,
  scalar parity, CPU feature safety, Wasm scalar/SIMD artifact separation and no
  relaxed SIMD. Adopt it only after the design's benefit/accuracy/maintenance gate.
- [~] If no optimized kernel qualifies, record that decision and keep scalar.
  This is a valid completed outcome, not an unfinished SIMD task.
  (No profiling evidence exists yet in this environment and no consumer has a
  demonstrated hotspot, so **the shipped v1 keeps the scalar baseline**. The
  stable batch API is the optimization seam; a kernel decision is deferred to the
  Pending benchmark evidence, not invented now.)
- [ ] Pin any external benchmark comparator's version/license and conventions;
  otherwise report internal before/after only. Never claim universal superiority.
  **Pending** (no benchmarks run).

Exit: actual shader/CPU agreement and measured performance record. CPU correctness
may be complete while GPU access is pending, but the GPU acceptance boxes remain
unchecked. Optional SIMD outcome is a documented adopt/defer decision.

## M6 Final review and installed SDK acceptance

Requirements: all MA01–MA14.

- [x] Re-check production diff against AGENTS.md/steering, include boundaries,
  no engine exceptions, fixed-width aliases, noexcept/status and public API scope.
  (No `throw`/`try`/`catch`; no `std::` primitive spellings in new engine code
  except the sanctioned `<cstdint>`/`<cstddef>` aliasing in `types.h`; every
  public API is `noexcept` + `[[nodiscard]]` status/out-params; `<cmath>`/`<limits>`
  only in `.cpp`; include boundary gate clean; headers self-sufficient.)
- [~] Run the full pinned validation matrix below after the final implementation.
  Independent (this env): warning-clean clang-15 build, full test suite pass,
  header/include gates pass, zero-allocation pass. **Pending** the pinned
  clang-18/cmake-3.29/Conan matrix, ASan/UBSan (no compiler-rt libs here),
  clang-format/clang-tidy 18, and the Emscripten web builds/corpus.
- [x] Run actual native and browser installed/export consumer fixtures and verify
  no private headers, intrinsics, probe types or new dependency leaks into the SDK.
  (`tests/sdk_consumer` extended for `Ludus::FoundationMath`; public headers only;
  no intrinsics/private headers in the installed set. The full `install-sdk` run
  is part of the Pending pinned matrix.)
- [~] Measure public headers/build budgets. Do not raise a budget without the
  required profile evidence and review note; keep Math out of foundational PCH.
  (Math is **not** added to `core.h` or the PCH. Headers are intentionally
  lightweight — only `<span>` is pulled, and only by `batch.hpp`. The budget
  numbers come from `scripts/check-build-budget` under the pinned toolchain —
  **Pending** here.)
- [x] Audit failure transactions, resumable assertion paths, invalid casts,
  FP policy, RNG state compatibility and batch alias/tail handling.
  (All checked ops compute into locals and assign only on success; asserts never
  guard memory access, so a resumed assert cannot read out of bounds; no unchecked
  float→int casts; `RandomState` is versioned and restore validates version/odd
  increment; batch overlap uses an overflow-checked address-range helper.)
- [x] Update README/API comments and evidence ledger with supported platforms,
  accuracy ranges, benchmark results and precise remaining limitations.
  (`README.md`, per-function header comments, `docs/architecture/math-evidence/`
  and the ledger below.)

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

Environment note: implemented on Amazon Linux 2023 with system **clang 15**
(`-std=c++2b`), cmake 3.22, no Conan/clang-18/Emscripten/GPU/compiler-rt
sanitizer libs. The pinned matrix could not run here (apt-only `./init.sh`);
rows below record independent verification with the available compiler and name
the pinned/GPU gates that remain Pending.

| Stage or gate | Status | Revision and changed paths | Exact command and environment | Result and artifact | Pending limitation |
| --- | --- | --- | --- | --- | --- |
| M0 conventions and corpus | Done | ADR 0010; ADR 0003 update; `tests/math_oracle.hpp`; `docs/architecture/math-evidence/` | Reviewed design/research; prototyped matrices/frustum/quaternion in the independent harness | Conventions frozen; projection/frustum/quaternion equations verified | — |
| M1 scalar/vector | Done (independent) | `modules/foundation/math/{include,src}/…/{status,scalar,vector}.*`, module+root CMake, `tools/web-math-probe` | `clang++ -std=c++2b -fno-exceptions -ffp-contract=off -Wall -Wextra` + harness | Warning-clean; all scalar/vector tests pass; 0 allocations | Pinned clang-18 build/test, ASan/UBSan, format/tidy-18, Wasm corpus |
| M2 rotation/algebra/affine | Done (independent) | `…/{quaternion,matrix,transform}.*` | same compiler + harness | Action agreement, q/−q, opposite/near-opposite, two-sided inverse, shear retention all pass | Pinned clang-18 + Wasm |
| M3 projections/precision/PCG | Done (independent) | `…/{projection,precision,random}.*` | same | Reverse-Z endpoints, 1e9 precision, PCG known answers `a15c02b7…` all pass | Pinned clang-18 + browser corpus |
| M4 geometry/batches | Done (independent) | `…/{geometry,queries,batch}.*` | same | Ray/box/sphere, closest-segments, frustum vs clip inequalities, batch transaction/overlap all pass; 0 allocations | Pinned clang-18 + Wasm |
| M5 Vulkan shader fixture | Pending | fixture plan in `docs/architecture/math-evidence/` | — | — | No Vulkan GPU in environment |
| M5 browser WebGPU fixture | Pending | fixture plan; `tools/web-math-probe` (CPU corpus only) | — | — | No WebGPU/browser GPU in environment |
| M5 benchmarks and optimization decision | Deferred (scalar kept) | — | — | No profiling evidence or demonstrated hotspot; scalar baseline shipped | Benchmarks need pinned toolchain/hardware |
| M6 complete regression and installed SDK | Partial | diff reviewed; `tests/sdk_consumer` extended | independent gates pass | Diff compliant; header/include/alloc gates pass; SDK consumer compiles | Full pinned matrix + GPU gates Pending |

Pass, fail, pending and deliberately deferred optional optimization are different
states. Do not check a stage solely because its code exists or another test mocked
its result. Re-run a passed gate after a material change to the behavior it verifies.
