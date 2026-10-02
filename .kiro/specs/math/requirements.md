# Ludus math requirements

Status: proposed implementation contract, 2026-10-01. No math engine code has
been implemented or measured by this handoff. Read [design.md](design.md),
[tasks.md](tasks.md), and the self-contained
[research](../../../docs/architecture/math-research.md).

## Purpose and scope

Supply a small, allocation-free math vocabulary for 2D gameplay/UI, 3D camera
and transform work, primitive spatial queries, and repeatable gameplay random
streams. Favor unambiguous conventions, numerical failure contracts, readable
debugger values and measured throughput. Native Linux/Clang and the pinned
Emscripten browser target must share the same public API and numerical meaning.

### MA01 Repository and SDK integration

- Create `modules/foundation/math/`, static target
  `ludus_foundation_math`, exported as `Ludus::FoundationMath`.
- Depend only on FoundationBase. Math requires no logger, profiler, allocator,
  containers, Platform, graphics API, jobs, initialization or global service.
- Install only public headers. Base's foundational include set and PCH do not
  import Math. Revalidate current checkout and preserve unrelated edits.

### MA02 Readable values and limited scope

- Provide concrete `Vector2`, `Vector3`, `Vector4`, `Vector3d`, `Quaternion`,
  `Matrix3`, `Matrix4`, `Affine2`, `Affine3`, and `TransformTRS` values.
- Use Ludus fixed-width types. Store visible components/columns, with fixed
  layouts and no owning memory, SIMD register fields, virtual functions, hidden
  caches, swizzle proxies, expression templates or generic dimension framework.
- Zero-initialize vectors; default quaternion and transforms to identity;
  default general matrices to zero with named identity factories.

### MA03 Mathematical conventions

- Freeze right-handed world/local coordinates, +Y up, camera forward -Z,
  column-vector action, column-major matrices and Hamilton quaternion order
  `(x,y,z,w)`. `A * B` applies B first for matrices, affine values and rotations.
- Use meters for geometric distance examples and radians for angles. Expose
  named degree/radian conversion, with no implicit units or runtime convention
  switches. Framebuffer coordinates are top-left with Y down.
- Maintain action/composition tests, not merely memory-layout tests.

### MA04 Numerical policy

- Preserve NaN/Inf detection and signed zero assumptions; no global fast math,
  approximate transcendental replacements, reciprocal bit hacks or floating-point
  environment mutation. Checked algorithms use controlled precise compilation.
- Specify per-operation domains and tolerances. No universal epsilon, fuzzy
  `operator==`, or claim of cross-platform bitwise float determinism.
- Add narrow policy documentation for standard math implementation headers.
  Every API is `noexcept`; C++ exceptions remain disabled for engine code.

### MA05 Explicit failures

- Fallible normalization, inverse, view/projection, plane, relative conversion
  and query APIs return `[[nodiscard]]` status and caller-owned outputs.
- On any failure, all outputs remain unchanged. Support exact input/output
  aliasing for single-value operations by computing into temporaries.
- Distinguish query miss from invalid/numerically unsupported input. No silent
  identity inverse, zero quaternion, invented hit or implicit content repair.
- Assertions diagnose programmer preconditions, not replace checked guards;
  resumable assertions must not cause out-of-range memory access.

### MA06 Scalar and vector operations

- Supply component arithmetic, exact equality, dot/cross, length/distance,
  robust checked normalization, lerp, clamp, smoothstep and exponential approach.
- Define endpoints, extrapolation, invalid intervals and finite-input behavior.
  Component multiplication is named `Hadamard`; vector-vector `*` is absent.
- Validate extremes and zero/subnormal cases. Ordinary arithmetic can overflow;
  checked numerical operations must detect unsupported results.

### MA07 Rotations

- Implement axis-angle construction, Hamilton composition, conjugate, vector
  rotation, robust rotation-between-directions and matrix conversion.
- Handle equal, opposite and near-opposite directions. Matrix-to-quaternion
  conversion rejects scale/shear/reflection and remains stable near 180 degrees.
- Supply distinct shortest-path nlerp/slerp; normalize results, correct hemisphere
  and handle close-angle fallback. Make unit-input and interpolation domains
  explicit. Preserve quaternion sign continuity rather than forcing W positive.

