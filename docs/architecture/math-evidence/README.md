# FoundationMath evidence ledger

Stable evidence for the math module's accuracy, platform execution and
performance. Rows are added as stages complete. Do not commit scratch book
extractions, PDFs, generated build output or machine-specific paths.

This checkout was implemented and verified on an **Amazon Linux 2023** sandbox
whose compiler is system **clang 15** (which accepts `-std=c++2b`), with
**cmake 3.22** and **no Conan / no pinned clang-18 / no Emscripten / no GPU /
no sanitizer runtime libraries**. The project's `./init.sh` only provisions the
pinned toolchain on apt-based Ubuntu, so the pinned validation matrix in
`.kiro/specs/math/tasks.md` could not be run here. Independent verification was
performed with the available compiler (see "Independent verification" below) and
the pinned/GPU gates are recorded as **Pending (toolchain/GPU unavailable)** so a
machine with the pinned toolchain can run them unchanged.

## Independent verification performed in this environment

Compiler: `clang version 15.0.7`, flags `-std=c++2b -fno-exceptions
-ffp-contract=off -Wall -Wextra`. All production `.cpp` files compile
**warning-clean**. The full Catch2 test suite logic (ported through a thin
test-macro shim because Catch2/Conan is unavailable here) and the standalone
analytic/metamorphic/failure/alias harnesses all pass:

- Scalar: lerp endpoints/extrapolation, smoothstep clamping, `NearlyEqual`
  infinity/negative-tolerance rules, exponential approach saturation.
- Vector: `Cross(X,Y)=Z`, robust length of 1e30 and 1e-25 magnitudes, checked
  normalize success/aliasing, failure-leaves-output-unchanged for
  zero/signed-zero/NaN/Inf, `TryNormalizeOr` fallback validation.
- Quaternion: `Rotate == Matrix3(q)·v` on an asymmetric vector, q/−q
  equivalence, rotation-between for equal/opposite/near-opposite directions,
  largest-component matrix→quaternion round trip near 180°, non-rotation
  rejection, Nlerp/Slerp endpoints and hemisphere correction, midpoint vs
  half-angle.
- Matrix/affine: two-sided inverse residual round-trip on a rotation+translation,
  scaling invariance, singular/near-singular rejection, in-place inverse,
  non-uniform-parent-scale + rotated-child **shear retention**, normal stays
  perpendicular to the transformed tangent, perspective divide guard.
- Projection: reverse-Z near→1/far→0 (finite and infinite), ortho ordering,
  LookAt degeneracy, viewport top-left corners and round trip, infinite-far
  `w=0` divide refusal.
- Geometry/queries: empty-vs-zero box, conservative AABB corner enclosure,
  ray/AABB (hit/parallel-miss/inside-start/zero-direction), ray/sphere
  (hit/tangent/miss/inside-start/negative-radius), closest segment points
  (skew/parallel/point), frustum extraction and conservative classification
  **agreeing with direct clip inequalities with no false-negative culling**,
  infinite frustum far-plane inactivity.
- Precision: 1e9 origin preserves a 0.5 m offset; wrong-order narrowing
  demonstrably loses it; range/validation rejections.
- Random: PCG32 known answers (`a15c02b7 …`), bound=0 non-consumption, bound=1
  and UINT32_MAX, float in `[0,1)`, state restore replay, even-increment /
  wrong-version / selector>2^63−1 rejection.
- Batch: per-element parity, odd counts and tails (0,1,3,4,7,8,15,16,17), exact
  in-place, partial-overlap rejection, size mismatch, transactional
  all-or-nothing on a non-finite element.
- Zero-allocation probe: representative checked operations run through the
  production path with a counting `operator new`/`delete`; **0 allocations**.
- Header self-sufficiency: all 12 public headers compile standalone.
- Foundational include-boundary checker (`tools/check_foundational_includes.py`):
  clean.

## Accuracy targets (initial, calibrated against the M0 corpus)

For ordinary well-scaled float32 fixtures:

| Quantity | Target |
| --- | --- |
| Unit-vector / unit-quaternion norm error | ≤ 2e-6 |
| Rotation angular error | ≤ 1e-5 rad |
| Matrix/affine action error (per component) | ≤ `1e-5 * max(1, refMagnitude)` |
| Matrix inverse | per `InversePolicy` (rcond ≥ 1e-6, residual ≤ 1e-5) |
| Bounds / frustum classification | conservative (no false-negative culling) |

Extreme/cancellation cases use absolute/domain-specific bounds, not these generic
relative targets, and are measured with higher-precision oracles
(`tests/math_oracle.hpp`) or atan2-based angle measurement near 0/π.

## Vulkan / WebGPU shader-fixture plan (M5)

The convention under test is that an asymmetric `Matrix4` (and a padded
`Matrix3` and an `Affine3` expanded to `mat4x3`/`Matrix4`) transforms points on
the GPU with the **same action and column order** as the CPU, and that reverse-Z
depth, canonical NDC and front-face winding match the documented contract.

Plan (not yet executed — no GPU in this environment):

1. A test-only payload type (`alignas(16)` float arrays, zeroed padding) built by
   the CPU flat-column export helpers — never a `reinterpret_cast` of a math
   value. `sizeof`/alignment/offsets/strides are asserted against the actual
   WGSL/SPIR-V declaration.
2. **WebGPU (browser):** a minimal compute or vertex pipeline multiplies a known
   asymmetric matrix by known points, reads the result back, and compares against
   the CPU result within the documented tolerance. Depth is checked by rendering a
   near and far fragment under a reverse-Z pipeline (clear 0, compare Greater) and
   reading the winning fragment. Uses a private test pipeline; it does **not**
   extend the RHI or expose probe handles as engine API, and does not alter the
   existing smoke clear/triangle paths.
3. **Vulkan (native):** the same payload and comparison through an isolated
   headless pipeline, additionally exercising the one graphics-owned Vulkan Y
   adaptation (negative viewport height) and front-face winding.
4. Record actual shaders, payload bytes, commands and read-back artifacts here.

Mocks, Node, or shader *compilation* alone do not certify GPU execution; until a
machine with a GPU/compositor runs these, the M5 GPU rows stay **Pending**.

## Benchmark plan (M5)

After numerical gates pass, record (native and browser): matrix/affine action,
normalization, quaternion ops, inverse, and batch transform/classify at
representative sizes (small, steady-state, large memory-bound), including
validation/preflight, packing and dispatch in the timed scope. Report revision,
hardware, compiler, flags, workload/counts, warmup, repetitions, medians/spread,
checksums/errors, allocations and binary size. There are **no measured
performance claims yet**; the optional SIMD kernel decision is deferred to that
evidence (scalar is the shipped, sufficient baseline absent a demonstrated
hotspot).
