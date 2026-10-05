#include <ludus/foundation/math/cubic.hpp>
#include <ludus/foundation/math/patch.hpp>
#include <ludus/foundation/math/random.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

using namespace ludus::foundation::math;
using ludus::foundation::core::usize;

namespace
{
constexpr float64 kTolerance = 2e-5;
constexpr float32 kNaN = std::numeric_limits<float32>::quiet_NaN();
constexpr float64 kInfinity = std::numeric_limits<float64>::infinity();

void Close(float32 actual, float64 expected, float64 tolerance = kTolerance)
{
    REQUIRE(std::abs(static_cast<float64>(actual) - expected) <= tolerance);
}
void Close(Vector3 actual, Vector3d expected, float64 tolerance = kTolerance)
{
    Close(actual.X, expected.X, tolerance);
    Close(actual.Y, expected.Y, tolerance);
    Close(actual.Z, expected.Z, tolerance);
}
Vector3d Wide(Vector3 value)
{
    return {static_cast<float64>(value.X), static_cast<float64>(value.Y), static_cast<float64>(value.Z)};
}

// Independent Bernstein basis and its explicit analytic derivatives. No
// production interpolation, difference-net or subdivision helpers are used.
void Basis(float64 u, float64* out, usize order)
{
    const float64 s = 1 - u;
    if (order == 0)
    {
        out[0] = s * s * s;
        out[1] = 3 * u * s * s;
        out[2] = 3 * u * u * s;
        out[3] = u * u * u;
    }
    else if (order == 1)
    {
        out[0] = -3 * s * s;
        out[1] = 3 * s * s - 6 * u * s;
        out[2] = 6 * u * s - 3 * u * u;
        out[3] = 3 * u * u;
    }
    else
    {
        out[0] = 6 * s;
        out[1] = 18 * u - 12;
        out[2] = 6 - 18 * u;
        out[3] = 6 * u;
    }
}
Vector3d CurveOracle(const CubicBezier3& curve, float64 u, usize order)
{
    float64 basis[4]{};
    Basis(u, basis, order);
    Vector3d result;
    for (usize i = 0; i < 4; ++i)
    {
        result = result + Wide(curve.Control[i]) * basis[i];
    }
    return result;
}
Vector3d PatchOracle(const BicubicBezierPatch& patch, PatchParameter parameter, usize du, usize dv)
{
    float64 ub[4]{}, vb[4]{};
    Basis(parameter.U, ub, du);
    Basis(parameter.V, vb, dv);
    Vector3d result;
    for (usize u = 0; u < 4; ++u)
    {
        for (usize v = 0; v < 4; ++v)
        {
            result = result + Wide(patch.Control[u][v]) * (ub[u] * vb[v]);
        }
    }
    return result;
}
BicubicBezierPatch Saddle()
{
    BicubicBezierPatch patch;
    for (usize u = 0; u < 4; ++u)
    {
        for (usize v = 0; v < 4; ++v)
        {
            const float32 x = static_cast<float32>(u) / 3.0f, y = static_cast<float32>(v) / 3.0f;
            patch.Control[u][v] = {x, y, x * y};
        }
    }
    return patch;
}
} // namespace

TEST_CASE("Cubic scalar and 2D samples have normalized analytic derivatives", "[math][bezier]")
{
    CubicSample1 scalar;
    REQUIRE(TryEvaluateCubic(CubicBezier1{{0, 0, 0, 1}}, 0.5, scalar) == MathStatus::Success);
    REQUIRE(scalar.Position == 0.125f);
    REQUIRE(scalar.FirstDerivative == 0.75f);
    REQUIRE(scalar.SecondDerivative == 3.0f);
    const CubicBezier2 curve{{{0, 2}, {0, 2}, {0, 2}, {1, 2}}};
    CubicSample2 sample;
    REQUIRE(TryEvaluateCubic(curve, 0.5, sample) == MathStatus::Success);
    REQUIRE(sample.Position == Vector2{0.125f, 2});
    REQUIRE(sample.FirstDerivative == Vector2{0.75f, 0});
    REQUIRE(sample.SecondDerivative == Vector2{3, 0});
    Vector2 position;
    REQUIRE(TryEvaluateCubic(curve, 0.5, position) == MathStatus::Success);
    REQUIRE(position == sample.Position);
}

