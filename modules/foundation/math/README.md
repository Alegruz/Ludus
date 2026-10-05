# Ludus::FoundationMath

A small, allocation-free math vocabulary for 2D gameplay/UI, 3D camera and
transform work, primitive spatial queries, and repeatable gameplay random
streams. It depends only on `Ludus::FoundationBase` and is an opt-in layer: it is
**not** part of Base's `core.h` or the foundational PCH.

See the specification in `.kiro/specs/math/` (requirements, design, tasks), the
decision record `docs/decisions/0010-foundation-math-module.md`, and the
accuracy/benchmark ledger in `docs/architecture/math-evidence/`.

## Conventions (frozen)

- Right-handed world/local; **+X right, +Y up, camera looks toward −Z**.
- `Cross(+X, +Y) = +Z`; positive rotation follows the right hand.
- Column vectors: `pOut = M * pIn`. Matrix composition `A * B` applies **B first**
  (`world = parent * local`). Column-major storage; element access `At(row, col)`.
- Hamilton quaternions stored `(x, y, z, w)`, default identity `(0,0,0,1)`;
  `a * b` rotates by **b then a**; the rotations of `q` and `−q` are identical.
- Radians for angles (named `DegreesToRadians`/`RadiansToDegrees`); meter examples
  for distance. No implicit units, no global convention switch.
- Projection NDC: X/Y in `[-1, 1]`, **depth Z in `[0, 1]`, Y up** (WebGPU
  canonical). All projection factories are **reverse-Z** (near→1, far→0).
- Framebuffer coordinates are top-left, Y down, physical pixels.

## Error handling

Every API is `noexcept` (the engine is `-fno-exceptions`). Fallible operations
return `[[nodiscard]] MathStatus` and write caller-owned outputs **only on
success**; on any failure every output — including an aliased input — is left
unchanged. A geometric *miss* is `Success` plus a `Miss` classification, distinct
from invalid input (a failing status). There is no universal epsilon and no fuzzy
`operator==`; tolerances are named per operation (see `scalar.hpp` and the policy
structs). CPU results are numerically equivalent within documented bounds, **not**
bit-exact across libm/Wasm/GPU/compiler versions. The PCG integer sequence and
saved state are the one bit-exact contract.

## Quick examples

### 2D affine transform

```cpp
#include <ludus/foundation/math/transform.hpp>
using namespace ludus::foundation::math;

Affine2 t = Affine2::Identity();
t.Translation = Vector2{100.0f, 50.0f};        // move 100,50 px
const Vector2 screen = TransformPoint(t, Vector2{0.0f, 0.0f});
```

### Parent/local 3D composition

```cpp
TransformTRS parentData;
parentData.Scale = Vector3{2.0f, 1.0f, 1.0f};  // non-uniform scale
TransformTRS childData;
TryFromAxisAngle(Vector3{0, 0, 1}, kHalfPiF, childData.Rotation);

const Affine3 parent = ToAffine3(parentData);
const Affine3 child  = ToAffine3(childData);
const Affine3 world  = Compose(parent, child); // retains shear from the mix
const Vector3 p = TransformPoint(world, Vector3{1, 0, 0});
```

### Camera / view + projection

```cpp
#include <ludus/foundation/math/projection.hpp>

Matrix4 view{};
if (TryLookAt(Vector3{0, 2, 5}, Vector3{0, 0, 0}, Vector3{0, 1, 0}, view) == MathStatus::Success)
{
    Matrix4 proj{};
    // reverse-Z: clear depth to 0, compare Greater/GreaterEqual in the renderer.
    (void)TryPerspectiveReverseZ(DegreesToRadians(60.0f), 16.0f / 9.0f, 0.1f, 1000.0f, proj);
    const Matrix4 worldToClip = proj * view;
}
```

### Large-origin conversion

```cpp
#include <ludus/foundation/math/precision.hpp>

Vector3 local{};
// Subtract the shared double origin BEFORE narrowing to float.
(void)TryMakeRelative(playerWorldPosition, cameraOrigin, /*maxAbsComponent=*/4096.0, local);
```

### Random samples independent of job order

For checked examples, address ownership and reproduction requirements, see the
[wiki randomness guide](../../../docs/wiki/guides/randomness.md).

```cpp
#include <ludus/foundation/math/addressed_random.hpp>

RandomKey lootKey;
// These values belong to the world/content protocol. Never derive them from
// a worker index, pointer, iteration position or rendering frame.
if (TryMakeRandomKey(/*worldSeed=*/42, /*persistentLootDomain=*/7, lootKey) == MathStatus::Success)
{
    const RandomAddress drop{ /*entityId=*/123, /*dropOccurrence=*/5, /*itemSlot=*/0 };
    uint32 ticket = 0;
    if (TrySampleBounded(lootKey, drop, 1000, ticket) == MathStatus::Success)
    {
        // ticket repeats for this logical drop regardless of worker scheduling.
    }
}
```

