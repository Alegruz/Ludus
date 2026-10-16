// Cross-checks the G1 cubic/bicubic kernel against the G0 reference corpus
// (tests/fixtures/curves_surfaces, ADR 0024). The corpus is generated from exact
// rational and 50-digit arithmetic without Ludus code. G1 results are Rounded:
// the envelope below is an estimated regression bound, not a certified error.

#include <ludus/foundation/math/cubic.hpp>
#include <ludus/foundation/math/patch.hpp>
#include <ludus/foundation/math/precision.hpp>

#include <curves_surfaces/reference_fixtures.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace ludus::foundation::math;
using namespace ludus::geometry_fixtures;

namespace
{
/// Relative rounding allowance: one float32 unit in the last place.
constexpr float64 ROUNDED_ENVELOPE = 1.0 / 8388608.0;
/// Bound on derivative control-net magnitudes relative to the largest control.
constexpr float64 DERIVATIVE_SCALE = 36.0;
/// Dense parameter sampling used for global/projection and length regressions.
constexpr usize DENSE_SAMPLE_COUNT = 4096;
/// Patch grid resolution used for tessellation-bound regressions.
constexpr usize PATCH_GRID_COUNT = 64;

float64 Widen(float32 value)
{
    return static_cast<float64>(value);
}

Vector3 ToVector(ControlPoint point)
{
    return {point.X, point.Y, point.Z};
}

Vector3d ToVector(ReferencePoint point)
{
    return {point.X, point.Y, point.Z};
}

Vector3d Wide(Vector3 value)
{
    return {Widen(value.X), Widen(value.Y), Widen(value.Z)};
}

float64 GetMaxAbsComponent(Vector3d value)
{
    return std::fmax(std::fabs(value.X), std::fmax(std::fabs(value.Y), std::fabs(value.Z)));
}

float64 GetDistance(Vector3d a, Vector3d b)
{
    return std::hypot(a.X - b.X, a.Y - b.Y, a.Z - b.Z);
}

bool AreBitwiseEqual(Vector3 actual, ControlPoint expected)
{
    return std::signbit(actual.X) == std::signbit(expected.X) && actual.X == expected.X &&
           std::signbit(actual.Y) == std::signbit(expected.Y) && actual.Y == expected.Y &&
           std::signbit(actual.Z) == std::signbit(expected.Z) && actual.Z == expected.Z;
}

/// Estimated Rounded-class envelope at a reference magnitude and net scale.
float64 GetEnvelope(float64 referenceMagnitude, float64 scale)
{
    return ROUNDED_ENVELOPE * (referenceMagnitude + scale);
}

void RequireClose(Vector3 actual, ReferencePoint expected, float64 scale)
{
    const Vector3d reference = ToVector(expected);
    const float64 magnitude = std::hypot(reference.X, reference.Y, reference.Z);
    REQUIRE(GetDistance(Wide(actual), reference) <= GetEnvelope(magnitude, scale));
}

CubicBezier3 MakeCurve(const CubicFixture& fixture)
{
    CubicBezier3 curve;
    for (usize i = 0; i < CUBIC_CONTROL_COUNT; ++i)
    {
        curve.Control[i] = ToVector(fixture.Control[i]);
    }
    return curve;
}

float64 GetControlScale(const CubicFixture& fixture)
{
    float64 scale = 0;
    for (const ControlPoint& point : fixture.Control)
    {
        scale = std::fmax(scale, GetMaxAbsComponent(Wide(ToVector(point))));
    }
    return scale;
}

BicubicBezierPatch MakePatch(const PatchFixture& fixture)
{
    BicubicBezierPatch patch;
    for (usize u = 0; u < PATCH_CONTROL_COUNT; ++u)
    {
        for (usize v = 0; v < PATCH_CONTROL_COUNT; ++v)
        {
            patch.Control[u][v] = ToVector(fixture.Control[u][v]);
        }
    }
    return patch;
}

float64 GetControlScale(const PatchFixture& fixture)
{
    float64 scale = 0;
    for (const StaticArray<ControlPoint, PATCH_CONTROL_COUNT>& row : fixture.Control)
    {
        for (const ControlPoint& point : row)
        {
            scale = std::fmax(scale, GetMaxAbsComponent(Wide(ToVector(point))));
        }
    }
    return scale;
}

Vector3d EvaluateOrFail(const CubicBezier3& curve, float64 u)
{
    Vector3 position;
    REQUIRE(TryEvaluateCubic(curve, u, position) == MathStatus::Success);
    return Wide(position);
}

Vector3d EvaluateOrFail(const BicubicBezierPatch& patch, float64 u, float64 v)
{
    Vector3 position;
    REQUIRE(TryEvaluatePatch(patch, PatchParameter{u, v}, position) == MathStatus::Success);
    return Wide(position);
}

MathStatus MapToMathStatus(ExpectedStatus status)
{
    switch (status)
    {
        case ExpectedStatus::Success:
            return MathStatus::Success;
        case ExpectedStatus::Degenerate:
            return MathStatus::Degenerate;
        case ExpectedStatus::IllConditioned:
            return MathStatus::IllConditioned;
        case ExpectedStatus::InvalidDomain:
            return MathStatus::InvalidArgument;
    }
    return MathStatus::InvalidArgument;
}

/// Float32 spacing at a positive magnitude in the normal range (ADR 0024 floor input).
float64 GetFloat32Spacing(float64 magnitude)
{
    return std::ldexp(1.0, std::ilogb(magnitude) - 23);
}

/// Distance from the patch to its P00-P33 diagonal triangle pair, computed independently of production code.
float64 GetTriangleDeviation(const BicubicBezierPatch& patch, float64 u, float64 v)
{
    const Vector3d p00 = Wide(patch.Control[0][0]);
    const Vector3d p30 = Wide(patch.Control[3][0]);
    const Vector3d p03 = Wide(patch.Control[0][3]);
    const Vector3d p33 = Wide(patch.Control[3][3]);
    Vector3d linear;
    if (v <= u)
    {
        linear = p00 + (p30 - p00) * u + (p33 - p30) * v;
    }
    else
    {
        linear = p00 + (p03 - p00) * v + (p33 - p03) * u;
    }
    return GetDistance(EvaluateOrFail(patch, u, v), linear);
}
} // namespace

