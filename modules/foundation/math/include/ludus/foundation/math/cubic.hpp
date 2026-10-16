#pragma once

#include <ludus/foundation/base/core.h>

#include <ludus/foundation/math/status.hpp>
#include <ludus/foundation/math/vector.hpp>

namespace ludus::foundation::math
{
/// @brief Scalar cubic Bezier span over normalized u in [0,1].
///
/// Plain value with no domain, units or continuity metadata; prepared owners
/// above FoundationMath supply those (ADR 0024).
struct CubicBezier1 final
{
    float32 Control[4]{}; ///< Bezier controls; Control[0] and Control[3] are the endpoints.
};
/// @brief 2D cubic Bezier span over normalized u in [0,1].
struct CubicBezier2 final
{
    Vector2 Control[4]{}; ///< Bezier controls; Control[0] and Control[3] are the endpoints.
};
/// @brief 3D cubic Bezier span over normalized u in [0,1].
struct CubicBezier3 final
{
    Vector3 Control[4]{}; ///< Bezier controls; Control[0] and Control[3] are the endpoints.
};

/// @brief Scalar position and analytic derivatives at one parameter.
struct CubicSample1 final
{
    float32 Position = 0;         ///< Value at u.
    float32 FirstDerivative = 0;  ///< d/du, per normalized u.
    float32 SecondDerivative = 0; ///< d2/du2, per normalized u squared.
};
/// @brief 2D position and analytic derivatives at one parameter.
struct CubicSample2 final
{
    Vector2 Position;         ///< Position at u.
    Vector2 FirstDerivative;  ///< d/du, per normalized u.
    Vector2 SecondDerivative; ///< d2/du2, per normalized u squared.
};
/// @brief 3D position and analytic derivatives at one parameter.
struct CubicSample3 final
{
    Vector3 Position;         ///< Position at u.
    Vector3 FirstDerivative;  ///< d/du, per normalized u.
    Vector3 SecondDerivative; ///< d2/du2, per normalized u squared.
};

/// @brief Scalar Hermite span: endpoints and derivatives per unit of an external domain.
///
/// The derivatives' domain (seconds, distance or knot units) is the caller's;
/// TryFromHermite receives its width explicitly.
struct CubicHermite1 final
{
    float32 Start = 0;           ///< Value at the domain start.
    float32 End = 0;             ///< Value at the domain end.
    float32 StartDerivative = 0; ///< Derivative at the start, per external domain unit.
    float32 EndDerivative = 0;   ///< Derivative at the end, per external domain unit.
};
/// @brief 2D Hermite span: endpoints and derivatives per unit of an external domain.
struct CubicHermite2 final
{
    Vector2 Start;           ///< Position at the domain start.
    Vector2 End;             ///< Position at the domain end.
    Vector2 StartDerivative; ///< Derivative at the start, per external domain unit.
    Vector2 EndDerivative;   ///< Derivative at the end, per external domain unit.
};
/// @brief 3D Hermite span: endpoints and derivatives per unit of an external domain.
struct CubicHermite3 final
{
    Vector3 Start;           ///< Position at the domain start.
    Vector3 End;             ///< Position at the domain end.
    Vector3 StartDerivative; ///< Derivative at the start, per external domain unit.
    Vector3 EndDerivative;   ///< Derivative at the end, per external domain unit.
};

/// @brief Allocation-free checked operations.
///
/// All controls must be finite; u is finite and in [0,1]. Outputs are unchanged on failure. Arithmetic uses float64
/// intermediates, with rounded float32 results; these are not certified bounds.
/// Endpoints are preserved exactly, including signed zero. Derivatives are with
/// respect to normalized u, not time or distance. Position-only evaluation can
/// succeed when a derivative cannot be represented (sample returns OutOfRange).
[[nodiscard]] MathStatus TryEvaluateCubic(const CubicBezier1& curve, float64 u, float32& out) noexcept;
/// @brief Evaluate a 2D cubic position at u, with the contract of the scalar overload.
/// @return NonFiniteInput, InvalidArgument for u outside [0,1], OutOfRange if the position overflows float32.
[[nodiscard]] MathStatus TryEvaluateCubic(const CubicBezier2& curve, float64 u, Vector2& out) noexcept;
/// @brief Evaluate a 3D cubic position at u, with the contract of the scalar overload.
/// @return NonFiniteInput, InvalidArgument for u outside [0,1], OutOfRange if the position overflows float32.
[[nodiscard]] MathStatus TryEvaluateCubic(const CubicBezier3& curve, float64 u, Vector3& out) noexcept;
/// @brief Evaluate a scalar cubic value and its first/second derivatives per normalized u.
/// @return OutOfRange when any of the three results overflows float32; out is then unchanged.
[[nodiscard]] MathStatus TryEvaluateCubic(const CubicBezier1& curve, float64 u, CubicSample1& out) noexcept;
/// @brief Evaluate a 2D cubic position and its first/second derivatives per normalized u.
/// @return OutOfRange when any of the three results overflows float32; out is then unchanged.
[[nodiscard]] MathStatus TryEvaluateCubic(const CubicBezier2& curve, float64 u, CubicSample2& out) noexcept;
/// @brief Evaluate a 3D cubic position and its first/second derivatives per normalized u.
/// @return OutOfRange when any of the three results overflows float32; out is then unchanged.
[[nodiscard]] MathStatus TryEvaluateCubic(const CubicBezier3& curve, float64 u, CubicSample3& out) noexcept;

/// @brief Hermite derivatives are per unit of the external domain.
///
/// Its finite, positive width h gives B1 = Start + h*StartDerivative/3 and B2 = End - h*EndDerivative/3.
[[nodiscard]] MathStatus TryFromHermite(const CubicHermite1& hermite, float64 domainWidth, CubicBezier1& out) noexcept;
/// @brief Convert a 2D Hermite span over an external domain of width domainWidth to Bezier controls.
/// @return NonFiniteInput, InvalidArgument for a nonpositive width, OutOfRange if a control overflows float32.
[[nodiscard]] MathStatus TryFromHermite(const CubicHermite2& hermite, float64 domainWidth, CubicBezier2& out) noexcept;
/// @brief Convert a 3D Hermite span over an external domain of width domainWidth to Bezier controls.
/// @return NonFiniteInput, InvalidArgument for a nonpositive width, OutOfRange if a control overflows float32.
[[nodiscard]] MathStatus TryFromHermite(const CubicHermite3& hermite, float64 domainWidth, CubicBezier3& out) noexcept;

/// @brief Left/right cover [0,u]/[u,1], each with its own normalized parameter.
///
/// Child derivatives therefore scale by u and 1-u, respectively. At endpoints one
/// child is constant. Input may alias either output; outputs must be distinct.
[[nodiscard]] MathStatus
TrySplitCubic(const CubicBezier1& curve, float64 u, CubicBezier1& outLeft, CubicBezier1& outRight) noexcept;
/// @brief Split a 2D cubic at u into normalized children, with the contract of the scalar overload.
/// @return NonFiniteInput; InvalidArgument for u outside [0,1] or identical outputs; OutOfRange when a child
/// control overflows float32. Both outputs are unchanged on failure.
[[nodiscard]] MathStatus
TrySplitCubic(const CubicBezier2& curve, float64 u, CubicBezier2& outLeft, CubicBezier2& outRight) noexcept;
/// @brief Split a 3D cubic at u into normalized children, with the contract of the scalar overload.
/// @return NonFiniteInput; InvalidArgument for u outside [0,1] or identical outputs; OutOfRange when a child
/// control overflows float32. Both outputs are unchanged on failure.
[[nodiscard]] MathStatus
TrySplitCubic(const CubicBezier3& curve, float64 u, CubicBezier3& outLeft, CubicBezier3& outRight) noexcept;
} // namespace ludus::foundation::math