### MA08 Matrices and transforms

- Implement Matrix3/4 action, multiplication, transpose and checked inverse.
  Use scale/condition-aware checks and verify the narrowed result.
- Use affine composition for 2D/3D transforms. TRS is authoring data and converts
  to affine; there is no TRS composition/decomposition that silently loses shear.
- Distinguish points, vectors and normals by named functions. Transform normals
  with the inverse transpose. Singular transforms may transform points but fail
  inverse/normal operations explicitly.

### MA09 Projection and viewport

- Canonical clip volume is `-w <= x,y <= w`, `0 <= z <= w`. Provide finite and
  infinite reverse-Z perspective and finite reverse-Z orthographic factories.
- Validate all factory inputs. Reverse-Z contracts pair near=1/far=0 with depth
  clear=0 and greater/greater-equal comparison in a future renderer.
- Expose checked perspective division and framebuffer conversion. Document one
  graphics-owned Vulkan Y adaptation and test winding/packing without adding
  a renderer or changing the existing smoke pipelines.

### MA10 Primitive geometry

- Supply `Plane`, `Aabb2`, `Aabb3`, `Sphere`, `Ray3`, `RayInterval`, `Frustum`
  and structured query results. Specify boundary, parameter and invalid-box rules.
- Implement signed plane distance, conservative affine AABB bounds, ray/AABB,
  ray/sphere, closest segment points and conservative frustum tests.
- Handle zero ray components, tangency, inside starts, empty versus degenerate
  boxes, constrained segment endpoints, and inactive infinite-far planes.
- No BVH/kD-tree, scene traversal, collision world, contact solver or exact
  topology claim belongs in v1.

### MA11 Large coordinates and GPU transfer

- Support checked double position minus double origin, followed by float
  narrowing with an explicit local range. Math owns no origin or world state.
- Preserve compact CPU values. GPU payloads have explicit columns, padding,
  offsets and matrix strides with CPU tests and actual shader fixtures.
- Never reinterpret a CPU Vector3/Matrix3/Affine3 as a shader buffer structure.
  Per-buffer uniform/storage layout belongs to the consuming graphics code.

### MA12 Randomness

- Provide an opt-in, caller-owned PCG32 XSH-RR stream, explicit seed/selector,
  next integer, unbiased exclusive bounded integer, and `[0,1)` float.
- Freeze versioned state and known-answer sequence; no time/global reseeding,
  shared mutable RNG, platform standard distributions or cryptographic promise.
- Validate bounds/state without consumption on invalid input. State replay is
  bit-exact for the same calls; rejection loops are not hard real-time bounded.

### MA13 Batch optimization

- Start with scalar batch point transforms and sphere/frustum classification.
  Use caller-owned spans, stable order, no allocation and explicit overlap rules.
- A later SIMD kernel must keep the public layout/API, have scalar differential
  and tail/unaligned tests, and show measured end-to-end gain on its actual target.
- Count repacking, dispatch, load/store traffic and output checking. No mandatory
  ISPC, AVX baseline, `-march=native`, relaxed Wasm SIMD or generic backend manager.

### MA14 Debugging and validation

- Pure math does not log or profile every operation. Document invariants and
  tolerance units; caller tools record inputs, status and replay seed.
- Require independent numeric oracles, action/metamorphic tests, boundary and
  failure tests, no-allocation checks, native sanitizers, warning/format/tidy,
  public-header budgets, installed SDK consumption and real Wasm execution.
- Actual Vulkan/WebGPU shader fixtures verify transfer convention separately
  from CPU correctness; mocks or shader compilation alone do not satisfy them.
- Keep an evidence ledger. Performance or GPU gates without measurements stay
  pending; optional SIMD is unnecessary when no justified hotspot exists.

## Deferred features

Physics integrators, IK, animation curves/squad, quaternion compression,
geometric algebra, exact predicates/CSG, procedural noise, distributions beyond
uniform, fixed-point lockstep, SIMD register APIs, full double math, reflection,
serialization frameworks, collision acceleration and scene/editor services need
separate consumer requirements. They are not prerequisites for this module.