TEST_CASE("G0 cubic fixtures reproduce exact Bernstein references", "[math][bezier][fixtures]")
{
    for (const CubicFixture& fixture : CUBIC_FIXTURES)
    {
        INFO(fixture.Name);
        const CubicBezier3 curve = MakeCurve(fixture);
        const float64 scale = GetControlScale(fixture);
        for (usize i = 0; i < fixture.Samples.GetSize(); ++i)
        {
            const CubicSampleReference& reference = fixture.Samples[i];
            INFO("u = " << reference.U);
            CubicSample3 sample;
            REQUIRE(TryEvaluateCubic(curve, reference.U, sample) == MathStatus::Success);
            RequireClose(sample.Position, reference.Position, scale);
            RequireClose(sample.FirstDerivative, reference.FirstDerivative, DERIVATIVE_SCALE * scale);
            RequireClose(sample.SecondDerivative, reference.SecondDerivative, DERIVATIVE_SCALE * scale);
            // Endpoint preservation is an Exact guarantee, not an envelope.
            if (reference.U == 0.0)
            {
                REQUIRE(AreBitwiseEqual(sample.Position, fixture.Control[0]) == true);
            }
            if (reference.U == 1.0)
            {
                REQUIRE(AreBitwiseEqual(sample.Position, fixture.Control[CUBIC_CONTROL_COUNT - 1]) == true);
            }
        }
        for (usize i = 0; i < fixture.ZeroSpeedU.GetSize(); ++i)
        {
            CubicSample3 sample;
            REQUIRE(TryEvaluateCubic(curve, fixture.ZeroSpeedU[i], sample) == MathStatus::Success);
            const Vector3d derivative = Wide(sample.FirstDerivative);
            REQUIRE(std::hypot(derivative.X, derivative.Y, derivative.Z) <= GetEnvelope(0, DERIVATIVE_SCALE * scale));
        }
        float64 extent = 0;
        for (const ControlPoint& point : fixture.Control)
        {
            extent = std::fmax(extent, GetMaxAbsComponent(Wide(ToVector(point))));
        }
        REQUIRE(fixture.PositionFloor == 2 * GetFloat32Spacing(extent));
    }
}

