// Geometric queries: plane distance/reflection, conservative AABB transform,
// ray/AABB, ray/sphere, closest segment points, frustum extraction and
// conservative classification against clip inequalities.

#include <ludus/foundation/math/projection.hpp>
#include <ludus/foundation/math/queries.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

using namespace ludus::foundation::math;

namespace
{
[[nodiscard]] bool Close(double a, double b, double t = 1e-5)
{
    return std::fabs(a - b) <= t;
}
constexpr double kInf = std::numeric_limits<double>::infinity();
} // namespace

TEST_CASE("Plane construction and signed distance", "[math][queries]")
{
    Plane pl{};
    REQUIRE(TryMakePlane(Vector3{0, 0, 2}, Vector3{0, 0, 5}, pl) == MathStatus::Success);
    REQUIRE(Close(pl.Normal.Z, 1.0));
    REQUIRE(Close(pl.D, -5.0));
    float d = 0.0f;
    REQUIRE(TryPlaneSignedDistance(pl, Vector3{0, 0, 8}, d) == MathStatus::Success);
    REQUIRE(Close(d, 3.0));
    REQUIRE(TryPlaneSignedDistance(pl, Vector3{0, 0, 2}, d) == MathStatus::Success);
    REQUIRE(Close(d, -3.0));
    // Non-unit plane fields rejected.
    Plane bad{Vector3{0, 0, 2}, 0.0f};
    REQUIRE(TryPlaneSignedDistance(bad, Vector3{0, 0, 0}, d) == MathStatus::Degenerate);
    // Zero normal to construct -> Degenerate.
    Plane out = pl;
    const Plane keep = out;
    REQUIRE(TryMakePlane(Vector3{0, 0, 0}, Vector3{0, 0, 0}, out) == MathStatus::Degenerate);
    REQUIRE((out.Normal.Z == keep.Normal.Z && out.D == keep.D));
}

TEST_CASE("Conservative AABB transform encloses all corners", "[math][queries]")
{
    Quaternion q{};
    REQUIRE(TryFromAxisAngle(Vector3{0, 0, 1}, kPiF / 4.0f, q) == MathStatus::Success);
    const Matrix3 r = ToMatrix3(q);
    Affine3 a = Affine3::Identity();
    a.Columns[0] = r.Columns[0];
    a.Columns[1] = r.Columns[1];
    a.Columns[2] = r.Columns[2];
    a.Translation = Vector3{10, -5, 0};
    const Aabb3 box{Vector3{0, 0, 0}, Vector3{1, 1, 1}};
    Aabb3 out{};
    REQUIRE(TryTransformAabb(a, box, out) == MathStatus::Success);
    for (int i = 0; i < 8; ++i)
    {
        const Vector3 corner{(i & 1) ? 1.0f : 0.0f, (i & 2) ? 1.0f : 0.0f, (i & 4) ? 1.0f : 0.0f};
        const Vector3 tc = TransformPoint(a, corner);
        REQUIRE(tc.X >= out.Min.X);
        REQUIRE(tc.X <= out.Max.X);
        REQUIRE(tc.Y >= out.Min.Y);
        REQUIRE(tc.Y <= out.Max.Y);
    }
    // Empty stays empty.
    Aabb3 emptyOut{};
    REQUIRE(TryTransformAabb(a, EmptyAabb3(), emptyOut) == MathStatus::Success);
    REQUIRE(IsEmpty(emptyOut));
}

TEST_CASE("Ray/AABB: hit, parallel miss, inside start, invalid direction", "[math][queries]")
{
    const Aabb3 box{Vector3{-1, -1, -1}, Vector3{1, 1, 1}};
    const RayInterval iv{0.0, kInf};
    RayHit h{};
    REQUIRE(TryRayAabb(Ray3{Vector3{-5, 0, 0}, Vector3{1, 0, 0}}, box, iv, h) == MathStatus::Success);
    REQUIRE(h.Result == HitResult::Hit);
    REQUIRE(Close(h.EntryT, 4.0));
    REQUIRE(Close(h.ExitT, 6.0));
    // Parallel outside slab misses.
    REQUIRE(TryRayAabb(Ray3{Vector3{0, 5, 0}, Vector3{1, 0, 0}}, box, iv, h) == MathStatus::Success);
    REQUIRE(h.Result == HitResult::Miss);
    // Inside start clips entry to minT.
    REQUIRE(TryRayAabb(Ray3{Vector3{0, 0, 0}, Vector3{1, 0, 0}}, box, iv, h) == MathStatus::Success);
    REQUIRE(h.Result == HitResult::Hit);
    REQUIRE(Close(h.EntryT, 0.0));
    // Zero direction invalid.
    REQUIRE(TryRayAabb(Ray3{Vector3{0, 0, 0}, Vector3{0, 0, 0}}, box, iv, h) == MathStatus::InvalidArgument);
}

