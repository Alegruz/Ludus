#pragma once

// Independent oracle for the outward-rounded enclosures. Shared by the native
// unit tests and the WebAssembly math probe so both pinned toolchains run the
// same deterministic corpus. The oracle never calls the production rounding
// helpers: exact sums and products are reconstructed with Dekker's error-free
// transformations and compared against each returned bound without rounding.
//
// Thanks to T. J. Dekker, "A floating-point technique for extending the
// available precision", Numerische Mathematik 18 (1971), pages 224-242: the
// Fast2Sum and Veltkamp-split product (Mul12) transformations are used as the
// exact reference. They require round to nearest and no overflow/underflow, so
// operands stay within [2^-200, 2^200] in magnitude.

#include <ludus/foundation/base/types.h>

#include <ludus/foundation/math/enclosure.hpp>
#include <ludus/foundation/math/status.hpp>

#include <cmath>

namespace ludus::foundation::math::enclosure_oracle
{
using core::float64;
using core::int32;
using core::uint64;

/// Exact real value Head + Tail, represented without rounding.
struct ExactValue final
{
    float64 Head = 0.0;
    float64 Tail = 0.0;
};

/// Dekker Fast2Sum after ordering by magnitude: Head + Tail == a + b exactly.
[[nodiscard]] inline ExactValue AddExactly(float64 a, float64 b) noexcept
{
    const float64 larger = std::fabs(a) >= std::fabs(b) ? a : b;
    const float64 smaller = std::fabs(a) >= std::fabs(b) ? b : a;
    const float64 head = larger + smaller;
    return ExactValue{ .Head = head, .Tail = smaller - (head - larger) };
}

/// Dekker Mul12 with Veltkamp splitting: Head + Tail == a * b exactly.
[[nodiscard]] inline ExactValue MultiplyExactly(float64 a, float64 b) noexcept
{
    constexpr float64 SPLIT_FACTOR = 134217729.0; // 2^27 + 1 for binary64.
    const float64 scaledA = SPLIT_FACTOR * a;
    const float64 highA = scaledA - (scaledA - a);
    const float64 lowA = a - highA;
    const float64 scaledB = SPLIT_FACTOR * b;
    const float64 highB = scaledB - (scaledB - b);
    const float64 lowB = b - highB;
    const float64 head = a * b;
    const float64 tail = (((highA * highB - head) + highA * lowB) + lowA * highB) + lowA * lowB;
    return ExactValue{ .Head = head, .Tail = tail };
}

/// Whether bound <= Head + Tail, where Head is the rounded value of the exact sum.
[[nodiscard]] inline bool IsLowerBoundOf(float64 bound, ExactValue exact) noexcept
{
    if (bound < exact.Head)
    {
        // bound is at most the predecessor of Head, and the exact value rounds to Head, so it lies above.
        return true;
    }
    if (bound == exact.Head)
    {
        return exact.Tail >= 0.0;
    }
    // bound - Head is exact for adjacent floats; otherwise it exceeds every rounding error of Head.
    return exact.Tail > 0.0 && bound - exact.Head <= exact.Tail;
}

/// Whether bound >= Head + Tail, where Head is the rounded value of the exact sum.
[[nodiscard]] inline bool IsUpperBoundOf(float64 bound, ExactValue exact) noexcept
{
    return IsLowerBoundOf(-bound, ExactValue{ .Head = -exact.Head, .Tail = -exact.Tail });
}

/// Whether enclosure contains Head + Tail.
[[nodiscard]] inline bool Contains(Enclosure enclosure, ExactValue exact) noexcept
{
    return IsLowerBoundOf(enclosure.Lower, exact) == true && IsUpperBoundOf(enclosure.Upper, exact) == true;
}

/// Whether both bounds are within one float64 unit of the rounded value.
[[nodiscard]] inline bool IsWithinOneUnit(Enclosure enclosure, float64 rounded) noexcept
{
    return enclosure.Lower >= std::nextafter(rounded, -INFINITY) &&
           enclosure.Upper <= std::nextafter(rounded, INFINITY);
}

/// SplitMix64 step (Steele, Lea and Flood, OOPSLA 2014), used only to make a deterministic corpus.
[[nodiscard]] inline uint64 NextRandom(uint64& inoutState) noexcept
{
    inoutState += 0x9E3779B97F4A7C15ULL;
    uint64 mixed = inoutState;
    mixed = (mixed ^ (mixed >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    mixed = (mixed ^ (mixed >> 27U)) * 0x94D049BB133111EBULL;
    return mixed ^ (mixed >> 31U);
}

/// Random finite value with a full random significand and magnitude in [2^-200, 2^200).
[[nodiscard]] inline float64 MakeRandomValue(uint64& inoutState) noexcept
{
    constexpr uint64 SIGNIFICAND_BITS = 52;
    constexpr int32 EXPONENT_SPAN = 400;
    const uint64 bits = NextRandom(inoutState);
    const float64 significand =
        1.0 + static_cast<float64>(bits & ((1ULL << SIGNIFICAND_BITS) - 1ULL)) / static_cast<float64>(1ULL << 52U);
    const int32 exponent = static_cast<int32>((bits >> SIGNIFICAND_BITS) % EXPONENT_SPAN) - EXPONENT_SPAN / 2;
    const float64 magnitude = std::ldexp(significand, exponent);
    return (bits >> 63U) != 0U ? -magnitude : magnitude;
}

/// Run caseCount deterministic cases; returns the number of failed checks.
///
/// Every case checks point sums, differences, products and square roots against
/// the exact reference, the one-unit tightness of point results, and a random
/// interval product against all four exact endpoint products. Half of the cases
/// draw the second operand near the first to exercise cancellation.
[[nodiscard]] inline int32
RunCorpus(uint64 seed, // NOLINT(bugprone-easily-swappable-parameters): seed and count differ in meaning and type
          int32 caseCount) noexcept
{
    uint64 state = seed;
    int32 failures = 0;
    for (int32 i = 0; i < caseCount; ++i)
    {
        const float64 a = MakeRandomValue(state);
        float64 b = MakeRandomValue(state);
        if ((i & 1) == 0)
        {
            b = -a * (1.0 + std::ldexp(static_cast<float64>(NextRandom(state) % 1024U), -40));
        }
        const Enclosure pointA = MakePointEnclosure(a);
        const Enclosure pointB = MakePointEnclosure(b);
        Enclosure result;

        const bool isAddOk = TryAddOutward(pointA, pointB, result) == MathStatus::Success &&
                             Contains(result, AddExactly(a, b)) == true && IsWithinOneUnit(result, a + b) == true;
        failures += isAddOk == true ? 0 : 1;

        const bool isSubtractOk = TrySubtractOutward(pointA, pointB, result) == MathStatus::Success &&
                                  Contains(result, AddExactly(a, -b)) == true && IsWithinOneUnit(result, a - b) == true;
        failures += isSubtractOk == true ? 0 : 1;

        const bool isMultiplyOk = TryMultiplyOutward(pointA, pointB, result) == MathStatus::Success &&
                                  Contains(result, MultiplyExactly(a, b)) == true &&
                                  IsWithinOneUnit(result, a * b) == true;
        failures += isMultiplyOk == true ? 0 : 1;

        const float64 radicand = std::fabs(a);
        bool isRootOk = TrySquareRootOutward(MakePointEnclosure(radicand), result) == MathStatus::Success;
        if (isRootOk == true)
        {
            // Lower^2 <= radicand <= Upper^2 with exact squares.
            isRootOk = IsUpperBoundOf(radicand, MultiplyExactly(result.Lower, result.Lower)) == true &&
                       IsLowerBoundOf(radicand, MultiplyExactly(result.Upper, result.Upper)) == true;
        }
        failures += isRootOk == true ? 0 : 1;

        const float64 widthA = std::fabs(a) * std::ldexp(static_cast<float64>(NextRandom(state) % 4096U), -12);
        const float64 widthB = std::fabs(b) * std::ldexp(static_cast<float64>(NextRandom(state) % 4096U), -12);
        const Enclosure rangeA{ .Lower = a - widthA, .Upper = a };
        const Enclosure rangeB{ .Lower = b, .Upper = b + widthB };
        bool isRangeOk = TryMultiplyOutward(rangeA, rangeB, result) == MathStatus::Success;
        if (isRangeOk == true)
        {
            isRangeOk = Contains(result, MultiplyExactly(rangeA.Lower, rangeB.Lower)) == true &&
                        Contains(result, MultiplyExactly(rangeA.Lower, rangeB.Upper)) == true &&
                        Contains(result, MultiplyExactly(rangeA.Upper, rangeB.Lower)) == true &&
                        Contains(result, MultiplyExactly(rangeA.Upper, rangeB.Upper)) == true;
        }
        failures += isRangeOk == true ? 0 : 1;
    }
    return failures;
}
} // namespace ludus::foundation::math::enclosure_oracle
