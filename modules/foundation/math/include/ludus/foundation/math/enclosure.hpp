#pragma once

// Outward-rounded float64 enclosures (ADR 0024, Certified guarantee class).
//
// Every operation rounds to nearest, as the process-wide IEEE mode already does,
// and then widens each bound by at most one float64 unit in the outward
// direction. A correctly rounded +, -, * or square root lies strictly between
// the neighbors of its rounded result unless it overflows, so the widened
// result encloses the exact real result for every pair of operands inside the
// input enclosures. Sums and differences detect an exact result with an
// error-free transformation and widen only the side that needs it. The process
// rounding mode is never changed. The implementation is compiled with
// contraction disabled; see the FoundationMath module page for validation.

#include <ludus/foundation/base/core.h>

#include <ludus/foundation/math/status.hpp>

namespace ludus::foundation::math
{
using core::float64;

/// @brief Closed float64 interval [Lower, Upper] that encloses an exact real value.
///
/// A valid enclosure has finite bounds with Lower <= Upper. A single value is
/// the enclosure [value, value]. Enclosures produced by the outward operations
/// below contain the exact real result of the same operation on every pair of
/// exact operands inside the inputs.
struct Enclosure final
{
    float64 Lower = 0.0; ///< Largest proven lower bound.
    float64 Upper = 0.0; ///< Smallest proven upper bound.
};

/// @brief Enclosure of one exactly known value.
/// @param value Finite value; the result is valid only when it is finite.
[[nodiscard]] constexpr Enclosure MakePointEnclosure(float64 value) noexcept
{
    return Enclosure{ .Lower = value, .Upper = value };
}

/// @brief Whether both bounds are finite and Lower <= Upper.
[[nodiscard]] bool IsValidEnclosure(Enclosure enclosure) noexcept;

/// @brief Next float64 toward positive infinity (IEEE nextafter); NaN stays NaN, +infinity stays +infinity.
[[nodiscard]] float64 NextFloat64Up(float64 value) noexcept;
/// @brief Next float64 toward negative infinity (IEEE nextafter); NaN stays NaN, -infinity stays -infinity.
[[nodiscard]] float64 NextFloat64Down(float64 value) noexcept;

/// @brief Enclose a + b for every a in left and b in right.
/// @return NonFiniteInput for a nonfinite bound, InvalidArgument for Lower > Upper, OutOfRange when a bound
/// overflows. outEnclosure is unchanged on failure. An exact float64 sum is not widened.
[[nodiscard]] MathStatus TryAddOutward(Enclosure left, Enclosure right, Enclosure& outEnclosure) noexcept;
/// @brief Enclose a - b for every a in left and b in right, with the TryAddOutward contract.
[[nodiscard]] MathStatus TrySubtractOutward(Enclosure left, Enclosure right, Enclosure& outEnclosure) noexcept;
/// @brief Enclose a * b for every a in left and b in right.
///
/// Each candidate endpoint product is widened by one unit on both sides unless an
/// operand is exactly zero, so products are never claimed to be exact.
/// @return The TryAddOutward statuses; outEnclosure is unchanged on failure.
[[nodiscard]] MathStatus TryMultiplyOutward(Enclosure left, Enclosure right, Enclosure& outEnclosure) noexcept;
/// @brief Enclose a * a for every a in value; the lower bound is 0 when value contains 0.
/// @return The TryAddOutward statuses; outEnclosure is unchanged on failure.
[[nodiscard]] MathStatus TrySquareOutward(Enclosure value, Enclosure& outEnclosure) noexcept;
/// @brief Enclose the square root of every a in value.
/// @return The TryAddOutward statuses, and InvalidArgument when value.Lower < 0. The lower bound is never
/// negative. outEnclosure is unchanged on failure.
[[nodiscard]] MathStatus TrySquareRootOutward(Enclosure value, Enclosure& outEnclosure) noexcept;
} // namespace ludus::foundation::math