TEST_CASE("Ray/sphere: hit, tangent, miss, inside start, negative radius", "[math][queries]")
{
    const Sphere sp{Vector3{0, 0, 0}, 1.0f};
    const RayInterval iv{0.0, kInf};
    RayHit h{};
    REQUIRE(TryRaySphere(Ray3{Vector3{-5, 0, 0}, Vector3{1, 0, 0}}, sp, iv, h) == MathStatus::Success);
    REQUIRE(h.Result == HitResult::Hit);
    REQUIRE(Close(h.EntryT, 4.0));
    REQUIRE(Close(h.ExitT, 6.0));
    // Tangent counts as hit.
    REQUIRE(TryRaySphere(Ray3{Vector3{-5, 1, 0}, Vector3{1, 0, 0}}, sp, iv, h) == MathStatus::Success);
    REQUIRE(h.Result == HitResult::Hit);
    // Miss.
    REQUIRE(TryRaySphere(Ray3{Vector3{-5, 2, 0}, Vector3{1, 0, 0}}, sp, iv, h) == MathStatus::Success);
    REQUIRE(h.Result == HitResult::Miss);
    // Inside start -> entry minT.
    REQUIRE(TryRaySphere(Ray3{Vector3{0, 0, 0}, Vector3{1, 0, 0}}, sp, iv, h) == MathStatus::Success);
    REQUIRE(h.Result == HitResult::Hit);
    REQUIRE(Close(h.EntryT, 0.0));
    // Negative radius invalid.
    REQUIRE(TryRaySphere(Ray3{Vector3{-5, 0, 0}, Vector3{1, 0, 0}}, Sphere{Vector3{0, 0, 0}, -1.0f}, iv, h) ==
            MathStatus::InvalidArgument);
}

TEST_CASE("Closest points between segments: skew, parallel, degenerate", "[math][queries]")
{
    SegmentClosest sc{};
    // Skew segments crossing at distance 1 along Z.
    REQUIRE(TryClosestPointsSegments(Vector3{-1, 0, 0}, Vector3{1, 0, 0}, Vector3{0, -1, 1}, Vector3{0, 1, 1}, sc) ==
            MathStatus::Success);
    REQUIRE(Close(sc.SquaredDistance, 1.0, 1e-6));
    REQUIRE(Close(sc.S, 0.5, 1e-6));
    REQUIRE(Close(sc.T, 0.5, 1e-6));
    // Parallel segments.
    REQUIRE(TryClosestPointsSegments(Vector3{0, 0, 0}, Vector3{1, 0, 0}, Vector3{0, 1, 0}, Vector3{1, 1, 0}, sc) ==
            MathStatus::Success);
    REQUIRE(Close(sc.SquaredDistance, 1.0, 1e-6));
    // Point vs segment.
    REQUIRE(TryClosestPointsSegments(Vector3{0.5f, 2, 0}, Vector3{0.5f, 2, 0}, Vector3{0, 0, 0}, Vector3{1, 0, 0}, sc) ==
            MathStatus::Success);
    REQUIRE(Close(sc.SquaredDistance, 4.0, 1e-6));
    REQUIRE(Close(sc.T, 0.5, 1e-6));
}

