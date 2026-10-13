#pragma once

// Compensated (Neumaier) floating-point summation. The result reproduces the
// CPython 3.12+ sum() of floats bit for bit, which offline tools rely on when
// they must match reference outputs exactly. Lives in summation.cpp so it is
// compiled with the module's precise FP flags (no contraction).

#include <ludus/foundation/base/core.h>

#include <ludus/foundation/math/vector.hpp>

namespace ludus::foundation::math
{
using core::float64;

/// @brief Neumaier compensated accumulator; default state is an empty sum (0.0).
class CompensatedSum final
{
public:
    /// Add one term to the running sum.
    void Add(float64 value) noexcept;
    /// Total plus the running compensation; the compensation is ignored when it is zero or non-finite.
    [[nodiscard]] float64 GetResult() const noexcept;

private:
    float64 mTotal = 0.0;
    float64 mCompensation = 0.0;
};

/// Dot product whose three products are added with CompensatedSum in X, Y, Z order.
[[nodiscard]] float64 CompensatedDot(Vector3d a, Vector3d b) noexcept;
} // namespace ludus::foundation::math
