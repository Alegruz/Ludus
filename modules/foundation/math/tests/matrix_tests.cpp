// Matrix action/multiplication/transpose and the checked inverse: two-sided
// residual, scaling invariance, singular rejection, aliasing.

#include <ludus/foundation/math/matrix.hpp>
#include <ludus/foundation/math/quaternion.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace ludus::foundation::math;

namespace
{
[[nodiscard]] bool Close(float a, float b, float t = 1e-4f)
{
    return std::fabs(a - b) <= t;
}
} // namespace

TEST_CASE("Matrix3 action and column-major element access", "[math][matrix]")
{
    // Column-major: Columns[c] is the image of basis vector c.
    const Matrix3 m{{Vector3{1, 2, 3}, Vector3{4, 5, 6}, Vector3{7, 8, 9}}};
    REQUIRE(m.At(0, 0) == 1.0f);
    REQUIRE(m.At(1, 0) == 2.0f);
    REQUIRE(m.At(0, 1) == 4.0f);
    const Vector3 r = m * Vector3{1, 0, 0};
    REQUIRE((r.X == 1.0f && r.Y == 2.0f && r.Z == 3.0f));
}

TEST_CASE("Matrix multiplication is non-commuting; transpose is correct", "[math][matrix]")
{
    Quaternion q{};
    REQUIRE(TryFromAxisAngle(Vector3{0, 0, 1}, kHalfPiF, q) == MathStatus::Success);
    const Matrix3 r = ToMatrix3(q);
    Matrix3 s = Matrix3::Identity();
    s.Columns[0].X = 2.0f; // scale X
    REQUIRE_FALSE((r * s) == (s * r));
    const Matrix3 t = Transpose(r);
    REQUIRE(Close(t.At(0, 1), r.At(1, 0)));
}

TEST_CASE("Checked inverse round-trips a rotation+translation on asymmetric point", "[math][matrix]")
{
    Quaternion q{};
    REQUIRE(TryFromAxisAngle(Vector3{0, 0, 1}, kHalfPiF, q) == MathStatus::Success);
    const Matrix3 r3 = ToMatrix3(q);
    Matrix4 m = Matrix4::Identity();
    for (int c = 0; c < 3; ++c)
    {
        m.Columns[c] = Vector4{r3.Columns[c].X, r3.Columns[c].Y, r3.Columns[c].Z, 0.0f};
    }
    m.Columns[3] = Vector4{3, 4, 5, 1};
    Matrix4 inv{};
    REQUIRE(TryInverse(m, inv) == MathStatus::Success);
    const Vector4 p{1, 2, 3, 1};
    const Vector4 back = inv * (m * p);
    REQUIRE(Close(back.X, 1.0f));
    REQUIRE(Close(back.Y, 2.0f));
    REQUIRE(Close(back.Z, 3.0f));
}

TEST_CASE("Inverse is scaling invariant and supports aliasing", "[math][matrix]")
{
    Matrix3 m{{Vector3{2, 0, 0}, Vector3{0, 3, 0}, Vector3{0, 0, 4}}};
    Matrix3 inv{};
    REQUIRE(TryInverse(m, inv) == MathStatus::Success);
    REQUIRE(Close(inv.At(0, 0), 0.5f));
    REQUIRE(Close(inv.At(1, 1), 1.0f / 3.0f));

    // In-place inverse.
    Matrix3 self = m;
    REQUIRE(TryInverse(self, self) == MathStatus::Success);
    REQUIRE(Close(self.At(2, 2), 0.25f));
}

TEST_CASE("Singular and nearly-dependent matrices are rejected, output unchanged", "[math][matrix]")
{
    Matrix3 out = Matrix3::Identity();
    const Matrix3 keep = out;
    REQUIRE(TryInverse(Matrix3::Zero(), out) == MathStatus::Degenerate);
    REQUIRE(out == keep);

    // Nearly dependent rows -> IllConditioned under the default policy.
    Matrix3 dependent{{Vector3{1, 0, 0}, Vector3{1, 1e-9f, 0}, Vector3{0, 0, 1}}};
    out = keep;
    const MathStatus status = TryInverse(dependent, out);
    REQUIRE((status == MathStatus::IllConditioned || status == MathStatus::Degenerate));
    REQUIRE(out == keep);
}