TEST_CASE("Cubic samples match an independent polynomial oracle", "[math][bezier]")
{
    RandomStream random;
    for (usize trial = 0; trial < 100; ++trial)
    {
        CubicBezier3 curve;
        for (Vector3& point : curve.Control)
        {
            point = {random.NextFloat01() * 20 - 10, random.NextFloat01() * 20 - 10, random.NextFloat01() * 20 - 10};
        }
        for (float64 u : {0.0, 0.0001, 0.13, 0.5, 0.93, 1.0})
        {
            CubicSample3 sample;
            REQUIRE(TryEvaluateCubic(curve, u, sample) == MathStatus::Success);
            Close(sample.Position, CurveOracle(curve, u, 0));
            Close(sample.FirstDerivative, CurveOracle(curve, u, 1));
            Close(sample.SecondDerivative, CurveOracle(curve, u, 2));
        }
    }
}

TEST_CASE("Cubic checks preserve outputs and distinguish position range", "[math][bezier]")
{
    CubicBezier1 curve{{-0.0f, 1, 2, 3}};
    float32 position = 7;
    REQUIRE(TryEvaluateCubic(curve, 0, position) == MathStatus::Success);
    REQUIRE(std::signbit(position));
    curve.Control[3] = -0.0f;
    REQUIRE(TryEvaluateCubic(curve, 1, position) == MathStatus::Success);
    REQUIRE(std::signbit(position));
    for (float64 u : {-0.1, 1.1, kInfinity})
    {
        position = 7;
        REQUIRE(TryEvaluateCubic(curve, u, position) != MathStatus::Success);
        REQUIRE(position == 7);
    }
    for (usize i = 0; i < 4; ++i)
    {
        CubicBezier1 invalid = curve;
        invalid.Control[i] = kNaN;
        position = 7;
        REQUIRE(TryEvaluateCubic(invalid, 0, position) == MathStatus::NonFiniteInput);
        REQUIRE(position == 7);
    }
    const float32 large = std::numeric_limits<float32>::max();
    curve = {{-large, large, large, large}};
    REQUIRE(TryEvaluateCubic(curve, 0, position) == MathStatus::Success);
    REQUIRE(position == -large);
    CubicSample1 sample{7, 8, 9};
    REQUIRE(TryEvaluateCubic(curve, 0, sample) == MathStatus::OutOfRange);
    REQUIRE(sample.Position == 7);
    REQUIRE(sample.FirstDerivative == 8);
    REQUIRE(sample.SecondDerivative == 9);
    // Input/output aliasing is safe: controls are loaded before publishing.
    curve = {{0, 0, 0, 1}};
    REQUIRE(TryEvaluateCubic(curve, 0.5, curve.Control[0]) == MathStatus::Success);
    REQUIRE(curve.Control[0] == 0.125f);
}

