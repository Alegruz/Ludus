#pragma once

// Shared test profiles and fixture adapters for FoundationGeometry tests.

#include <ludus/foundation/base/core.h>

#include <ludus/foundation/geometry/distance.hpp>
#include <ludus/foundation/geometry/path.hpp>
#include <ludus/foundation/geometry/status.hpp>

#include <curves_surfaces/reference_fixtures.hpp>

#include <cstring>

namespace ludus::foundation::geometry::test_support
{
namespace fixtures = ludus::geometry_fixtures;

/// Distance gap used by the corpus tests, asset units.
inline constexpr float64 CORPUS_DISTANCE_ERROR = 1e-7;
/// Work limit large enough for every corpus fixture at CORPUS_DISTANCE_ERROR.
inline constexpr usize CORPUS_WORK_LIMIT = 1U << 20U;
/// Inversion work limit: enough bisection steps to reach float64 resolution.
inline constexpr usize CORPUS_QUERY_ITERATIONS = 256;

/// A valid profile with every field inside its ADR 0024 range.
[[nodiscard]] inline ToleranceProfile MakeProfile() noexcept
{
    return ToleranceProfile
    {
        .PositionError = 1e-5,
        .RelativePositionError = 0.0,
        .DistanceError = CORPUS_DISTANCE_ERROR,
        .TangentAngleError = 1e-3,
        .ProjectionGap = 1e-5,
        .ContinuityPosition = 1e-6,
        .ContinuityDerivative = 1e-9,
        .ContinuityAngle = 1e-6,
        .NormalMinDerivativeLength = 1e-6,
        .NormalMinSinAngle = 1e-3,
        .NormalAngleError = 1e-2,
        .CoincidenceDistance = 1e-6,
    };
}

/// Generous limits for corpus tests.
[[nodiscard]] inline WorkLimits MakeLimits() noexcept
{
    return WorkLimits
    {
        .MaxSpans = CORPUS_WORK_LIMIT,
        .MaxLeaves = CORPUS_WORK_LIMIT,
        .MaxDepth = 64,
        .MaxSubdivisions = CORPUS_WORK_LIMIT,
        .MaxIterations = CORPUS_QUERY_ITERATIONS,
        .MaxPreparedBytes = CORPUS_WORK_LIMIT * 256U,
    };
}

[[nodiscard]] inline PathPreparation MakePreparation() noexcept
{
    return PathPreparation{ .Tolerance = MakeProfile(), .Limits = MakeLimits() };
}

[[nodiscard]] inline math::Vector3d ToVector(const fixtures::ControlPoint& point) noexcept
{
    return math::Vector3d{static_cast<float64>(point.X), static_cast<float64>(point.Y), static_cast<float64>(point.Z)};
}

/// Cubic corpus fixture by name; the first fixture when the name is unknown.
[[nodiscard]] inline const fixtures::CubicFixture& FindCubic(const char* name) noexcept
{
    for (const fixtures::CubicFixture& fixture : fixtures::CUBIC_FIXTURES)
    {
        if (std::strcmp(fixture.Name, name) == 0)
        {
            return fixture;
        }
    }
    return fixtures::CUBIC_FIXTURES[0];
}

/// One cubic source span from a corpus fixture's exact float32 controls.
[[nodiscard]] inline PathSpanSource MakeSpan(const fixtures::CubicFixture& fixture) noexcept
{
    PathSpanSource span;
    span.Kind = SpanKind::Cubic;
    for (usize i = 0; i < SPAN_CONTROL_COUNT; ++i)
    {
        span.Control[i] = ToVector(fixture.Control[i]);
    }
    return span;
}

/// Prepare a single-span path from a corpus fixture, at the origin.
[[nodiscard]] inline GeometryStatus
PrepareFixture(const fixtures::CubicFixture& fixture, PathSpanSource& outSpan, PreparedPath& outPath) noexcept
{
    outSpan = MakeSpan(fixture);
    const PathSource source{ .Spans = BorrowedView<const PathSpanSource>(&outSpan, 1), .Revision = 1 };
    return TryPreparePath(source, MakePreparation(), outPath, nullptr);
}

[[nodiscard]] inline DistancePreparation MakeDistancePreparation() noexcept
{
    return DistancePreparation{ .Precision = PrecisionProfile::Strict, .Limits = MakeLimits() };
}
} // namespace ludus::foundation::geometry::test_support
