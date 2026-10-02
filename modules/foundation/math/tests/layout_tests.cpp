// Value layout contract (design §3): sizes, natural alignment, standard-layout,
// trivial copyability, and default initialisation.

#include <ludus/foundation/math/geometry.hpp>
#include <ludus/foundation/math/matrix.hpp>
#include <ludus/foundation/math/quaternion.hpp>
#include <ludus/foundation/math/transform.hpp>
#include <ludus/foundation/math/vector.hpp>

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <type_traits>

using namespace ludus::foundation::math;

TEST_CASE("Vector sizes and alignment", "[math][layout]")
{
    STATIC_REQUIRE(sizeof(Vector2) == 8);
    STATIC_REQUIRE(sizeof(Vector3) == 12);
    STATIC_REQUIRE(sizeof(Vector4) == 16);
    STATIC_REQUIRE(sizeof(Vector3d) == 24);
    STATIC_REQUIRE(alignof(Vector2) == alignof(float));
    STATIC_REQUIRE(alignof(Vector3) == alignof(float));
    STATIC_REQUIRE(alignof(Vector4) == alignof(float));
    STATIC_REQUIRE(alignof(Vector3d) == alignof(double));
    STATIC_REQUIRE(std::is_standard_layout_v<Vector3>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<Vector4>);
}

TEST_CASE("Quaternion / matrix / affine / TRS layout", "[math][layout]")
{
    STATIC_REQUIRE(sizeof(Quaternion) == 16);
    STATIC_REQUIRE(sizeof(Matrix3) == 36);
    STATIC_REQUIRE(sizeof(Matrix4) == 64);
    STATIC_REQUIRE(sizeof(Affine2) == 24);
    STATIC_REQUIRE(sizeof(Affine3) == 48);
    STATIC_REQUIRE(sizeof(TransformTRS) == 40);
    STATIC_REQUIRE(std::is_trivially_copyable_v<Matrix4>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<Affine3>);
    STATIC_REQUIRE(std::is_standard_layout_v<TransformTRS>);
}

TEST_CASE("Default initialisation", "[math][layout]")
{
    Vector3 v{};
    REQUIRE((v.X == 0.0f && v.Y == 0.0f && v.Z == 0.0f));
    Quaternion q{};
    REQUIRE((q.X == 0.0f && q.Y == 0.0f && q.Z == 0.0f && q.W == 1.0f));
    Matrix3 m{};
    REQUIRE(m == Matrix3::Zero());
    Affine3 a{};
    REQUIRE(a == Affine3::Identity());
    TransformTRS trs{};
    REQUIRE((trs.Scale.X == 1.0f && trs.Rotation.W == 1.0f && trs.Translation.X == 0.0f));
}

TEST_CASE("Exact equality: signed zero and NaN", "[math][layout]")
{
    REQUIRE((Vector3{0.0f, 0.0f, 0.0f} == Vector3{-0.0f, -0.0f, -0.0f})); // +0 == -0
    const float nan = std::numeric_limits<float>::quiet_NaN();
    REQUIRE_FALSE((Vector3{nan, 0, 0} == Vector3{nan, 0, 0})); // NaN != NaN
}