TEST_CASE("Hermite conversion scales external-domain derivatives", "[math][bezier]")
{
    CubicBezier1 curve;
    REQUIRE(TryFromHermite(CubicHermite1{2, 5, 3, -6}, 2, curve) == MathStatus::Success);
    REQUIRE(curve.Control[1] == 4);
    REQUIRE(curve.Control[2] == 9);
    CubicSample1 sample;
    REQUIRE(TryEvaluateCubic(curve, 0, sample) == MathStatus::Success);
    REQUIRE(sample.FirstDerivative == 6);
    REQUIRE(TryEvaluateCubic(curve, 1, sample) == MathStatus::Success);
    REQUIRE(sample.FirstDerivative == -12);
    const CubicBezier1 before = curve;
    for (float64 width : {0.0, -1.0, kInfinity})
    {
        REQUIRE(TryFromHermite(CubicHermite1{}, width, curve) != MathStatus::Success);
        for (usize i = 0; i < 4; ++i)
        {
            REQUIRE(curve.Control[i] == before.Control[i]);
        }
    }
    REQUIRE(TryFromHermite(CubicHermite1{0, 0, 10, 0}, std::numeric_limits<float64>::max(), curve) ==
            MathStatus::OutOfRange);
    REQUIRE(curve.Control[1] == before.Control[1]);
    REQUIRE(TryFromHermite(CubicHermite1{0, 0, kNaN, 0}, 1, curve) == MathStatus::NonFiniteInput);
    CubicBezier2 c2;
    REQUIRE(TryFromHermite(CubicHermite2{{0, 0}, {2, 4}, {1, 2}, {1, 2}}, 2, c2) == MathStatus::Success);
    CubicBezier3 c3;
    REQUIRE(TryFromHermite(CubicHermite3{{0, 0, 0}, {2, 4, 6}, {1, 2, 3}, {1, 2, 3}}, 2, c3) == MathStatus::Success);
    CubicSample3 s3;
    REQUIRE(TryEvaluateCubic(c3, 0.5, s3) == MathStatus::Success);
    Close(s3.Position, {1, 2, 3});
    Close(s3.FirstDerivative, {2, 4, 6});
}

TEST_CASE("Cubic subdivision preserves shape and rescales child derivatives", "[math][bezier]")
{
    const CubicBezier3 curve{{{1, 2, 3}, {-3, 1, 5}, {4, 6, -2}, {7, -1, 8}}};
    for (float64 split : {0.0, 0.27, 1.0})
    {
        CubicBezier3 left, right;
        REQUIRE(TrySplitCubic(curve, split, left, right) == MathStatus::Success);
        REQUIRE(left.Control[3] == right.Control[0]);
        REQUIRE(left.Control[0] == curve.Control[0]);
        REQUIRE(right.Control[3] == curve.Control[3]);
        for (float64 u : {0.0, 0.2, 0.9, 1.0})
        {
            CubicSample3 sample;
            REQUIRE(TryEvaluateCubic(left, u, sample) == MathStatus::Success);
            Close(sample.Position, CurveOracle(curve, split * u, 0));
            Close(sample.FirstDerivative, CurveOracle(curve, split * u, 1) * split);
            Close(sample.SecondDerivative, CurveOracle(curve, split * u, 2) * (split * split));
            REQUIRE(TryEvaluateCubic(right, u, sample) == MathStatus::Success);
            Close(sample.Position, CurveOracle(curve, split + (1 - split) * u, 0));
            Close(sample.FirstDerivative, CurveOracle(curve, split + (1 - split) * u, 1) * (1 - split));
        }
        CubicBezier3 aliased = curve;
        REQUIRE(TrySplitCubic(aliased, split, aliased, right) == MathStatus::Success);
        for (usize i = 0; i < 4; ++i)
        {
            REQUIRE(aliased.Control[i] == left.Control[i]);
        }
        REQUIRE(TrySplitCubic(curve, split, aliased, aliased) == MathStatus::InvalidArgument);
        REQUIRE(TrySplitCubic(curve, kInfinity, aliased, right) == MathStatus::NonFiniteInput);
        for (usize i = 0; i < 4; ++i)
        {
            REQUIRE(aliased.Control[i] == left.Control[i]);
        }
    }
    CubicBezier1 l1, r1;
    CubicBezier2 l2, r2;
    REQUIRE(TrySplitCubic(CubicBezier1{{0, 0, 0, 1}}, 0.5, l1, r1) == MathStatus::Success);
    REQUIRE(l1.Control[3] == 0.125f);
    REQUIRE(TrySplitCubic(CubicBezier2{{{0, 0}, {0, 0}, {0, 0}, {1, 2}}}, 0.5, l2, r2) == MathStatus::Success);
    REQUIRE(l2.Control[3] == Vector2{0.125f, 0.25f});
}

