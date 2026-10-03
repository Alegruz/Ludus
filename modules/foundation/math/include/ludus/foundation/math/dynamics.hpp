#pragma once

#include <ludus/foundation/base/core.h>

#include <ludus/foundation/math/status.hpp>
#include <ludus/foundation/math/vector.hpp>

namespace ludus::foundation::math
{
// Exact coefficients for dv/dt = -rate * (v - constantTargetVelocity).
// displacement = target * dt + (velocity - target) * DistanceScale.
// velocityNext = target + (velocity - target) * VelocityScale.
struct LinearDragStep final
{
    float64 VelocityScale = 1.0;
    float64 DistanceScale = 0.0;
};

// Finite nonnegative rate/dt. Zero rate gives {1, dt}; tiny rates retain
// displacement precision. Outputs are unchanged on failure; no allocation.
[[nodiscard]] MathStatus TryComputeLinearDragStep(float64 rate, float64 dt, LinearDragStep& out) noexcept;

struct RadialSweepContact final
{
    bool Hit = false;
    float64 Fraction = 0.0;
    float64 Radius = 0.0;
    Vector3d Normal; // Outward unit normal; zero at a coincident center.
};

// Earliest overlap of a linearly moving point and a radial band whose radius
// interpolates from radiusStart to radiusEnd over the SAME normalized interval.
// halfWidth includes any hull radius. Supports expansion/shrinkage and initial
// overlap. Finite coordinates, nonnegative finite radii/width required. A miss
// is Success with Hit=false. Numeric range failures leave out unchanged.
// Use Z=0 for 2D annuli. This is geometry, not a force or fluid integrator.
[[nodiscard]] MathStatus TrySweepRadialBand(Vector3d start,
                                            Vector3d end,
                                            Vector3d origin,
                                            float64 radiusStart,
                                            float64 radiusEnd,
                                            float64 halfWidth,
                                            RadialSweepContact& out) noexcept;
} // namespace ludus::foundation::math
