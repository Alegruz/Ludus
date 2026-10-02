// Projection factories: reverse-Z depth endpoints, infinite-far, ortho,
// LookAt degeneracy, viewport mapping, infinite-far w=0 direction handling.

#include <ludus/foundation/math/projection.hpp>
#include <ludus/foundation/math/transform.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace ludus::foundation::math;

namespace
{
[[nodiscard]] float Depth(const Matrix4& p, float z)
{
    const Vector4 clip = p * Vector4{0, 0, z, 1};
    Vector3 ndc{};
    (void)TryPerspectiveDivide(clip, 0.0f, ndc);
    return ndc.Z;
}
[[nodiscard]] bool Close(float a, float b, float t = 1e-4f)
{
    return std::fabs(a - b) <= t;
}
} // namespace

TEST_CASE("Finite reverse-Z: near maps to 1, far to 0", "[math][projection]")
{
    Matrix4 p{};
    REQUIRE(TryPerspectiveReverseZ(DegreesToRadians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f, p) == MathStatus::Success);
    REQUIRE(Close(Depth(p, -0.1f), 1.0f));
    REQUIRE(Close(Depth(p, -100.0f), 0.0f));
}

TEST_CASE("Projection input validation", "[math][projection]")
{
    Matrix4 p = Matrix4::Identity();
    const Matrix4 keep = p;
    REQUIRE(TryPerspectiveReverseZ(1.0f, 1.0f, 10.0f, 5.0f, p) == MathStatus::InvalidArgument); // far<near
    REQUIRE(p == keep);
    REQUIRE(TryPerspectiveReverseZ(1.0f, -1.0f, 0.1f, 100.0f, p) == MathStatus::InvalidArgument); // aspect<=0
    REQUIRE(p == keep);
    REQUIRE(TryPerspectiveReverseZ(0.0f, 1.0f, 0.1f, 100.0f, p) == MathStatus::InvalidArgument); // fov 0
    REQUIRE(p == keep);
}

TEST_CASE("Infinite reverse-Z: near maps to 1, far recedes toward 0", "[math][projection]")
{
    Matrix4 p{};
    REQUIRE(TryPerspectiveReverseZInfinite(DegreesToRadians(60.0f), 1.0f, 0.1f, p) == MathStatus::Success);
    REQUIRE(Close(Depth(p, -0.1f), 1.0f));
    REQUIRE(Depth(p, -1000.0f) < 1e-3f);
}

TEST_CASE("Infinite-far homogeneous w=0 is a direction, not divided", "[math][projection]")
{
    Matrix4 p{};
    REQUIRE(TryPerspectiveReverseZInfinite(DegreesToRadians(60.0f), 1.0f, 0.1f, p) == MathStatus::Success);
    // A ray toward the far plane in NDC: the matrix maps the far direction to a
    // w=0 clip vector; perspective divide must refuse to divide it.
    Matrix4 inv{};
    REQUIRE(TryInverse(p, inv) == MathStatus::Success);
    // NDC depth 0 at the center is the far point; unproject it.
    const Vector4 clip{0, 0, 0, 1};
    const Vector4 view = inv * clip;
    // Reconstruct a direction by subtracting the near point rather than dividing
    // a potential w=0: this test asserts the divide guard, so feed w=0 directly.
    Vector3 out{};
    REQUIRE(TryPerspectiveDivide(Vector4{view.X, view.Y, view.Z, 0.0f}, 0.0f, out) == MathStatus::InvalidArgument);
}

TEST_CASE("Orthographic ordering validation", "[math][projection]")
{
    Matrix4 o{};
    REQUIRE(TryOrthographicReverseZ(-1, 1, -1, 1, 0, 10, o) == MathStatus::Success);
    const Matrix4 keep = o;
    REQUIRE(TryOrthographicReverseZ(1, -1, -1, 1, 0, 10, o) == MathStatus::InvalidArgument);
    REQUIRE(o == keep);
}

TEST_CASE("LookAt degeneracy and basic validity", "[math][projection]")
{
    Matrix4 v{};
    REQUIRE(TryLookAt(Vector3{0, 0, 0}, Vector3{0, 0, 0}, Vector3{0, 1, 0}, v) == MathStatus::Degenerate);
    REQUIRE(TryLookAt(Vector3{0, 0, 5}, Vector3{0, 0, 0}, Vector3{0, 0, 1}, v) ==
            MathStatus::Degenerate); // up parallel
    REQUIRE(TryLookAt(Vector3{0, 0, 5}, Vector3{0, 0, 0}, Vector3{0, 1, 0}, v) == MathStatus::Success);
}

TEST_CASE("NDC to framebuffer top-left corners", "[math][projection]")
{
    const Viewport vp{0, 0, 800, 600};
    Vector2 px{};
    REQUIRE(TryNdcToFramebuffer(Vector3{-1, 1, 0}, vp, px) == MathStatus::Success);
    REQUIRE((px.X == 0.0f && px.Y == 0.0f)); // top-left
    REQUIRE(TryNdcToFramebuffer(Vector3{1, -1, 0}, vp, px) == MathStatus::Success);
    REQUIRE((px.X == 800.0f && px.Y == 600.0f)); // bottom-right
    REQUIRE(TryNdcToFramebuffer(Vector3{0, 0, 0}, vp, px) == MathStatus::Success);
    REQUIRE((px.X == 400.0f && px.Y == 300.0f));

    // Round trip.
    Vector2 ndc{};
    REQUIRE(TryFramebufferToNdc(Vector2{400, 300}, vp, ndc) == MathStatus::Success);
    REQUIRE((std::fabs(ndc.X) < 1e-5f && std::fabs(ndc.Y) < 1e-5f));

    // Non-positive viewport rejected.
    Vector2 keep{5, 5};
    px = keep;
    REQUIRE(TryNdcToFramebuffer(Vector3{0, 0, 0}, Viewport{0, 0, 0, 600}, px) == MathStatus::InvalidArgument);
    REQUIRE(px == keep);
}