TEST_CASE("G0 arc lengths and distances bound G1 chord polygons", "[math][bezier][fixtures]")
{
    for (const CubicFixture& fixture : CUBIC_FIXTURES)
    {
        INFO(fixture.Name);
        const CubicBezier3 curve = MakeCurve(fixture);
        const float64 scale = GetControlScale(fixture);
        float64 chordLength = 0;
        Vector3d previous = EvaluateOrFail(curve, 0);
        for (usize k = 1; k <= DENSE_SAMPLE_COUNT; ++k)
        {
            const Vector3d current =
                EvaluateOrFail(curve, static_cast<float64>(k) / static_cast<float64>(DENSE_SAMPLE_COUNT));
            chordLength += GetDistance(previous, current);
            previous = current;
        }
        // An inscribed polygon never exceeds the true length; dense sampling approaches it.
        REQUIRE(chordLength <=
                fixture.ArcLength + GetEnvelope(fixture.ArcLength, scale) * static_cast<float64>(DENSE_SAMPLE_COUNT));
        REQUIRE(chordLength >= fixture.ArcLength * (1 - 1e-5));
        for (usize i = 0; i < fixture.Distances.GetSize(); ++i)
        {
            const DistanceReference& reference = fixture.Distances[i];
            REQUIRE(reference.U > 0.0);
            REQUIRE(reference.U < 1.0);
            REQUIRE(reference.Distance < fixture.ArcLength);
            if (i > 0)
            {
                REQUIRE(fixture.Distances[i - 1].U < reference.U);
            }
        }
    }
}

TEST_CASE("G0 projection references are global minima of G1 samples", "[math][bezier][fixtures]")
{
    for (const CubicFixture& fixture : CUBIC_FIXTURES)
    {
        INFO(fixture.Name);
        const CubicBezier3 curve = MakeCurve(fixture);
        const float64 scale = GetControlScale(fixture);
        for (usize i = 0; i < fixture.Projections.GetSize(); ++i)
        {
            const ProjectionReference& reference = fixture.Projections[i];
            const Vector3d query = ToVector(reference.Query);
            const float64 envelope = GetEnvelope(reference.Distance, scale);
            REQUIRE(std::fabs(GetDistance(EvaluateOrFail(curve, reference.U), query) - reference.Distance) <= envelope);
            float64 sampledMinimum = GetDistance(EvaluateOrFail(curve, 0), query);
            for (usize k = 1; k <= DENSE_SAMPLE_COUNT; ++k)
            {
                const float64 u = static_cast<float64>(k) / static_cast<float64>(DENSE_SAMPLE_COUNT);
                sampledMinimum = std::fmin(sampledMinimum, GetDistance(EvaluateOrFail(curve, u), query));
            }
            REQUIRE(sampledMinimum >= reference.Distance - envelope);
            REQUIRE(reference.TieCount >= 1);
            REQUIRE(reference.TieCount <= reference.MinimumCount);
            bool isLocalMinimum = false;
            for (usize m = 0; m < reference.MinimumCount; ++m)
            {
                if (reference.MinimumU[m] == reference.U)
                {
                    isLocalMinimum = true;
                }
            }
            REQUIRE(isLocalMinimum == true);
        }
    }
}

TEST_CASE("G0 Catmull-Rom tangents convert through G1 Hermite with explicit knot width", "[math][bezier][fixtures]")
{
    for (const CatmullRomFixture& fixture : CATMULL_ROM_FIXTURES)
    {
        INFO(fixture.Name);
        if (fixture.Status != ExpectedStatus::Success)
        {
            REQUIRE(fixture.SpanWidth == 0.0);
            continue;
        }
        const Vector3d startTangent = ToVector(fixture.StartTangent);
        const Vector3d endTangent = ToVector(fixture.EndTangent);
        const CubicHermite3 hermite{
            ToVector(fixture.Waypoint[1]),
            ToVector(fixture.Waypoint[2]),
            {static_cast<float32>(startTangent.X),
             static_cast<float32>(startTangent.Y),
             static_cast<float32>(startTangent.Z)},
            {static_cast<float32>(endTangent.X),
             static_cast<float32>(endTangent.Y),
             static_cast<float32>(endTangent.Z)},
        };
        CubicBezier3 converted;
        REQUIRE(TryFromHermite(hermite, fixture.SpanWidth, converted) == MathStatus::Success);
        for (usize i = 0; i < CUBIC_CONTROL_COUNT; ++i)
        {
            RequireClose(converted.Control[i], fixture.SpanControl[i], 10.0);
        }
    }
}

