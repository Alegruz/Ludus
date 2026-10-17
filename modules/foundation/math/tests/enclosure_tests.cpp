// Outward-rounded enclosures checked against Dekker's exact transformations.

#include "enclosure_oracle.hpp"

#include <ludus/foundation/math/enclosure.hpp>

#include <catch2/catch_test_macros.hpp>

#include <limits>

using namespace ludus::foundation::math;
using ludus::foundation::core::float64;
using ludus::foundation::core::int32;

namespace
{
constexpr float64 LARGEST = std::numeric_limits<float64>::max();
constexpr float64 SMALLEST_SUBNORMAL = std::numeric_limits<float64>::denorm_min();
constexpr int32 CORPUS_CASE_COUNT = 200000;
constexpr ludus::foundation::core::uint64 CORPUS_SEED = 0x1D0C5EEDULL;
} // namespace

TEST_CASE("Outward enclosures contain every exact sum, product and root in the corpus", "[math][enclosure]")
{
    REQUIRE(enclosure_oracle::RunCorpus(CORPUS_SEED, CORPUS_CASE_COUNT) == 0);
}

TEST_CASE("Exact float64 sums are not widened and inexact ones widen one side", "[math][enclosure]")
{
    Enclosure result;
    REQUIRE(TryAddOutward(MakePointEnclosure(1.0), MakePointEnclosure(2.0), result) == MathStatus::Success);
    REQUIRE((result.Lower == 3.0 && result.Upper == 3.0));

    // 0.1 + 0.2 rounds up to 0.30000000000000004, so only the lower bound moves.
    REQUIRE(TryAddOutward(MakePointEnclosure(0.1), MakePointEnclosure(0.2), result) == MathStatus::Success);
    REQUIRE(result.Upper == 0.1 + 0.2);
    REQUIRE(result.Lower == NextFloat64Down(0.1 + 0.2));
    REQUIRE(enclosure_oracle::Contains(result, enclosure_oracle::AddExactly(0.1, 0.2)) == true);

    REQUIRE(TrySubtractOutward(MakePointEnclosure(1.0), MakePointEnclosure(1.0), result) == MathStatus::Success);
    REQUIRE((result.Lower == 0.0 && result.Upper == 0.0));
}

TEST_CASE("Products and roots never claim exactness except at zero", "[math][enclosure]")
{
    Enclosure result;
    REQUIRE(TryMultiplyOutward(MakePointEnclosure(3.0), MakePointEnclosure(0.5), result) == MathStatus::Success);
    REQUIRE(result.Lower == NextFloat64Down(1.5));
    REQUIRE(result.Upper == NextFloat64Up(1.5));
    REQUIRE(TryMultiplyOutward(MakePointEnclosure(0.0), Enclosure{ .Lower = -2.0, .Upper = 5.0 }, result) ==
            MathStatus::Success);
    REQUIRE((result.Lower == 0.0 && result.Upper == 0.0));

    REQUIRE(TrySquareRootOutward(MakePointEnclosure(4.0), result) == MathStatus::Success);
    REQUIRE((result.Lower < 2.0 && result.Upper > 2.0));
    REQUIRE(TrySquareRootOutward(Enclosure{ .Lower = 0.0, .Upper = 0.0 }, result) == MathStatus::Success);
    REQUIRE((result.Lower == 0.0 && result.Upper == 0.0));
    // A root of a tiny positive lower bound never goes negative.
    REQUIRE(TrySquareRootOutward(
                Enclosure
                {
                    .Lower = SMALLEST_SUBNORMAL,
                    .Upper = 1.0,
                },
                result) == MathStatus::Success);
    REQUIRE(result.Lower >= 0.0);
}

TEST_CASE("Interval products and squares follow the sign cases", "[math][enclosure]")
{
    Enclosure result;
    REQUIRE(TryMultiplyOutward(Enclosure{ .Lower = -2.0, .Upper = 3.0 },
                               Enclosure{ .Lower = -5.0, .Upper = 4.0 },
                               result) == MathStatus::Success);
    REQUIRE((result.Lower <= -15.0 && result.Lower >= NextFloat64Down(-15.0)));
    REQUIRE((result.Upper >= 12.0 && result.Upper <= NextFloat64Up(12.0)));

    REQUIRE(TrySquareOutward(Enclosure{ .Lower = -3.0, .Upper = 2.0 }, result) == MathStatus::Success);
    REQUIRE(result.Lower == 0.0);
    REQUIRE((result.Upper >= 9.0 && result.Upper <= NextFloat64Up(9.0)));
    REQUIRE(TrySquareOutward(Enclosure{ .Lower = -3.0, .Upper = -2.0 }, result) == MathStatus::Success);
    REQUIRE((result.Lower <= 4.0 && result.Lower >= NextFloat64Down(4.0)));
}

TEST_CASE("Invalid enclosures and overflow fail without writing", "[math][enclosure]")
{
    const Enclosure sentinel{ .Lower = 7.0, .Upper = 8.0 };
    Enclosure result = sentinel;
    const float64 notANumber = std::numeric_limits<float64>::quiet_NaN();
    const float64 infinity = std::numeric_limits<float64>::infinity();

    REQUIRE(TryAddOutward(MakePointEnclosure(notANumber), MakePointEnclosure(1.0), result) ==
            MathStatus::NonFiniteInput);
    REQUIRE(TryMultiplyOutward(MakePointEnclosure(1.0), Enclosure{ .Lower = 0.0, .Upper = infinity }, result) ==
            MathStatus::NonFiniteInput);
    REQUIRE(TrySubtractOutward(Enclosure{ .Lower = 2.0, .Upper = 1.0 }, MakePointEnclosure(1.0), result) ==
            MathStatus::InvalidArgument);
    REQUIRE(TrySquareRootOutward(Enclosure{ .Lower = -1.0, .Upper = 1.0 }, result) == MathStatus::InvalidArgument);
    REQUIRE(TryAddOutward(MakePointEnclosure(LARGEST), MakePointEnclosure(LARGEST), result) == MathStatus::OutOfRange);
    REQUIRE(TryMultiplyOutward(MakePointEnclosure(LARGEST), MakePointEnclosure(2.0), result) == MathStatus::OutOfRange);
    // The largest finite product is widened past the finite range and is rejected rather than truncated.
    REQUIRE(TrySquareOutward(MakePointEnclosure(LARGEST), result) == MathStatus::OutOfRange);
    REQUIRE((result.Lower == sentinel.Lower && result.Upper == sentinel.Upper));

    REQUIRE(IsValidEnclosure(Enclosure{ .Lower = 1.0, .Upper = 1.0 }) == true);
    REQUIRE(IsValidEnclosure(Enclosure{ .Lower = 1.0, .Upper = 0.0 }) == false);
    REQUIRE(NextFloat64Up(1.0) > 1.0);
    REQUIRE(NextFloat64Down(0.0) == -SMALLEST_SUBNORMAL);
}
