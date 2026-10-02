// Zero-allocation probe for FoundationMath. Representative checked operations are
// driven through the real production functions under a counting operator
// new/delete; none of them may allocate. Built as its own executable and only
// when sanitizers are OFF (the sanitizer runtime provides its own new/delete).
// Mirrors modules/foundation/containers/tests/allocation_tests.cpp.

#include <ludus/foundation/math/batch.hpp>
#include <ludus/foundation/math/matrix.hpp>
#include <ludus/foundation/math/projection.hpp>
#include <ludus/foundation/math/queries.hpp>
#include <ludus/foundation/math/quaternion.hpp>
#include <ludus/foundation/math/random.hpp>
#include <ludus/foundation/math/transform.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <span>

namespace
{
long gNews = 0;
long gDeletes = 0;
bool gTracking = false;

void* AlignedAlloc(std::size_t n, std::size_t al) noexcept
{
    if (al < alignof(std::max_align_t))
    {
        al = alignof(std::max_align_t);
    }
    std::size_t rounded = (n + al - 1) & ~(al - 1);
    if (rounded == 0)
    {
        rounded = al;
    }
    return std::aligned_alloc(al, rounded);
}
void CountNew() noexcept
{
    if (gTracking)
    {
        ++gNews;
    }
}
void CountDelete(void* p) noexcept
{
    if (p != nullptr && gTracking)
    {
        ++gDeletes;
    }
}
} // namespace

void* operator new(std::size_t n)
{
    CountNew();
    void* p = AlignedAlloc(n, alignof(std::max_align_t));
    if (p == nullptr)
    {
        throw std::bad_alloc{};
    }
    return p;
}
void* operator new(std::size_t n, std::align_val_t al)
{
    CountNew();
    void* p = AlignedAlloc(n, static_cast<std::size_t>(al));
    if (p == nullptr)
    {
        throw std::bad_alloc{};
    }
    return p;
}
void* operator new(std::size_t n, const std::nothrow_t&) noexcept
{
    CountNew();
    return AlignedAlloc(n, alignof(std::max_align_t));
}
void operator delete(void* p) noexcept
{
    CountDelete(p);
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept
{
    CountDelete(p);
    std::free(p);
}
void operator delete(void* p, std::align_val_t) noexcept
{
    CountDelete(p);
    std::free(p);
}
void operator delete(void* p, std::size_t, std::align_val_t) noexcept
{
    CountDelete(p);
    std::free(p);
}

using namespace ludus::foundation::math;

namespace
{
struct TrackGuard
{
    TrackGuard()
    {
        gNews = 0;
        gDeletes = 0;
        gTracking = true;
    }
    ~TrackGuard()
    {
        gTracking = false;
    }
};
} // namespace

TEST_CASE("Checked math operations never allocate", "[math][alloc]")
{
    TrackGuard guard;

    // Normalisation.
    Vector3 unit{};
    REQUIRE(TryNormalize(Vector3{3, 0, 4}, unit) == MathStatus::Success);

    // Quaternion construction + rotation + conversion + extraction.
    Quaternion q{};
    REQUIRE(TryFromAxisAngle(Vector3{0, 0, 1}, 0.7f, q) == MathStatus::Success);
    const Vector3 rotated = Rotate(q, Vector3{1, 2, 3});
    const Matrix3 r = ToMatrix3(q);
    Quaternion back{};
    REQUIRE(TryToRotation(r, back) == MathStatus::Success);
    (void)rotated;

    // Matrix / affine inverse.
    Matrix4 m = ToMatrix4([] {
        Affine3 a = Affine3::Identity();
        a.Translation = Vector3{1, 2, 3};
        return a;
    }());
    Matrix4 inv{};
    REQUIRE(TryInverse(m, inv) == MathStatus::Success);

    // Projection + classification.
    Matrix4 p{};
    REQUIRE(TryPerspectiveReverseZ(1.0f, 1.0f, 0.1f, 100.0f, p) == MathStatus::Success);
    Frustum fr{};
    REQUIRE(TryExtractFrustum(p, FrustumMode::FinitePerspectiveOrOrthographic, fr) == MathStatus::Success);
    FrustumRelation rel = FrustumRelation::Outside;
    REQUIRE(TryClassifySphere(fr, Sphere{Vector3{0, 0, -10}, 1.0f}, 0.0f, rel) == MathStatus::Success);

    // Queries.
    RayHit hit{};
    REQUIRE(TryRaySphere(Ray3{Vector3{-5, 0, 0}, Vector3{1, 0, 0}}, Sphere{Vector3{0, 0, 0}, 1.0f},
                         RayInterval{0.0, 1e30}, hit) == MathStatus::Success);

    // Random stream.
    RandomStream rng;
    (void)rng.NextUInt32();
    (void)rng.NextFloat01();

    // Batch (caller-owned stack buffers; no heap scratch inside).
    std::array<Vector3, 8> input{};
    std::array<Vector3, 8> output{};
    REQUIRE(TryTransformPoints([] {
        Affine3 a = Affine3::Identity();
        return a;
    }(), std::span<const Vector3>(input), std::span<Vector3>(output)) == MathStatus::Success);

    REQUIRE(gNews == 0);
    REQUIRE(gDeletes == 0);
}
