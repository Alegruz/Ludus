# Ludus::FoundationGeometry

Prepared spatial paths, scalar tracks and bounded distance maps above the
FoundationMath cubic kernel. This is phase G2 of the
[curves and surfaces architecture](../../architecture/curves-surfaces.md). Names,
statuses, domains and guarantee classes follow
[ADR 0024](../../decisions/0024-curves-surfaces-contracts.md). Validation is
recorded in the [evidence ledger](../../architecture/math-evidence/curves-surfaces.md#g2-prepared-paths-tracks-and-distance-maps).

The module depends on `Ludus::FoundationBase`, `Ludus::FoundationMath` and
`Ludus::FoundationContainers`. It is not in `core.h` or the foundational PCH.
Include the header you use:

| Header | Contents |
| --- | --- |
| `status.hpp` | `GeometryStatus`, `GuaranteeClass`, domain values, `SpanSide`, `BoundaryPolicy`, `ToleranceProfile`, `WorkLimits`, `ComputePositionFloor`, `GeometryDiagnostic`; includes only Base |
| `path.hpp` | `PathSource`, `TryPreparePath`, `PreparedPath`, `PathView`, sampling, tangents, control bounds, joint continuity, Hermite and centripetal Catmull-Rom conversion |
| `distance.hpp` | `TryBuildDistanceMap`, `PreparedDistanceMap`, `TryBoundDistance`, `TryResolveDistance` |
| `track.hpp` | `TrackKey`, `TryPrepareTrack`, `PreparedTrack`, `TryEvaluateTrack` |

Frames, projection, surfaces and tessellation (`frame.hpp`, `query.hpp`,
`surface.hpp`, `tessellation.hpp`) belong to later phases and do not exist yet.

## Contracts

Every function is `noexcept`; fallible ones return `[[nodiscard]] GeometryStatus`
and leave outputs unchanged on failure. Preparation builds a complete candidate
and moves it into the output owner only on success, so a failed rebuild keeps the
previous snapshot. Preparation reports the first failure through an optional
`GeometryDiagnostic`. Sampling, inversion and conversion into caller-owned ranges
never allocate. Owners are move-only and single-owner; views borrow them and are
invalidated by moving or destroying the owner.

- **Domains.** `SegmentParameter` is a span index and u in [0,1].
  `PathDistance` is prefix arc length in asset units. `TrackTime` is seconds.
  They are separate types. Kernel derivatives are per normalized u; track
  derivatives are per second.
- **Span sides.** An exact boundary selects the outgoing span at u = 0. The
  terminal endpoint selects the last span at u = 1. `SpanSide::Incoming` selects
  the previous span at u = 1; at the first endpoint both sides select the first
  span. A Step span holds its left value until u = 1.
- **Boundary policies.** Policies act only outside the closed domain. Paths
  accept `Reject`, `Clamp` and, when closed, `Wrap`. Tracks accept `Reject`,
  `Clamp`, `Hold`, `Loop` and `PingPong`. Every other combination is
  `InvalidArgument`, and nothing extrapolates.
- **Precision.** Source controls are float64. Preparation subtracts the chunk
  origin in float64, then narrows to float32. The effective position error,
  max(`PositionError`, `RelativePositionError` x `AssetScale`), must be at least
  the float32 position floor `2 * ulp32(E)` of the narrowed controls and their
  narrowing error. Otherwise preparation returns `UnsupportedPrecision`; split
  the asset into chunks or choose a nearer origin.
- **Connectivity.** Consecutive spans, and the closing joint of a closed path,
  must meet within `ContinuityPosition` (`InvalidTopology` otherwise). Each
  joint's C0/G1/C1/C2 classification compares derivatives scaled by each span's
  `DomainWidth` and stores the measured differences. It is a threshold decision
  on Rounded values.
- **No defaults.** `ToleranceProfile` and `WorkLimits` have no engine
  defaults. Every tolerance field is validated against its ADR 0024 range, and a
  zero work limit allows no work.

## Guarantees

| Result | Class |
| --- | --- |
| Stored endpoints, key values, span control boxes, position floor | Exact |
| Interior positions, derivatives, tangents, conversions, continuity values | Rounded |
| Leaf and total length bounds, prefix bounds, inversion residual | Certified |
| `TotalEstimate`, `PrefixEstimate`, `DistanceBound::Estimate` | Estimated, inside the Certified interval |

Certified bounds come from FoundationMath's [outward-rounded enclosures](math.md#outward-rounded-enclosures).
A strict `DistancePreparation` (the default) accepts them; there is no Estimated
fallback mode yet. Linear table lookup (an Approximate mode) is not provided.

## Paths

```cpp
#include <ludus/foundation/geometry/distance.hpp>
#include <ludus/foundation/geometry/path.hpp>
using namespace ludus::foundation::geometry;
using ludus::foundation::BorrowedView;
using ludus::foundation::math::Vector3d;

// Waypoints with uneven spacing: centripetal Catmull-Rom uses sqrt(chord) knots.
const Vector3d points[] = {{0, 0, 0}, {1, 0, 0}, {1, 4, 0}, {10, 4, 0}};
PathSpanSource spans[3];
usize spanCount = 0;
GeometryStatus status = TryConvertCentripetalCatmullRom(
    BorrowedView<const Vector3d>(points), false, 1e-6, BorrowedView<PathSpanSource>(spans), spanCount);

PreparedPath path;
if (IsSuccess(status))
{
    const PathSource source{.Spans = BorrowedView<const PathSpanSource>(spans, spanCount), .Revision = 1};
    status = TryPreparePath(source, PathPreparation{.Tolerance = tolerance, .Limits = limits}, path, nullptr);
}
```

`TryConvertCentripetalCatmullRom` uses the nonuniform Barry-Goldman tangent at
interior waypoints and the one-sided chord tangent at open endpoints. It rejects
consecutive waypoints within `coincidenceDistance` as `InvalidDomain`; editors
should offer a visible merge. A closed source wraps neighbors. It must not repeat
its first waypoint at the end: the repeated pair is coincident, so it is rejected.
`TryConvertHermite` applies `B1 = P0 + h*T0/3` and `B2 = P1 - h*T1/3` with each
waypoint's explicit `OutgoingWidth`. Both conversions check output capacity
before writing (`CapacityExceeded`).

`TryEvaluatePath` returns a chunk-relative position, or a `CubicSample3` with
derivatives per u. Add `PathView::Origin` in float64 for asset coordinates.
`TryEvaluatePathTangent` returns `Degenerate` where |dP/du| is at or below a
caller-supplied speed, for example at a cusp, a zero-speed ease endpoint or a
stationary span. Position at those points remains valid.

## Distance maps

`TryBuildDistanceMap` brackets each leaf's arc length between its chord and its
control-polygon length, using outward rounding. It splits the leaf with the
largest gap at its parameter midpoint, with ties broken by earlier span and
parameter, until the whole-path gap `TotalUpper - TotalLower` is at most the
path's `DistanceError`. The gap is rechecked in path order before success.
`MaxLeaves`, `MaxDepth`, `MaxSubdivisions` and `MaxPreparedBytes` bound the work.
Exhausting a limit returns `BudgetExceeded`, reports the achieved Certified gap in
the diagnostic and keeps the previous map.

```cpp
PreparedDistanceMap map;
TryBuildDistanceMap(path.GetView(), DistancePreparation{.Limits = limits}, map, nullptr);

DistanceResolution at;
const DistanceQuery query{.Distance = PathDistance{.Value = 2.5}, .Policy = BoundaryPolicy::Clamp,
                          .MaxIterations = 128};
if (IsSuccess(TryResolveDistance(path.GetView(), map.GetView(), query, at)))
{
    // |L(at.Parameter) - 2.5| <= at.ResidualBound <= DistanceError (Certified).
}
```

The `PathDistance` domain is [0, `TotalEstimate`]. `TryResolveDistance` finds
the leaf by monotone lookup over prefix estimates, then bisects partial-span
estimates for the earliest parameter that reaches the target. It accepts only
when the Certified residual, which includes every preceding leaf's uncertainty,
fits `DistanceError`. A target equal to a stored boundary estimate resolves to
that exact boundary. Zero-length runs resolve to their earliest parameter, and
the path end resolves to the last span at u = 1. Near zero speed, u(s) is
ill-conditioned even though its residual is bounded; request a tangent to detect
that case. Allow at least 64 iterations so bisection can reach float64
resolution. `TryBoundDistance` returns the Certified prefix enclosure at any
parameter.

A map records its path's `Revision` and span count. Queries through an unbuilt
map return `MissingCache`, and queries through a map of another revision return
`StaleRevision`. Sampling never builds or repairs a cache. Distance uses the
asset's local metric; nonuniform scale needs a separate world-metric cache.

## Tracks

```cpp
#include <ludus/foundation/geometry/track.hpp>

const TrackKey keys[] = {
    {.Time = 0.0, .Value = 0.0F, .Interpolation = TrackInterpolation::Linear},
    {.Time = 1.0, .Value = 1.0F, .OutSlope = 2.0F, .Interpolation = TrackInterpolation::Hermite},
    {.Time = 3.0, .Value = 1.0F, .Interpolation = TrackInterpolation::Step},
    {.Time = 3.5, .Value = 0.0F},
};
PreparedTrack track;
TrackSample sample;
if (IsSuccess(TryPrepareTrack(BorrowedView<const TrackKey>(keys), limits, track, nullptr)))
{
    TryEvaluateTrack(track.GetView(), TrackTime{.Seconds = 4.5}, BoundaryPolicy::PingPong, SpanSide::Outgoing,
                     sample);
    // Reflected to 2.5 s: Value 1.1875, DerivativePerSecond +0.625 (negated on the reflected half).
}
```

Key times must be strictly increasing; equal times are `InvalidDomain`, and the
runtime never resolves a tie. Each key names the interpolation of the span that
starts at it. Hermite spans use the left key's `OutSlope` and the right key's
`InSlope`, in value units per second. They may overshoot, and evaluation never
clamps the result. Author a timed hold as equal values over a positive interval.
`Hold` returns the endpoint value with zero derivative. `Clamp` returns the
endpoint value with the one-sided derivative from inside the domain.

## Not yet provided

Path cursors for coherent traversal, a Visual-only Estimated build, Approximate
table lookup, transported frames, projection, sweeps, surfaces, tessellation,
cooked formats and editor adapters are later phases. Performance and memory
budgets are unmeasured.