Persist the root seed, stable domain/address assignments, generator identity
`Philox4x32-10`, and all three v1 contract IDs with the owner's replay/content
version. Use `TryMakeRandomAddress` before narrowing external counters. Reusing
an address repeats a sample; changing a bound changes its mapping. Checked
functions preserve outputs on failure. Rejection attempts stay inside the
address, have a 65536-attempt cap, and report `OutOfRange` on exhaustion. This is
non-cryptographic randomness. `PreparedBound32` amortizes threshold division;
`TrySampleBlock` shares one evaluation across four aligned dimensions.

References: Salmon et al., [Random123 (SC11)](https://www.thesalmons.org/john/random123/papers/random123sc11.pdf),
Lemire, [Fast Random Integer Generation in an Interval](https://arxiv.org/abs/1805.10941),
Vigna's [SplitMix64 finalizer](https://prng.di.unimi.it/splitmix64.c), and
Game Programming Gems 1 §2.0, 2 §1.16, and 3 §2.1. The
[architecture](../../../docs/architecture/randomness.md) and
[chapter review](../../../docs/architecture/randomness-gems-review.md) explain
the selected ideas and limitations. Random123's BSD notice ships in the SDK.

### Repeatable random stream

```cpp
#include <ludus/foundation/math/random.hpp>

RandomStream loot;
(void)loot.TryReseed(/*seed=*/questId, /*selector=*/kLootStream);
uint32 roll = 0;
(void)loot.TryNextBounded(100, roll);         // unbiased [0,100)
float chance = loot.NextFloat01();            // [0,1)
```

The caller owns the stream and its logical call order; save/restore
`RandomState` for bit-exact replay. Bounded sampling uses rejection and has no
hard worst-case iteration bound — do not call it from a deadline-bounded
infrastructure callback. This is not a cryptographic generator.

## Cubic curves and bicubic patches

Include `cubic.hpp` or `patch.hpp` explicitly. `CubicBezier1/2/3` have four
scalar/2D/3D controls; `BicubicBezierPatch` has `Control[u][v]`. Evaluation,
Hermite conversion and subdivision allocate nothing and preserve outputs on
failure. Inputs use normalized finite parameters in `[0,1]`; there is no implicit
clamping or extrapolation. Results are rounded float32 values computed using
float64 intermediates, without a certified error bound.

```cpp
#include <ludus/foundation/math/cubic.hpp>
using namespace ludus::foundation::math;

CubicBezier3 curve;
const CubicHermite3 source{{0, 0, 0}, {3, 6, 9}, {1, 2, 3}, {1, 2, 3}};
if (IsSuccess(TryFromHermite(source, 3.0, curve)))
{
    CubicSample3 sample;
    if (IsSuccess(TryEvaluateCubic(curve, 0.5, sample)))
    {
        // Position = (1.5,3,4.5). FirstDerivative is per normalized u.
        // Divide it by the source width 3 to recover the source-domain velocity.
    }
}
```

The position-only overload remains usable when derivatives overflow float32.
Patch samples include `DU`, `DV`, `DUU`, `DUV`, `DVV`. `TryPatchNormal` checks
analytic `Cross(DU,DV)` and an explicit `PatchNormalPolicy`; zero/parallel
partials are singular, and a caller can reject short or nearly parallel partials.
Subdivision produces two/four rounded children in their own normalized domains,
with matching stored split controls. Prepared paths, distance lookup, transported
frames, adjacency, meshes and editor documents remain future work. See the
[architecture](../../../docs/architecture/curves-surfaces.md) and
[kernel evidence](../../../docs/architecture/math-evidence/curves-surfaces.md).

## GPU transfer

CPU value layout is **not** a shader ABI. Use explicit flat-column export helpers
(no `reinterpret_cast` of a `Vector3`/`Matrix3`/`Affine3` into a buffer struct).
Depth clear/compare selection, the single Vulkan Y adaptation, and per-buffer
uniform/storage layout belong to the consuming graphics code. See the shader
fixture plan under `docs/architecture/math-evidence/`.

## PCG provenance and license

The PCG32 (XSH-RR) algorithm and seeding sequence in `random.hpp`/`random.cpp`
are adapted from Melissa O'Neill's PCG reference `pcg-c-basic`
(<https://www.pcg-random.org/>), distributed under the Apache License 2.0. Only
the small integer algorithm is reimplemented in Ludus style; no source file is
copied. The frozen known-answer sequence for `seed=42, selector=54` is
`a15c02b7, 7b47f409, ba1d3330, 83d2f293, bfa4784b, cbed606e`.
