// Geometry value semantics: empty vs degenerate boxes, finiteness, interval
// validity, default values.

#include <ludus/foundation/math/geometry.hpp>

#include <catch2/catch_test_macros.hpp>

#include <limits>

using namespace ludus::foundation::math;

TEST_CASE("Empty box differs from a zero-size box at origin", "[math][geometry]")
{
    const Aabb3 empty = EmptyAabb3();
    REQUIRE(IsEmpty(empty));
    REQUIRE(IsFinite(empty)); // sentinel is finite, just inverted

    const Aabb3 zeroSize{Vector3{0, 0, 0}, Vector3{0, 0, 0}};
    REQUIRE_FALSE(IsEmpty(zeroSize)); // a valid degenerate point box
}

TEST_CASE("Non-finite box field is invalid, not empty", "[math][geometry]")
{
    const float inf = std::numeric_limits<float>::infinity();
    const Aabb3 bad{Vector3{0, 0, 0}, Vector3{inf, 1, 1}};
    REQUIRE_FALSE(IsFinite(bad));
    // min <= max still holds componentwise, so it is "not empty" by the min>max
    // rule; callers treat non-finite as invalid separately (queries do).
    REQUIRE_FALSE(IsEmpty(bad));
}

TEST_CASE("2D empty/degenerate parity with 3D", "[math][geometry]")
{
    const Aabb2 empty = EmptyAabb2();
    REQUIRE(IsEmpty(empty));
    const Aabb2 zero{Vector2{0, 0}, Vector2{0, 0}};
    REQUIRE_FALSE(IsEmpty(zero));
}

TEST_CASE("Default values", "[math][geometry]")
{
    const Sphere s{};
    REQUIRE(s.Radius == 0.0f); // zero radius is valid
    const Frustum f{};
    REQUIRE(f.ActiveMask == 0x3f);
    REQUIRE(f.Mode == FrustumMode::FinitePerspectiveOrOrthographic);
}
