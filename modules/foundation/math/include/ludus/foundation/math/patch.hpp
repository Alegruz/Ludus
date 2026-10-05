#pragma once

#include <ludus/foundation/base/core.h>

#include <ludus/foundation/math/status.hpp>
#include <ludus/foundation/math/vector.hpp>

namespace ludus::foundation::math
{
struct BicubicBezierPatch final
{
    Vector3 Control[4][4]{}; // Control[u index][v index].
};
struct PatchParameter final
{
    float64 U = 0;
    float64 V = 0;
};
struct PatchSample final
{
    Vector3 Position;
    Vector3 DU;
    Vector3 DV;
    Vector3 DUU;
    Vector3 DUV;
    Vector3 DVV;
};
struct PatchSubdivision final
{
    BicubicBezierPatch U0V0;
    BicubicBezierPatch U1V0;
    BicubicBezierPatch U0V1;
    BicubicBezierPatch U1V1;
};
struct PatchNormalPolicy final
{
    float64 MinDerivativeLength = 0; // In the patch's coordinate units.
    float64 MinSinAngle = 0;         // [0,1); scale-independent regularity threshold.
};

// Allocation-free, transactional outputs. All controls and parameters must be
// finite; U,V are in [0,1]. Corners preserve stored values exactly. Rounded
// float32 results use float64 intermediates, without a certified error bound.
// Partials are with respect to normalized U,V. Position-only evaluation does
// not require representable partials; full samples return OutOfRange if needed.
[[nodiscard]] MathStatus
TryEvaluatePatch(const BicubicBezierPatch& patch, PatchParameter parameter, Vector3& out) noexcept;
[[nodiscard]] MathStatus
TryEvaluatePatch(const BicubicBezierPatch& patch, PatchParameter parameter, PatchSample& out) noexcept;

// Four normalized children cover the low/high U and V intervals. Input may
// alias a child of out. No adjacency, mesh tessellation or seam policy is implied.
[[nodiscard]] MathStatus
TrySplitPatch(const BicubicBezierPatch& patch, PatchParameter parameter, PatchSubdivision& out) noexcept;

// Geometric normal normalize(Cross(DU,DV)), using only finite DU/DV. Exact zero
// partials or parallel partials return Degenerate; nonzero partials at/below the
// policy thresholds return IllConditioned. No universal epsilon is imposed.
// A singular normal does not invalidate the position. Output may alias DU/DV.
[[nodiscard]] MathStatus TryPatchNormal(const PatchSample& sample, PatchNormalPolicy policy, Vector3& out) noexcept;
} // namespace ludus::foundation::math