TEST_CASE("G0 Hermite track spans keep per-second derivative units", "[math][bezier][fixtures]")
{
    for (const TrackFixture& fixture : TRACK_FIXTURES)
    {
        INFO(fixture.Name);
        for (usize span = 0; span + 1 < fixture.Keys.GetSize(); ++span)
        {
            const TrackKey& start = fixture.Keys[span];
            const TrackKey& end = fixture.Keys[span + 1];
            if (start.Interpolation != TrackInterpolation::Hermite)
            {
                continue;
            }
            const float64 width = end.Time - start.Time;
            CubicBezier1 curve;
            REQUIRE(TryFromHermite(CubicHermite1{start.Value, end.Value, start.OutSlope, end.InSlope}, width, curve) ==
                    MathStatus::Success);
            usize checked = 0;
            for (usize i = 0; i < fixture.Samples.GetSize(); ++i)
            {
                const TrackSampleReference& reference = fixture.Samples[i];
                const bool isInterior = reference.Time > start.Time && reference.Time < end.Time;
                const bool isStart = reference.Time == start.Time && reference.Side == SpanSide::Outgoing;
                const bool isEnd = reference.Time == end.Time && reference.Side == SpanSide::Incoming;
                if (reference.Status != ExpectedStatus::Success ||
                    (isInterior == false && isStart == false && isEnd == false))
                {
                    continue;
                }
                CubicSample1 sample;
                REQUIRE(TryEvaluateCubic(curve, (reference.Time - start.Time) / width, sample) == MathStatus::Success);
                REQUIRE(std::fabs(Widen(sample.Position) - reference.Value) <=
                        GetEnvelope(std::fabs(reference.Value), 4.0));
                // Kernel derivatives are per normalized u; divide by the span width for seconds.
                REQUIRE(std::fabs(Widen(sample.FirstDerivative) / width - reference.DerivativePerSecond) <=
                        GetEnvelope(std::fabs(reference.DerivativePerSecond), DERIVATIVE_SCALE * 4.0));
                ++checked;
            }
            REQUIRE(checked > 0);
            if (fixture.HasOvershoot == true)
            {
                float32 peak = 0;
                REQUIRE(TryEvaluateCubic(curve, (fixture.OvershootTime - start.Time) / width, peak) ==
                        MathStatus::Success);
                REQUIRE(std::fabs(Widen(peak) - fixture.OvershootValue) <= GetEnvelope(fixture.OvershootValue, 4.0));
                REQUIRE(fixture.OvershootValue > Widen(end.Value));
            }
        }
    }
}

TEST_CASE("G0 patch fixtures reproduce exact tensor references and normal classes", "[math][bezier][fixtures]")
{
    for (const PatchFixture& fixture : PATCH_FIXTURES)
    {
        INFO(fixture.Name);
        const BicubicBezierPatch patch = MakePatch(fixture);
        const float64 scale = GetControlScale(fixture);
        for (usize i = 0; i < fixture.Samples.GetSize(); ++i)
        {
            const PatchSampleReference& reference = fixture.Samples[i];
            INFO("(u, v) = (" << reference.U << ", " << reference.V << ")");
            PatchSample sample;
            REQUIRE(TryEvaluatePatch(patch, PatchParameter{reference.U, reference.V}, sample) == MathStatus::Success);
            RequireClose(sample.Position, reference.Position, scale);
            RequireClose(sample.DU, reference.DU, DERIVATIVE_SCALE * scale);
            RequireClose(sample.DV, reference.DV, DERIVATIVE_SCALE * scale);
            RequireClose(sample.DUU, reference.DUU, DERIVATIVE_SCALE * scale);
            RequireClose(sample.DUV, reference.DUV, DERIVATIVE_SCALE * scale);
            RequireClose(sample.DVV, reference.DVV, DERIVATIVE_SCALE * scale);
            const bool isCorner =
                (reference.U == 0.0 || reference.U == 1.0) && (reference.V == 0.0 || reference.V == 1.0);
            if (isCorner == true)
            {
                const usize u = reference.U == 0.0 ? 0 : PATCH_CONTROL_COUNT - 1;
                const usize v = reference.V == 0.0 ? 0 : PATCH_CONTROL_COUNT - 1;
                REQUIRE(AreBitwiseEqual(sample.Position, fixture.Control[u][v]) == true);
            }
        }
        for (usize i = 0; i < fixture.Normals.GetSize(); ++i)
        {
            const NormalReference& reference = fixture.Normals[i];
            INFO("normal at (" << reference.U << ", " << reference.V << ")");
            PatchSample sample;
            REQUIRE(TryEvaluatePatch(patch, PatchParameter{reference.U, reference.V}, sample) == MathStatus::Success);
            Vector3 normal{7, 7, 7};
            const MathStatus status =
                TryPatchNormal(sample, PatchNormalPolicy{reference.MinDerivativeLength, reference.MinSinAngle}, normal);
            REQUIRE(status == MapToMathStatus(reference.Status));
            if (reference.Status == ExpectedStatus::Success)
            {
                REQUIRE(GetDistance(Wide(normal), ToVector(reference.Normal)) <= 1e-6);
            }
            else
            {
                REQUIRE(normal.X == 7.0f);
            }
        }
    }
}