TEST_CASE("Bicubic tensor samples match independent derivative oracles", "[math][bezier]")
{
    RandomStream random;
    for (usize trial = 0; trial < 25; ++trial)
    {
        BicubicBezierPatch patch;
        for (auto& row : patch.Control)
        {
            for (Vector3& p : row)
            {
                p = {random.NextFloat01() * 10, random.NextFloat01() * 10, random.NextFloat01() * 10};
            }
        }
        for (PatchParameter uv : {PatchParameter{0, 0}, {0.1, 0.9}, {0.5, 0.5}, {1, 1}})
        {
            PatchSample sample;
            REQUIRE(TryEvaluatePatch(patch, uv, sample) == MathStatus::Success);
            Close(sample.Position, PatchOracle(patch, uv, 0, 0));
            Close(sample.DU, PatchOracle(patch, uv, 1, 0));
            Close(sample.DV, PatchOracle(patch, uv, 0, 1));
            Close(sample.DUU, PatchOracle(patch, uv, 2, 0));
            Close(sample.DUV, PatchOracle(patch, uv, 1, 1));
            Close(sample.DVV, PatchOracle(patch, uv, 0, 2));
        }
    }
    PatchSample sample;
    REQUIRE(TryEvaluatePatch(Saddle(), {0.25, 0.75}, sample) == MathStatus::Success);
    Close(sample.Position, {0.25, 0.75, 0.1875});
    Close(sample.DU, {1, 0, 0.75});
    Close(sample.DV, {0, 1, 0.25});
    Close(sample.DUV, {0, 0, 1});
    Vector3 normal;
    REQUIRE(TryPatchNormal(sample, {}, normal) == MathStatus::Success);
    Close(normal, Vector3d{-0.75, -0.25, 1} * (1 / std::sqrt(1.625)));
}

TEST_CASE("Patch subdivision preserves all four parameter rectangles", "[math][bezier]")
{
    BicubicBezierPatch patch = Saddle();
    patch.Control[1][2].Z = 3;
    for (PatchParameter split : {PatchParameter{0, 1}, {0.37, 0.61}, {1, 0}})
    {
        PatchSubdivision children;
        REQUIRE(TrySplitPatch(patch, split, children) == MathStatus::Success);
        const BicubicBezierPatch* nets[] = {&children.U0V0, &children.U1V0, &children.U0V1, &children.U1V1};
        for (usize child = 0; child < 4; ++child)
        {
            const float64 u0 = (child % 2) != 0 ? split.U : 0, v0 = child >= 2 ? split.V : 0;
            const float64 us = (child % 2) != 0 ? 1 - split.U : split.U, vs = child >= 2 ? 1 - split.V : split.V;
            for (PatchParameter uv : {PatchParameter{0, 0}, {0.2, 0.8}, {1, 1}})
            {
                const PatchParameter parent{u0 + us * uv.U, v0 + vs * uv.V};
                PatchSample sample;
                REQUIRE(TryEvaluatePatch(*nets[child], uv, sample) == MathStatus::Success);
                Close(sample.Position, PatchOracle(patch, parent, 0, 0));
                Close(sample.DU, PatchOracle(patch, parent, 1, 0) * us);
                Close(sample.DV, PatchOracle(patch, parent, 0, 1) * vs);
                Close(sample.DUV, PatchOracle(patch, parent, 1, 1) * (us * vs));
            }
        }
        for (usize i = 0; i < 4; ++i)
        {
            REQUIRE(children.U0V0.Control[3][i] == children.U1V0.Control[0][i]);
            REQUIRE(children.U0V0.Control[i][3] == children.U0V1.Control[i][0]);
        }
        PatchSubdivision alias;
        alias.U0V0 = patch;
        REQUIRE(TrySplitPatch(alias.U0V0, split, alias) == MathStatus::Success);
        REQUIRE(alias.U1V1.Control[1][2] == children.U1V1.Control[1][2]);
        REQUIRE(TrySplitPatch(patch, {-1, 0}, alias) == MathStatus::InvalidArgument);
        REQUIRE(alias.U1V1.Control[1][2] == children.U1V1.Control[1][2]);
    }
}

