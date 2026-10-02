// Quaternion construction, action agreement, q/-q equivalence, rotation-between
// (equal/opposite/near-opposite), matrix conversion, interpolation endpoints.

#include <ludus/foundation/math/matrix.hpp>
#include <ludus/foundation/math/quaternion.hpp>

#include "math_oracle.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace ludus::foundation::math;
namespace mt = ludus::math_test;

namespace
{
[[nodiscard]] bool CloseVec(Vector3 a, Vector3 b, float t = 1e-5f)
{
    return std::fabs(a.X - b.X) <= t && std::fabs(a.Y - b.Y) <= t && std::fabs(a.Z - b.Z) <= t;
}
} // namespace

TEST_CASE("Axis-angle +90 about Z maps X to Y, matches oracle", "[math][quaternion]")
{
    Quaternion q{};
    REQUIRE(TryFromAxisAngle(Vector3{0, 0, 1}, kHalfPiF, q) == MathStatus::Success);
    const Vector3 r = Rotate(q, Vector3{1, 0, 0});
    REQUIRE(CloseVec(r, Vector3{0, 1, 0}));
    REQUIRE(CloseVec(r, mt::RotateReference(q, Vector3{1, 0, 0})));
    REQUIRE(std::fabs(mt::QuatLen(q) - 1.0) < 2e-6);
}

TEST_CASE("Rotate agrees with Matrix3(q)*v on an asymmetric vector", "[math][quaternion]")
{
    Quaternion q{};
    REQUIRE(TryFromAxisAngle(Vector3{0.3f, 0.6f, 0.2f}, 1.1f, q) == MathStatus::Success);
    const Vector3 v{1.0f, -2.0f, 3.0f};
    const Vector3 byQuat = Rotate(q, v);
    const Vector3 byMatrix = ToMatrix3(q) * v;
    REQUIRE(CloseVec(byQuat, byMatrix, 1e-5f));
}

TEST_CASE("q and -q are the same rotation", "[math][quaternion]")
{
    Quaternion q{};
    REQUIRE(TryFromAxisAngle(Vector3{0, 1, 0}, 1.0f, q) == MathStatus::Success);
    const Quaternion nq{-q.X, -q.Y, -q.Z, -q.W};
    REQUIRE(SameRotation(q, nq, 1e-5f));
    REQUIRE_FALSE(q == nq); // componentwise equality differs
}

TEST_CASE("TryFromAxisAngle rejects zero axis / non-finite", "[math][quaternion]")
{
    Quaternion q = QuaternionIdentity();
    const Quaternion keep = q;
    REQUIRE(TryFromAxisAngle(Vector3{0, 0, 0}, 1.0f, q) == MathStatus::Degenerate);
    REQUIRE(q == keep);
    REQUIRE(TryFromAxisAngle(Vector3{1, 0, 0}, std::numeric_limits<float>::infinity(), q) ==
            MathStatus::NonFiniteInput);
    REQUIRE(q == keep);
}

TEST_CASE("TryRotationBetween: equal, opposite, near-opposite", "[math][quaternion]")
{
    Quaternion q{};
    // Equal directions -> identity.
    REQUIRE(TryRotationBetween(Vector3{0, 1, 0}, Vector3{0, 1, 0}, q) == MathStatus::Success);
    REQUIRE(SameRotation(q, QuaternionIdentity(), 1e-5f));

    // Opposite directions: 180-degree rotation that maps from to to.
    REQUIRE(TryRotationBetween(Vector3{1, 0, 0}, Vector3{-1, 0, 0}, q) == MathStatus::Success);
    REQUIRE(CloseVec(Rotate(q, Vector3{1, 0, 0}), Vector3{-1, 0, 0}, 1e-4f));

    // Near-opposite: must still map `from` to `to` (not snap to a generic axis).
    const Vector3 from{1, 0, 0};
    const Vector3 to{-1, 1e-3f, 0};
    REQUIRE(TryRotationBetween(from, to, q) == MathStatus::Success);
    Vector3 toUnit{};
    REQUIRE(TryNormalize(to, toUnit) == MathStatus::Success);
    REQUIRE(mt::AngleBetween(Rotate(q, from), toUnit) < 1e-3);

    // Zero direction fails.
    const Quaternion keep = q;
    REQUIRE(TryRotationBetween(Vector3{0, 0, 0}, Vector3{1, 0, 0}, q) == MathStatus::Degenerate);
    REQUIRE(q == keep);
}

TEST_CASE("Matrix<->quaternion round trip and non-rotation rejection", "[math][quaternion]")
{
    Quaternion q{};
    REQUIRE(TryFromAxisAngle(Vector3{0.2f, 0.5f, 0.84f}, 2.9f, q) == MathStatus::Success); // near 180
    const Matrix3 m = ToMatrix3(q);
    Quaternion back{};
    REQUIRE(TryToRotation(m, back) == MathStatus::Success);
    REQUIRE(SameRotation(q, back, 1e-4f));

    // Scaled basis rejected.
    Matrix3 scaled = m;
    scaled.Columns[0] = scaled.Columns[0] * 2.0f;
    REQUIRE(TryToRotation(scaled, back) == MathStatus::Degenerate);
    // Reflection rejected.
    Matrix3 refl = Matrix3::Identity();
    refl.Columns[0].X = -1.0f;
    REQUIRE(TryToRotation(refl, back) == MathStatus::Degenerate);
}

TEST_CASE("Nlerp/Slerp endpoints and hemisphere correction", "[math][quaternion]")
{
    const Quaternion a = QuaternionIdentity();
    Quaternion b{};
    REQUIRE(TryFromAxisAngle(Vector3{0, 0, 1}, 1.5f, b) == MathStatus::Success);
    REQUIRE(SameRotation(SlerpShortest(a, b, 0.0f), a, 1e-5f));
    REQUIRE(SameRotation(SlerpShortest(a, b, 1.0f), b, 1e-5f));
    REQUIRE(SameRotation(NlerpShortest(a, b, 0.0f), a, 1e-5f));
    REQUIRE(SameRotation(NlerpShortest(a, b, 1.0f), b, 1e-5f));

    // Opposite-sign b: still interpolates the shorter path (same rotations).
    const Quaternion nb{-b.X, -b.Y, -b.Z, -b.W};
    REQUIRE(SameRotation(SlerpShortest(a, nb, 1.0f), b, 1e-4f));

    // Slerp midpoint equals the half-angle rotation within tolerance.
    Quaternion half{};
    REQUIRE(TryFromAxisAngle(Vector3{0, 0, 1}, 0.75f, half) == MathStatus::Success);
    REQUIRE(SameRotation(SlerpShortest(a, b, 0.5f), half, 1e-4f));
}
