// Large-coordinate relative conversion: 1e9 origin preserves sub-meter offset,
// range rejection, and the lost-detail demonstration of wrong-order narrowing.

#include <ludus/foundation/math/precision.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

using namespace ludus::foundation::math;

TEST_CASE("Double subtract before narrow preserves local detail at 1e9", "[math][precision]")
{
    const Vector3d position{1000000000.5, 0.0, 0.0};
    const Vector3d origin{1000000000.0, 0.0, 0.0};
    Vector3 rel{};
    REQUIRE(TryMakeRelative(position, origin, 10.0, rel) == MathStatus::Success);
    REQUIRE(std::fabs(rel.X - 0.5f) < 1e-4f);
}

TEST_CASE("Wrong-order narrowing loses the detail this call preserves", "[math][precision]")
{
    // Narrowing to float FIRST then subtracting loses the 0.5 m offset entirely.
    const float naive = static_cast<float>(1000000000.5) - static_cast<float>(1000000000.0);
    REQUIRE(naive != 0.5f); // detail lost
}

TEST_CASE("Range and validation", "[math][precision]")
{
    Vector3 keep{7, 7, 7};
    Vector3 rel = keep;
    REQUIRE(TryMakeRelative(Vector3d{100, 0, 0}, Vector3d{0, 0, 0}, 10.0, rel) == MathStatus::OutOfRange);
    REQUIRE(rel == keep);
    REQUIRE(TryMakeRelative(Vector3d{1, 0, 0}, Vector3d{0, 0, 0}, 0.0, rel) == MathStatus::InvalidArgument);
    REQUIRE(rel == keep);
    const double inf = std::numeric_limits<double>::infinity();
    REQUIRE(TryMakeRelative(Vector3d{inf, 0, 0}, Vector3d{0, 0, 0}, 10.0, rel) == MathStatus::NonFiniteInput);
    REQUIRE(rel == keep);
}