TEST_CASE("Frustum extraction and conservative classification", "[math][queries]")
{
    Matrix4 p{};
    REQUIRE(TryPerspectiveReverseZ(DegreesToRadians(90.0f), 1.0f, 0.1f, 100.0f, p) == MathStatus::Success);
    Frustum fr{};
    REQUIRE(TryExtractFrustum(p, FrustumMode::FinitePerspectiveOrOrthographic, fr) == MathStatus::Success);
    FrustumRelation rel = FrustumRelation::Outside;
    REQUIRE(TryClassifySphere(fr, Sphere{Vector3{0, 0, -10}, 0.1f}, 0.0f, rel) == MathStatus::Success);
    REQUIRE(rel == FrustumRelation::Inside);
    REQUIRE(TryClassifySphere(fr, Sphere{Vector3{0, 0, 10}, 0.1f}, 0.0f, rel) == MathStatus::Success);
    REQUIRE(rel == FrustumRelation::Outside);
    REQUIRE(TryClassifySphere(fr, Sphere{Vector3{0, 0, -0.1f}, 0.5f}, 0.0f, rel) == MathStatus::Success);
    REQUIRE(rel == FrustumRelation::Intersecting);
    // AABB inside and empty-outside.
    REQUIRE(TryClassifyAabb(fr, Aabb3{Vector3{-0.1f, -0.1f, -10.1f}, Vector3{0.1f, 0.1f, -9.9f}}, 0.0f, rel) ==
            MathStatus::Success);
    REQUIRE(rel == FrustumRelation::Inside);
    REQUIRE(TryClassifyAabb(fr, EmptyAabb3(), 0.0f, rel) == MathStatus::Success);
    REQUIRE(rel == FrustumRelation::Outside);
}

TEST_CASE("Infinite frustum marks far plane inactive and does not cull distant objects",
          "[math][queries]")
{
    Matrix4 p{};
    REQUIRE(TryPerspectiveReverseZInfinite(DegreesToRadians(90.0f), 1.0f, 0.1f, p) == MathStatus::Success);
    Frustum fr{};
    REQUIRE(TryExtractFrustum(p, FrustumMode::InfiniteReverseZPerspective, fr) == MathStatus::Success);
    REQUIRE((fr.ActiveMask & (1u << 5)) == 0); // far inactive
    FrustumRelation rel = FrustumRelation::Outside;
    REQUIRE(TryClassifySphere(fr, Sphere{Vector3{0, 0, -1e6f}, 1.0f}, 0.0f, rel) == MathStatus::Success);
    REQUIRE(rel == FrustumRelation::Inside); // no false-negative far culling
}

TEST_CASE("Classification agrees with direct clip inequalities (no false-negative culling)",
          "[math][queries]")
{
    Matrix4 p{};
    REQUIRE(TryPerspectiveReverseZ(DegreesToRadians(70.0f), 1.3f, 0.2f, 50.0f, p) == MathStatus::Success);
    Frustum fr{};
    REQUIRE(TryExtractFrustum(p, FrustumMode::FinitePerspectiveOrOrthographic, fr) == MathStatus::Success);
    // Sample a grid of points; any point whose clip coords satisfy the canonical
    // inequalities (-w<=x,y<=w, 0<=z<=w) must NOT be classified Outside.
    int tested = 0;
    for (int ix = -6; ix <= 6; ++ix)
    {
        for (int iy = -6; iy <= 6; ++iy)
        {
            for (int iz = 1; iz <= 10; ++iz)
            {
                const Vector3 world{static_cast<float>(ix), static_cast<float>(iy), -static_cast<float>(iz)};
                const Vector4 clip = p * Vector4{world.X, world.Y, world.Z, 1.0f};
                const float w = clip.W;
                const bool insideClip = (w > 0.0f) && (clip.X >= -w) && (clip.X <= w) && (clip.Y >= -w) &&
                                        (clip.Y <= w) && (clip.Z >= 0.0f) && (clip.Z <= w);
                if (!insideClip)
                {
                    continue;
                }
                FrustumRelation rel = FrustumRelation::Outside;
                REQUIRE(TryClassifySphere(fr, Sphere{world, 0.0f}, 0.0f, rel) == MathStatus::Success);
                REQUIRE(rel != FrustumRelation::Outside); // conservative: never cull a visible point
                ++tested;
            }
        }
    }
    REQUIRE(tested > 0);
}
