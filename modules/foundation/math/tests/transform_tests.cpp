// Affine action/compose/inverse, normal transform, TRS shear retention,
// Matrix4 export and perspective divide.

#include <ludus/foundation/math/quaternion.hpp>
#include <ludus/foundation/math/transform.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace ludus::foundation::math;

namespace
{
[[nodiscard]] bool Close(float a, float b, float t = 1e-5f)
{
    return std::fabs(a - b) <= t;
}
[[nodiscard]] bool CloseVec(Vector3 a, Vector3 b, float t = 1e-5f)
{
    return Close(a.X, b.X, t) && Close(a.Y, b.Y, t) && Close(a.Z, b.Z, t);
}
} // namespace

TEST_CASE("Affine3 transform point vs vector", "[math][transform]")
{
    Affine3 a = Affine3::Identity();
    a.Translation = Vector3{10, 0, 0};
    REQUIRE(CloseVec(TransformPoint(a, Vector3{1, 2, 3}), Vector3{11, 2, 3}));
    REQUIRE(CloseVec(TransformVector(a, Vector3{1, 2, 3}), Vector3{1, 2, 3})); // no translation
}

TEST_CASE("Affine3 compose order and inverse round-trip", "[math][transform]")
{
    Quaternion q{};
    REQUIRE(TryFromAxisAngle(Vector3{0, 0, 1}, kHalfPiF, q) == MathStatus::Success);
    TransformTRS r;
    r.Rotation = q;
    const Affine3 rot = ToAffine3(r);
    Affine3 trans = Affine3::Identity();
    trans.Translation = Vector3{5, 0, 0};
    const Affine3 composed = Compose(trans, rot); // apply rot, then translate
    const Vector3 p = TransformPoint(composed, Vector3{1, 0, 0});
    REQUIRE(CloseVec(p, Vector3{5, 1, 0}, 1e-5f));

    Affine3 inv{};
    REQUIRE(TryInverse(composed, inv) == MathStatus::Success);
    REQUIRE(CloseVec(TransformPoint(inv, p), Vector3{1, 0, 0}, 1e-4f));
}

TEST_CASE("Non-uniform parent scale plus rotated child retains shear", "[math][transform]")
{
    TransformTRS parent;
    parent.Scale = Vector3{2, 1, 1};
    TransformTRS child;
    REQUIRE(TryFromAxisAngle(Vector3{0, 0, 1}, kHalfPiF * 0.5f, child.Rotation) == MathStatus::Success);
    const Affine3 world = Compose(ToAffine3(parent), ToAffine3(child));
    // Columns are no longer orthogonal: shear is present (not silently removed).
    const float d = Dot(world.Columns[0], world.Columns[1]);
    REQUIRE(std::fabs(d) > 1e-3f);
}

TEST_CASE("Normal transform stays perpendicular to transformed tangent", "[math][transform]")
{
    TransformTRS s;
    s.Scale = Vector3{2, 1, 1};
    const Affine3 a = ToAffine3(s);
    Vector3 n{};
    REQUIRE(TryTransformNormal(a, Vector3{0, 1, 0}, n) == MathStatus::Success);
    const Vector3 tangent = TransformVector(a, Vector3{1, 0, 0});
    REQUIRE(std::fabs(Dot(n, tangent)) < 1e-5f);
    REQUIRE(std::fabs(Length(n) - 1.0f) < 2e-6f);

    // Zero input normal fails.
    Vector3 keep{7, 7, 7};
    Vector3 out = keep;
    REQUIRE(TryTransformNormal(a, Vector3{0, 0, 0}, out) == MathStatus::Degenerate);
    REQUIRE(out == keep);
}

TEST_CASE("Singular transform fails inverse and normal, output unchanged", "[math][transform]")
{
    Affine3 singular = Affine3::Identity();
    singular.Columns[0] = Vector3{0, 0, 0}; // collapse X axis
    Affine3 out = Affine3::Identity();
    const Affine3 keep = out;
    REQUIRE(TryInverse(singular, out) != MathStatus::Success);
    REQUIRE(out == keep);
    Matrix3 nm = Matrix3::Identity();
    const Matrix3 keepM = nm;
    REQUIRE(TryNormalMatrix(singular, InversePolicy{}, nm) != MathStatus::Success);
    REQUIRE(nm == keepM);
}

TEST_CASE("ToMatrix4 writes implicit last row; perspective divide is checked", "[math][transform]")
{
    Affine3 a = Affine3::Identity();
    a.Translation = Vector3{1, 2, 3};
    const Matrix4 m = ToMatrix4(a);
    REQUIRE(m.At(3, 3) == 1.0f);
    REQUIRE(m.At(3, 0) == 0.0f);
    REQUIRE(m.At(0, 3) == 1.0f); // translation in last column

    Vector3 ndc{};
    REQUIRE(TryPerspectiveDivide(Vector4{2, 4, 6, 2}, 0.0f, ndc) == MathStatus::Success);
    REQUIRE(CloseVec(ndc, Vector3{1, 2, 3}));
    Vector3 keep{9, 9, 9};
    Vector3 out = keep;
    REQUIRE(TryPerspectiveDivide(Vector4{1, 1, 1, 0}, 0.0f, out) == MathStatus::InvalidArgument);
    REQUIRE(out == keep);
}

TEST_CASE("TryToAffine3 validates unit rotation", "[math][transform]")
{
    TransformTRS trs;
    trs.Rotation = Quaternion{0, 0, 0, 2}; // not unit
    Affine3 out = Affine3::Identity();
    const Affine3 keep = out;
    REQUIRE(TryToAffine3(trs, 1e-4f, out) == MathStatus::Degenerate);
    REQUIRE(out == keep);
    trs.Rotation = QuaternionIdentity();
    REQUIRE(TryToAffine3(trs, 1e-4f, out) == MathStatus::Success);
}