TEST_CASE("G0 shared edges and tessellation bounds hold for G1 samples", "[math][bezier][fixtures]")
{
    for (const SharedEdgeFixture& fixture : SHARED_EDGE_FIXTURES)
    {
        INFO(fixture.Name);
        const BicubicBezierPatch first = MakePatch(PATCH_FIXTURES[fixture.FirstPatch]);
        const BicubicBezierPatch second = MakePatch(PATCH_FIXTURES[fixture.SecondPatch]);
        const float64 scale = std::fmax(GetControlScale(PATCH_FIXTURES[fixture.FirstPatch]),
                                        GetControlScale(PATCH_FIXTURES[fixture.SecondPatch]));
        for (usize i = 0; i < fixture.Samples.GetSize(); ++i)
        {
            const EdgeSampleReference& reference = fixture.Samples[i];
            Vector3 a;
            Vector3 b;
            REQUIRE(TryEvaluatePatch(first, PatchParameter{fixture.FirstU, reference.FirstV}, a) ==
                    MathStatus::Success);
            REQUIRE(TryEvaluatePatch(second, PatchParameter{fixture.SecondU, reference.SecondV}, b) ==
                    MathStatus::Success);
            RequireClose(a, reference.Position, scale);
            RequireClose(b, reference.Position, scale);
            // Corners are stored values, so both patches reproduce them bitwise. Interior edge samples
            // are only Rounded; G4 must emit them once from the canonical boundary instead.
            const bool isCorner = reference.FirstV == 0.0 || reference.FirstV == 1.0;
            if (isCorner == true)
            {
                REQUIRE(a.X == b.X);
                REQUIRE(a.Y == b.Y);
                REQUIRE(a.Z == b.Z);
            }
        }
    }

    for (const PatchFixture& fixture : PATCH_FIXTURES)
    {
        INFO(fixture.Name);
        const BicubicBezierPatch patch = MakePatch(fixture);
        const TessellationReference& bound = fixture.Tessellation;
        const float64 envelope = GetEnvelope(bound.InteriorBound, GetControlScale(fixture));
        float64 maximum = 0;
        for (usize i = 0; i <= PATCH_GRID_COUNT; ++i)
        {
            for (usize j = 0; j <= PATCH_GRID_COUNT; ++j)
            {
                const float64 u = static_cast<float64>(i) / static_cast<float64>(PATCH_GRID_COUNT);
                const float64 v = static_cast<float64>(j) / static_cast<float64>(PATCH_GRID_COUNT);
                maximum = std::fmax(maximum, GetTriangleDeviation(patch, u, v));
            }
        }
        REQUIRE(maximum <= bound.InteriorBound + envelope);
        if (bound.HasTrueMaxDeviation == true)
        {
            REQUIRE(std::fabs(GetTriangleDeviation(patch, bound.TrueMaxU, bound.TrueMaxV) - bound.TrueMaxDeviation) <=
                    envelope);
            REQUIRE(maximum <= bound.TrueMaxDeviation + envelope);
        }
    }
}

TEST_CASE("G0 precision fixtures narrow only relative to their origin", "[math][fixtures]")
{
    for (const PrecisionFixture& fixture : PRECISION_FIXTURES)
    {
        INFO(fixture.Name);
        float64 naiveError = 0;
        float64 relativeExtent = 0;
        float64 absoluteExtent = 0;
        for (usize i = 0; i < CUBIC_CONTROL_COUNT; ++i)
        {
            const Vector3d source = ToVector(fixture.Source[i]);
            Vector3 relative;
            REQUIRE(TryMakeRelative(source, ToVector(fixture.Origin), fixture.MaxAbsComponent, relative) ==
                    MathStatus::Success);
            REQUIRE(AreBitwiseEqual(relative, fixture.Relative[i]) == true);
            const Vector3 naive{static_cast<float32>(source.X),
                                static_cast<float32>(source.Y),
                                static_cast<float32>(source.Z)};
            naiveError = std::fmax(naiveError, GetDistance(Wide(naive), source));
            relativeExtent = std::fmax(relativeExtent, GetMaxAbsComponent(Wide(relative)));
            absoluteExtent = std::fmax(absoluteExtent, GetMaxAbsComponent(source));
        }
        REQUIRE(std::fabs(naiveError - fixture.NaiveNarrowingError) <= 1e-12);
        REQUIRE(fixture.RelativePositionFloor == 2 * GetFloat32Spacing(relativeExtent));
        REQUIRE(fixture.AbsolutePositionFloor == 2 * GetFloat32Spacing(absoluteExtent));
        // Direct narrowing loses more than the whole absolute floor's worth of local detail.
        REQUIRE(naiveError > fixture.RelativePositionFloor);
    }
}
