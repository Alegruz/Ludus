// Outward-rounded enclosures (see enclosure.hpp).
//
// Thanks to Ramon E. Moore, R. Baker Kearfott and Michael J. Cloud, "Introduction
// to Interval Analysis", SIAM, 2009, chapter 2 (the interval number system):
// adopt endpoint interval arithmetic whose rounded bounds are widened outward so
// they still enclose the exact result. Departure: instead of switching the
// processor rounding mode, round to nearest and step one float64 unit outward,
// which needs no global state and works under WebAssembly.
// Thanks to Donald E. Knuth, "The Art of Computer Programming", Volume 2, 3rd
// edition, section 4.2.2 (Theorem B, the TwoSum error-free transformation): an
// exact sum is detected and not widened.

#include <ludus/foundation/math/enclosure.hpp>

#include <ludus/foundation/math/scalar.hpp>

#include <cmath>
#include <limits>

namespace ludus::foundation::math
{
namespace
{
constexpr float64 POSITIVE_INFINITY = std::numeric_limits<float64>::infinity();

[[nodiscard]] MathStatus ValidateOperand(Enclosure value) noexcept
{
    if (IsFinite(value.Lower) == false || IsFinite(value.Upper) == false)
    {
        return MathStatus::NonFiniteInput;
    }
    return value.Lower <= value.Upper ? MathStatus::Success : MathStatus::InvalidArgument;
}

// Rounding error of sum = fl(a + b); exact when no overflow occurs (Knuth's TwoSum).
// NOLINTBEGIN(bugprone-easily-swappable-parameters): symmetric TwoSum operands and their rounded sum.
[[nodiscard]] float64 ComputeSumError(float64 a, float64 b, float64 sum) noexcept
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    const float64 virtualB = sum - a;
    const float64 virtualA = sum - virtualB;
    return (a - virtualA) + (b - virtualB);
}

[[nodiscard]] float64 AddDown(float64 a, float64 b) noexcept
{
    const float64 sum = a + b;
    return ComputeSumError(a, b, sum) < 0.0 ? NextFloat64Down(sum) : sum;
}

[[nodiscard]] float64 AddUp(float64 a, float64 b) noexcept
{
    const float64 sum = a + b;
    return ComputeSumError(a, b, sum) > 0.0 ? NextFloat64Up(sum) : sum;
}

[[nodiscard]] float64 MultiplyDown(float64 a, float64 b) noexcept
{
    if (a == 0.0 || b == 0.0)
    {
        return 0.0;
    }
    return NextFloat64Down(a * b);
}

[[nodiscard]] float64 MultiplyUp(float64 a, float64 b) noexcept
{
    if (a == 0.0 || b == 0.0)
    {
        return 0.0;
    }
    return NextFloat64Up(a * b);
}

[[nodiscard]] MathStatus Commit(Enclosure result, Enclosure& outEnclosure) noexcept
{
    if (IsFinite(result.Lower) == false || IsFinite(result.Upper) == false)
    {
        return MathStatus::OutOfRange;
    }
    outEnclosure = result;
    return MathStatus::Success;
}
} // namespace

bool IsValidEnclosure(Enclosure enclosure) noexcept
{
    return ValidateOperand(enclosure) == MathStatus::Success;
}

float64 NextFloat64Up(float64 value) noexcept
{
    return std::nextafter(value, POSITIVE_INFINITY);
}

float64 NextFloat64Down(float64 value) noexcept
{
    return std::nextafter(value, -POSITIVE_INFINITY);
}

MathStatus TryAddOutward(Enclosure left, Enclosure right, Enclosure& outEnclosure) noexcept
{
    MathStatus status = ValidateOperand(left);
    if (status == MathStatus::Success)
    {
        status = ValidateOperand(right);
    }
    if (status != MathStatus::Success)
    {
        return status;
    }
    return Commit(Enclosure{ .Lower = AddDown(left.Lower, right.Lower), .Upper = AddUp(left.Upper, right.Upper) },
                  outEnclosure);
}

MathStatus TrySubtractOutward(Enclosure left, Enclosure right, Enclosure& outEnclosure) noexcept
{
    MathStatus status = ValidateOperand(left);
    if (status == MathStatus::Success)
    {
        status = ValidateOperand(right);
    }
    if (status != MathStatus::Success)
    {
        return status;
    }
    return Commit(Enclosure{ .Lower = AddDown(left.Lower, -right.Upper), .Upper = AddUp(left.Upper, -right.Lower) },
                  outEnclosure);
}

MathStatus TryMultiplyOutward(Enclosure left, Enclosure right, Enclosure& outEnclosure) noexcept
{
    MathStatus status = ValidateOperand(left);
    if (status == MathStatus::Success)
    {
        status = ValidateOperand(right);
    }
    if (status != MathStatus::Success)
    {
        return status;
    }
    const float64 lowerLower = MultiplyDown(left.Lower, right.Lower);
    const float64 lowerUpper = MultiplyDown(left.Lower, right.Upper);
    const float64 upperLower = MultiplyDown(left.Upper, right.Lower);
    const float64 upperUpper = MultiplyDown(left.Upper, right.Upper);
    const float64 lower = Min(Min(lowerLower, lowerUpper), Min(upperLower, upperUpper));
    const float64 highLowerLower = MultiplyUp(left.Lower, right.Lower);
    const float64 highLowerUpper = MultiplyUp(left.Lower, right.Upper);
    const float64 highUpperLower = MultiplyUp(left.Upper, right.Lower);
    const float64 highUpperUpper = MultiplyUp(left.Upper, right.Upper);
    const float64 upper = Max(Max(highLowerLower, highLowerUpper), Max(highUpperLower, highUpperUpper));
    return Commit(Enclosure{ .Lower = lower, .Upper = upper }, outEnclosure);
}

MathStatus TrySquareOutward(Enclosure value, Enclosure& outEnclosure) noexcept
{
    const MathStatus status = ValidateOperand(value);
    if (status != MathStatus::Success)
    {
        return status;
    }
    const float64 lowerMagnitude = Abs(value.Lower);
    const float64 upperMagnitude = Abs(value.Upper);
    const bool isZeroInside = value.Lower <= 0.0 && value.Upper >= 0.0;
    const float64 smallest = isZeroInside == true ? 0.0 : Min(lowerMagnitude, upperMagnitude);
    const float64 largest = Max(lowerMagnitude, upperMagnitude);
    // A square is never negative, so the widened lower bound is clamped at zero.
    return Commit(
        Enclosure
        {
            .Lower = Max(0.0, MultiplyDown(smallest, smallest)),
            .Upper = MultiplyUp(largest, largest),
        },
        outEnclosure);
}

MathStatus TrySquareRootOutward(Enclosure value, Enclosure& outEnclosure) noexcept
{
    const MathStatus status = ValidateOperand(value);
    if (status != MathStatus::Success)
    {
        return status;
    }
    if (value.Lower < 0.0)
    {
        return MathStatus::InvalidArgument;
    }
    const float64 lower = value.Lower == 0.0 ? 0.0 : Max(0.0, NextFloat64Down(std::sqrt(value.Lower)));
    const float64 upper = value.Upper == 0.0 ? 0.0 : NextFloat64Up(std::sqrt(value.Upper));
    return Commit(Enclosure{ .Lower = lower, .Upper = upper }, outEnclosure);
}
} // namespace ludus::foundation::math
