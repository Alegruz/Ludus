// Batch point transform and sphere classification: odd counts, tails, in-place,
// partial overlap rejection, size mismatch, empty, transactional failure.

#include <ludus/foundation/math/batch.hpp>
#include <ludus/foundation/math/projection.hpp>
#include <ludus/foundation/math/queries.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <limits>
#include <span>
#include <vector>

using namespace ludus::foundation::math;

namespace
{
[[nodiscard]] Affine3 MakeTransform()
{
    Affine3 a = Affine3::Identity();
    a.Columns[0] = Vector3{0, 1, 0};
    a.Columns[1] = Vector3{-1, 0, 0}; // 90-degree rotation about Z
    a.Translation = Vector3{3, 4, 5};
    return a;
}
} // namespace

TEST_CASE("Batch transform matches per-element and supports odd counts/tails", "[math][batch]")
{
    const Affine3 a = MakeTransform();
    for (int n : {0, 1, 3, 4, 7, 8, 15, 16, 17})
    {
        std::vector<Vector3> input(static_cast<usize>(n));
        std::vector<Vector3> output(static_cast<usize>(n));
        for (int i = 0; i < n; ++i)
        {
            input[static_cast<usize>(i)] = Vector3{static_cast<float>(i), static_cast<float>(-i), 1.0f};
        }
        REQUIRE(TryTransformPoints(a, std::span<const Vector3>(input), std::span<Vector3>(output)) ==
                MathStatus::Success);
        for (int i = 0; i < n; ++i)
        {
            REQUIRE(output[static_cast<usize>(i)] == TransformPoint(a, input[static_cast<usize>(i)]));
        }
    }
}

TEST_CASE("Exact in-place transform is supported; partial overlap rejected", "[math][batch]")
{
    const Affine3 a = MakeTransform();
    std::array<Vector3, 4> buffer{Vector3{1, 0, 0}, Vector3{0, 1, 0}, Vector3{0, 0, 1}, Vector3{1, 1, 1}};
    std::array<Vector3, 4> reference{};
    for (usize i = 0; i < buffer.size(); ++i)
    {
        reference[i] = TransformPoint(a, buffer[i]);
    }
    REQUIRE(TryTransformPoints(a, std::span<const Vector3>(buffer), std::span<Vector3>(buffer)) == MathStatus::Success);
    for (usize i = 0; i < buffer.size(); ++i)
    {
        REQUIRE(buffer[i] == reference[i]);
    }
    // Partial overlap (output shifted by one) rejected.
    std::array<Vector3, 4> b2{Vector3{1, 0, 0}, Vector3{0, 1, 0}, Vector3{0, 0, 1}, Vector3{1, 1, 1}};
    const std::array<Vector3, 4> keep = b2;
    REQUIRE(TryTransformPoints(a, std::span<const Vector3>(b2.data(), 3), std::span<Vector3>(b2.data() + 1, 3)) ==
            MathStatus::InvalidArgument);
    REQUIRE(b2 == keep); // unchanged
}

TEST_CASE("Batch size mismatch, empty, and transactional failure", "[math][batch]")
{
    const Affine3 a = MakeTransform();
    std::array<Vector3, 4> input{};
    std::array<Vector3, 3> small{};
    REQUIRE(TryTransformPoints(a, std::span<const Vector3>(input), std::span<Vector3>(small.data(), 3)) ==
            MathStatus::SizeMismatch);
    REQUIRE(TryTransformPoints(a, std::span<const Vector3>(), std::span<Vector3>()) == MathStatus::Success);

    // A non-finite element rejects the whole batch, leaving output untouched.
    std::array<Vector3, 3> bad{Vector3{1, 2, 3}, Vector3{std::numeric_limits<float>::quiet_NaN(), 0, 0},
                               Vector3{4, 5, 6}};
    std::array<Vector3, 3> out{Vector3{9, 9, 9}, Vector3{9, 9, 9}, Vector3{9, 9, 9}};
    const std::array<Vector3, 3> keep = out;
    REQUIRE(TryTransformPoints(a, std::span<const Vector3>(bad), std::span<Vector3>(out)) == MathStatus::NonFiniteInput);
    REQUIRE(out == keep); // all-or-nothing
}

TEST_CASE("Batch sphere classification matches scalar and rejects overlap", "[math][batch]")
{
    Matrix4 p{};
    REQUIRE(TryPerspectiveReverseZ(DegreesToRadians(90.0f), 1.0f, 0.1f, 100.0f, p) == MathStatus::Success);
    Frustum fr{};
    REQUIRE(TryExtractFrustum(p, FrustumMode::FinitePerspectiveOrOrthographic, fr) == MathStatus::Success);
    std::array<Sphere, 3> spheres{Sphere{Vector3{0, 0, -10}, 0.1f}, Sphere{Vector3{0, 0, 10}, 0.1f},
                                  Sphere{Vector3{0, 0, -0.1f}, 0.5f}};
    std::array<FrustumRelation, 3> out{};
    REQUIRE(TryClassifySpheres(fr, std::span<const Sphere>(spheres), 0.0f, std::span<FrustumRelation>(out)) ==
            MathStatus::Success);
    REQUIRE(out[0] == FrustumRelation::Inside);
    REQUIRE(out[1] == FrustumRelation::Outside);
    REQUIRE(out[2] == FrustumRelation::Intersecting);
    for (usize i = 0; i < spheres.size(); ++i)
    {
        FrustumRelation scalar = FrustumRelation::Outside;
        REQUIRE(TryClassifySphere(fr, spheres[i], 0.0f, scalar) == MathStatus::Success);
        REQUIRE(scalar == out[i]);
    }
}
