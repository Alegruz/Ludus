#pragma once

#include <ludus/foundation/base/core.h>

#include <ludus/foundation/math/status.hpp>
#include <ludus/foundation/math/vector.hpp>

namespace ludus::foundation::math
{
/// @brief Tensor-product bicubic Bezier patch over normalized (U,V) in [0,1]^2.
///
/// Plain value with no adjacency, units or texture mapping; prepared surfaces
/// above FoundationMath own topology and seams (ADR 0024).
struct BicubicBezierPatch final
{
    Vector3 Control[4][4]{}; ///< Control[u index][v index].
};
/// @brief Normalized patch parameters; distinct from texture coordinates.
struct PatchParameter final
{
    float64 U = 0; ///< First patch parameter, in [0,1] for checked operations.
    float64 V = 0; ///< Second patch parameter, in [0,1] for checked operations.
};
/// @brief Position and analytic partials at one parameter, per normalized U and V.
struct PatchSample final
{
    Vector3 Position; ///< Surface position.
    Vector3 DU;       ///< First partial with respect to U.
    Vector3 DV;       ///< First partial with respect to V.
    Vector3 DUU;      ///< Second partial with respect to U.
    Vector3 DUV;      ///< Mixed second partial.
    Vector3 DVV;      ///< Second partial with respect to V.
};
/// @brief Four children of a split, each over its own normalized [0,1]^2.
struct PatchSubdivision final
{
    BicubicBezierPatch U0V0; ///< Low U, low V rectangle.
    BicubicBezierPatch U1V0; ///< High U, low V rectangle.
    BicubicBezierPatch U0V1; ///< Low U, high V rectangle.
    BicubicBezierPatch U1V1; ///< High U, high V rectangle.
};
/// @brief Explicit conditioning thresholds for geometric normals; no universal epsilon.
struct PatchNormalPolicy final
{
    float64 MinDerivativeLength = 0; ///< In the patch's coordinate units.
    float64 MinSinAngle = 0;         ///< [0,1); scale-independent regularity threshold.
};

/// @brief Allocation-free, transactional outputs.
///
/// All controls and parameters must be finite; U,V are in [0,1]. Corners preserve stored values exactly. Rounded
/// float32 results use float64 intermediates, without a certified error bound.
/// Partials are with respect to normalized U,V. Position-only evaluation does
/// not require representable partials; full samples return OutOfRange if needed.
[[nodiscard]] MathStatus
TryEvaluatePatch(const BicubicBezierPatch& patch, PatchParameter parameter, Vector3& out) noexcept;
/// @brief Evaluate the position and all first/second partials, with the contract of the position overload.
/// @return OutOfRange when any of the six results overflows float32; out is then unchanged.
[[nodiscard]] MathStatus
TryEvaluatePatch(const BicubicBezierPatch& patch, PatchParameter parameter, PatchSample& out) noexcept;

/// @brief Four normalized children cover the low/high U and V intervals.
///
/// Input may alias a child of out. No adjacency, mesh tessellation or seam policy is implied.
[[nodiscard]] MathStatus
TrySplitPatch(const BicubicBezierPatch& patch, PatchParameter parameter, PatchSubdivision& out) noexcept;

/// @brief Geometric normal normalize(Cross(DU,DV)), using only finite DU/DV.
///
/// Exact zero partials or parallel partials return Degenerate; nonzero partials at/below the policy thresholds
/// return IllConditioned. No universal epsilon is imposed.
/// A singular normal does not invalidate the position. Output may alias DU/DV.
[[nodiscard]] MathStatus TryPatchNormal(const PatchSample& sample, PatchNormalPolicy policy, Vector3& out) noexcept;
} // namespace ludus::foundation::math