TEST_CASE("Patch failures are transactional and normals expose singularities", "[math][bezier]")
{
    BicubicBezierPatch patch = Saddle();
    patch.Control[0][0] = {-0.0f, -0.0f, -0.0f};
    Vector3 out{7, 8, 9};
    REQUIRE(TryEvaluatePatch(patch, {}, out) == MathStatus::Success);
    REQUIRE(std::signbit(out.X));
    REQUIRE(std::signbit(out.Y));
    REQUIRE(std::signbit(out.Z));
    out = {7, 8, 9};
    REQUIRE(TryEvaluatePatch(patch, {0, kInfinity}, out) == MathStatus::NonFiniteInput);
    REQUIRE(out == Vector3{7, 8, 9});
    patch.Control[3][3].Z = kNaN;
    REQUIRE(TryEvaluatePatch(patch, {}, out) == MathStatus::NonFiniteInput);
    REQUIRE(out == Vector3{7, 8, 9});
    patch = Saddle();
    const float32 large = std::numeric_limits<float32>::max();
    patch.Control[0][0].X = -large;
    patch.Control[1][0].X = large;
    PatchSample sentinel;
    sentinel.Position = {7, 8, 9};
    sentinel.DUV = {4, 5, 6};
    REQUIRE(TryEvaluatePatch(patch, {}, out) == MathStatus::Success);
    REQUIRE(TryEvaluatePatch(patch, {}, sentinel) == MathStatus::OutOfRange);
    REQUIRE(sentinel.Position == Vector3{7, 8, 9});
    REQUIRE(sentinel.DUV == Vector3{4, 5, 6});
    PatchSample sample;
    out = {7, 8, 9};
    REQUIRE(TryPatchNormal(sample, {}, out) == MathStatus::Degenerate);
    REQUIRE(out == Vector3{7, 8, 9});
    sample.DU = {1, 0, 0};
    sample.DV = {2, 0, 0};
    REQUIRE(TryPatchNormal(sample, {}, out) == MathStatus::Degenerate);
    sample.DU = {1, 2, 3};
    sample.DV = {3, 6, 9};
    REQUIRE(TryPatchNormal(sample, {}, out) == MathStatus::Degenerate);
    REQUIRE(TryPatchNormal(sample, {10, 0.1}, out) == MathStatus::Degenerate);
    sample.DU = {1, 0, 0};
    sample.DV = {1, 0.001f, 0};
    REQUIRE(TryPatchNormal(sample, {0, 0.01}, out) == MathStatus::IllConditioned);
    REQUIRE(TryPatchNormal(sample, {2, 0}, out) == MathStatus::IllConditioned);
    REQUIRE(TryPatchNormal(sample, {0, 1}, out) == MathStatus::InvalidArgument);
    REQUIRE(out == Vector3{7, 8, 9});
    sample.DU.X = kNaN;
    REQUIRE(TryPatchNormal(sample, {}, out) == MathStatus::NonFiniteInput);
    for (float32 scale : {std::numeric_limits<float32>::denorm_min(), large})
    {
        sample.DU = {scale, 0, 0};
        sample.DV = {0, scale, 0};
        REQUIRE(TryPatchNormal(sample, {}, sample.DU) == MathStatus::Success);
        REQUIRE(sample.DU == Vector3{0, 0, 1});
    }
}
