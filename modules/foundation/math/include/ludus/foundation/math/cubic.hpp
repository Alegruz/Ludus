#pragma once

#include <ludus/foundation/base/core.h>

#include <ludus/foundation/math/status.hpp>
#include <ludus/foundation/math/vector.hpp>

namespace ludus::foundation::math
{
struct CubicBezier1 final
{
    float32 Control[4]{};
};
struct CubicBezier2 final
{
    Vector2 Control[4]{};
};
struct CubicBezier3 final
{
    Vector3 Control[4]{};
};

struct CubicSample1 final
{
    float32 Position = 0;
    float32 FirstDerivative = 0;
    float32 SecondDerivative = 0;
};
struct CubicSample2 final
{
    Vector2 Position;
    Vector2 FirstDerivative;
    Vector2 SecondDerivative;
};
struct CubicSample3 final
{
    Vector3 Position;
    Vector3 FirstDerivative;
    Vector3 SecondDerivative;
};

struct CubicHermite1 final
{
    float32 Start = 0;
    float32 End = 0;
    float32 StartDerivative = 0;
    float32 EndDerivative = 0;
};
struct CubicHermite2 final
{
    Vector2 Start;
    Vector2 End;
    Vector2 StartDerivative;
    Vector2 EndDerivative;
};
struct CubicHermite3 final
{
    Vector3 Start;
    Vector3 End;
    Vector3 StartDerivative;
    Vector3 EndDerivative;
};

// Allocation-free checked operations. All controls must be finite; u is finite
// and in [0,1]. Outputs are unchanged on failure. Arithmetic uses float64
// intermediates, with rounded float32 results; these are not certified bounds.
// Endpoints are preserved exactly, including signed zero. Derivatives are with
// respect to normalized u, not time or distance. Position-only evaluation can
// succeed when a derivative cannot be represented (sample returns OutOfRange).
[[nodiscard]] MathStatus TryEvaluateCubic(const CubicBezier1& curve, float64 u, float32& out) noexcept;
[[nodiscard]] MathStatus TryEvaluateCubic(const CubicBezier2& curve, float64 u, Vector2& out) noexcept;
[[nodiscard]] MathStatus TryEvaluateCubic(const CubicBezier3& curve, float64 u, Vector3& out) noexcept;
[[nodiscard]] MathStatus TryEvaluateCubic(const CubicBezier1& curve, float64 u, CubicSample1& out) noexcept;
[[nodiscard]] MathStatus TryEvaluateCubic(const CubicBezier2& curve, float64 u, CubicSample2& out) noexcept;
[[nodiscard]] MathStatus TryEvaluateCubic(const CubicBezier3& curve, float64 u, CubicSample3& out) noexcept;

// Hermite derivatives are per unit of the external domain. Its finite, positive
// width h gives B1 = Start + h*StartDerivative/3 and B2 = End - h*EndDerivative/3.
[[nodiscard]] MathStatus TryFromHermite(const CubicHermite1& hermite, float64 domainWidth, CubicBezier1& out) noexcept;
[[nodiscard]] MathStatus TryFromHermite(const CubicHermite2& hermite, float64 domainWidth, CubicBezier2& out) noexcept;
[[nodiscard]] MathStatus TryFromHermite(const CubicHermite3& hermite, float64 domainWidth, CubicBezier3& out) noexcept;

// Left/right cover [0,u]/[u,1], each with its own normalized parameter.
// Child derivatives therefore scale by u and 1-u, respectively. At endpoints one
// child is constant. Input may alias either output; outputs must be distinct.
[[nodiscard]] MathStatus
TrySplitCubic(const CubicBezier1& curve, float64 u, CubicBezier1& left, CubicBezier1& right) noexcept;
[[nodiscard]] MathStatus
TrySplitCubic(const CubicBezier2& curve, float64 u, CubicBezier2& left, CubicBezier2& right) noexcept;
[[nodiscard]] MathStatus
TrySplitCubic(const CubicBezier3& curve, float64 u, CubicBezier3& left, CubicBezier3& right) noexcept;
} // namespace ludus::foundation::math
