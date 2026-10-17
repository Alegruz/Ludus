// Prepared paths, source conversion and continuity against the G0 reference corpus.

#include "geometry_test_support.hpp"

#include <ludus/foundation/geometry/path.hpp>
#include <ludus/foundation/math/scalar.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <limits>

using namespace ludus::foundation::geometry;
using ludus::foundation::BorrowedView;
using ludus::foundation::core::StaticArray;
namespace fixtures = ludus::geometry_fixtures;
namespace math = ludus::foundation::math;
using math::Vector3;
using math::Vector3d;

namespace
{
constexpr usize UNEVEN_SPAN_COUNT = 3;
constexpr usize LOOP_WAYPOINT_COUNT = 4;
constexpr usize JOINED_SPAN_COUNT = 2;
constexpr usize HERMITE_WAYPOINT_COUNT = 3;
// Rounded-sample regression envelope relative to the larger of the reference magnitude and 1 (not a bound).
constexpr float64 SAMPLE_TOLERANCE = 1e-6;
// Float64 conversion arithmetic regression envelope (not a bound).
constexpr float64 CONVERSION_TOLERANCE = 1e-12;

[[nodiscard]] bool IsClose(float64 actual, float64 expected, float64 tolerance) noexcept
{
    return math::Abs(actual - expected) <= tolerance * math::Max(1.0, math::Abs(expected));
}

[[nodiscard]] bool IsClose(const Vector3& actual, const fixtures::ReferencePoint& expected, float64 tolerance) noexcept
{
    return IsClose(static_cast<float64>(actual.X), expected.X, tolerance) == true &&
           IsClose(static_cast<float64>(actual.Y), expected.Y, tolerance) == true &&
           IsClose(static_cast<float64>(actual.Z), expected.Z, tolerance) == true;
}

[[nodiscard]] bool IsClose(const Vector3d& actual, const fixtures::ReferencePoint& expected, float64 tolerance) noexcept
{
    return IsClose(actual.X, expected.X, tolerance) == true && IsClose(actual.Y, expected.Y, tolerance) == true &&
           IsClose(actual.Z, expected.Z, tolerance) == true;
}

[[nodiscard]] bool IsSame(const Vector3& actual, const fixtures::ControlPoint& expected) noexcept
{
    return actual.X == expected.X && actual.Y == expected.Y && actual.Z == expected.Z;
}

[[nodiscard]] PathSpanSource MakeLine(const Vector3d& start, const Vector3d& end) noexcept
{
    PathSpanSource span;
    span.Kind = SpanKind::Line;
    span.Control[0] = start;
    span.Control[3] = end;
    return span;
}
} // namespace

TEST_CASE("Prepared corpus spans keep exact endpoints and match reference samples", "[geometry][path][fixtures]")
{
    for (const fixtures::CubicFixture& fixture : fixtures::CUBIC_FIXTURES)
    {
        INFO(fixture.Name);
        PathSpanSource span;
        PreparedPath path;
        REQUIRE(test_support::PrepareFixture(fixture, span, path) == GeometryStatus::Success);
        const PathView view = path.GetView();
        REQUIRE(view.Spans.GetSize() == 1);
        REQUIRE(view.Joints.GetSize() == 0);
        REQUIRE(view.PositionFloor == fixture.PositionFloor);
        for (usize i = 0; i < SPAN_CONTROL_COUNT; ++i)
        {
            REQUIRE(IsSame(view.Spans[0].Control[i], fixture.Control[i]) == true);
        }

        Vector3 position;
        REQUIRE(TryEvaluatePath(view,
                                SegmentParameter
                                {
                                    .SpanIndex = 0,
                                    .U = 0.0,
                                },
                                position) == GeometryStatus::Success);
        REQUIRE(IsSame(position, fixture.Control[0]) == true);
        REQUIRE(TryEvaluatePath(view,
                                SegmentParameter
                                {
                                    .SpanIndex = 0,
                                    .U = 1.0,
                                },
                                position) == GeometryStatus::Success);
        REQUIRE(IsSame(position, fixture.Control[SPAN_CONTROL_COUNT - 1]) == true);

        for (usize i = 0; i < fixture.Samples.GetSize(); ++i)
        {
            const fixtures::CubicSampleReference& reference = fixture.Samples[i];
            math::CubicSample3 sample;
            REQUIRE(TryEvaluatePath(view, SegmentParameter{ .SpanIndex = 0, .U = reference.U }, sample) ==
                    GeometryStatus::Success);
            REQUIRE(IsClose(sample.Position, reference.Position, SAMPLE_TOLERANCE) == true);
            REQUIRE(IsClose(sample.FirstDerivative, reference.FirstDerivative, SAMPLE_TOLERANCE) == true);
            REQUIRE(IsClose(sample.SecondDerivative, reference.SecondDerivative, SAMPLE_TOLERANCE) == true);
        }

        // The control box is Exact and encloses every sample.
        math::Aabb3 bounds;
        REQUIRE(TryGetSpanControlBounds(view, 0, bounds) == GeometryStatus::Success);
        for (usize i = 0; i < fixture.Samples.GetSize(); ++i)
        {
            const fixtures::ReferencePoint& point = fixture.Samples[i].Position;
            REQUIRE((point.X >= static_cast<float64>(bounds.Min.X) && point.X <= static_cast<float64>(bounds.Max.X)));
            REQUIRE((point.Y >= static_cast<float64>(bounds.Min.Y) && point.Y <= static_cast<float64>(bounds.Max.Y)));
            REQUIRE((point.Z >= static_cast<float64>(bounds.Min.Z) && point.Z <= static_cast<float64>(bounds.Max.Z)));
        }
        REQUIRE(TryGetSpanControlBounds(view, 1, bounds) == GeometryStatus::InvalidDomain);
    }
}

TEST_CASE("Zero-speed parameters keep their position but have no tangent", "[geometry][path][fixtures]")
{
    usize checkedCount = 0;
    for (const fixtures::CubicFixture& fixture : fixtures::CUBIC_FIXTURES)
    {
        INFO(fixture.Name);
        PathSpanSource span;
        PreparedPath path;
        REQUIRE(test_support::PrepareFixture(fixture, span, path) == GeometryStatus::Success);
        const PathView view = path.GetView();
        for (usize i = 0; i < fixture.ZeroSpeedU.GetSize(); ++i)
        {
            const SegmentParameter parameter{ .SpanIndex = 0, .U = fixture.ZeroSpeedU[i] };
            Vector3 position;
            REQUIRE(TryEvaluatePath(view, parameter, position) == GeometryStatus::Success);
            const Vector3 sentinel{7.0F, 8.0F, 9.0F};
            Vector3 tangent = sentinel;
            // Roots that are not dyadic leave a float64-rounding residual speed far below 1e-6 per u.
            REQUIRE(TryEvaluatePathTangent(view, parameter, 1e-6, tangent) == GeometryStatus::Degenerate);
            REQUIRE(tangent == sentinel);
            ++checkedCount;
        }
    }
    REQUIRE(checkedCount == 5);

    // The cusp's speed is exactly zero, so even a zero threshold reports Degenerate.
    PathSpanSource span;
    PreparedPath path;
    REQUIRE(test_support::PrepareFixture(test_support::FindCubic("cusp"), span, path) == GeometryStatus::Success);
    Vector3 tangent;
    REQUIRE(TryEvaluatePathTangent(path.GetView(), SegmentParameter{ .SpanIndex = 0, .U = 0.5 }, 0.0, tangent) ==
            GeometryStatus::Degenerate);
    REQUIRE(TryEvaluatePathTangent(path.GetView(), SegmentParameter{ .SpanIndex = 0, .U = 0.25 }, 0.0, tangent) ==
            GeometryStatus::Success);
    REQUIRE(TryEvaluatePathTangent(path.GetView(), SegmentParameter{ .SpanIndex = 0, .U = 0.25 }, -1.0, tangent) ==
            GeometryStatus::InvalidArgument);

    REQUIRE(test_support::PrepareFixture(test_support::FindCubic("line_uniform"), span, path) ==
            GeometryStatus::Success);
    REQUIRE(TryEvaluatePathTangent(path.GetView(), SegmentParameter{ .SpanIndex = 0, .U = 0.5 }, 0.0, tangent) ==
            GeometryStatus::Success);
    REQUIRE(tangent == Vector3{1.0F, 0.0F, 0.0F});
}

TEST_CASE("Far-origin controls narrow exactly only after subtracting the chunk origin", "[geometry][path][fixtures]")
{
    const fixtures::PrecisionFixture& fixture = fixtures::PRECISION_FIXTURES[0];
    PathSpanSource span;
    for (usize i = 0; i < SPAN_CONTROL_COUNT; ++i)
    {
        span.Control[i] = Vector3d{fixture.Source[i].X, fixture.Source[i].Y, fixture.Source[i].Z};
    }
    const Vector3d origin{fixture.Origin.X, fixture.Origin.Y, fixture.Origin.Z};
    PathSource source{ .Origin = origin, .Spans = BorrowedView<const PathSpanSource>(&span, 1), .Revision = 3 };

    PreparedPath path;
    REQUIRE(TryPreparePath(source, test_support::MakePreparation(), path, nullptr) == GeometryStatus::Success);
    const PathView view = path.GetView();
    REQUIRE(view.Origin == origin);
    REQUIRE(view.PositionFloor == fixture.RelativePositionFloor);
    for (usize i = 0; i < SPAN_CONTROL_COUNT; ++i)
    {
        REQUIRE(IsSame(view.Spans[0].Control[i], fixture.Relative[i]) == true);
    }

    // Without the origin the float32 floor (256 units) exceeds the requested position error.
    source.Origin = Vector3d{};
    source.Revision = 4;
    GeometryDiagnostic diagnostic;
    REQUIRE(TryPreparePath(source, test_support::MakePreparation(), path, &diagnostic) ==
            GeometryStatus::UnsupportedPrecision);
    REQUIRE(diagnostic.Code == GeometryStatus::UnsupportedPrecision);
    REQUIRE(diagnostic.AchievedBound >= fixture.AbsolutePositionFloor);
    REQUIRE(diagnostic.RequestedTolerance == test_support::MakeProfile().PositionError);
    REQUIRE(path.GetView().Revision == 3);

    // A relative tolerance at a declared asset scale can admit the coarse floor.
    PathPreparation coarse = test_support::MakePreparation();
    coarse.Tolerance.RelativePositionError = 0.5;
    source.AssetScale = 1024.0;
    REQUIRE(TryPreparePath(source, coarse, path, nullptr) == GeometryStatus::Success);
    REQUIRE(path.GetView().EffectivePositionError == 512.0);
}

TEST_CASE("Invalid sources fail without replacing the prepared path", "[geometry][path]")
{
    const fixtures::CubicFixture& fixture = test_support::FindCubic("parabola");
    PathSpanSource span;
    PreparedPath path;
    REQUIRE(test_support::PrepareFixture(fixture, span, path) == GeometryStatus::Success);
    const PathPreparation preparation = test_support::MakePreparation();

    const auto requireUnchanged = [&]() {
        const PathView view = path.GetView();
        REQUIRE(view.Revision == 1);
        REQUIRE(view.Spans.GetSize() == 1);
        REQUIRE(IsSame(view.Spans[0].Control[1], fixture.Control[1]) == true);
    };

    StaticArray<PathSpanSource, JOINED_SPAN_COUNT> spans{
        {test_support::MakeSpan(fixture), test_support::MakeSpan(fixture)}};
    PathSource source{ .Spans = BorrowedView<const PathSpanSource>(spans.GetData(), spans.GetSize()), .Revision = 9 };
    GeometryDiagnostic diagnostic;
    // The second span starts at the origin, not where the first ends.
    REQUIRE(TryPreparePath(source, preparation, path, &diagnostic) == GeometryStatus::InvalidTopology);
    REQUIRE(diagnostic.AchievedBound > preparation.Tolerance.ContinuityPosition);
    requireUnchanged();

    REQUIRE(TryPreparePath(PathSource{}, preparation, path, nullptr) == GeometryStatus::InvalidArgument);
    requireUnchanged();

    spans[1].Control[2].Y = std::numeric_limits<float64>::quiet_NaN();
    REQUIRE(TryPreparePath(source, preparation, path, nullptr) == GeometryStatus::NonFiniteInput);
    requireUnchanged();

    spans[1] = test_support::MakeSpan(fixture);
    spans[1].Control[1].X = 1e39;
    REQUIRE(TryPreparePath(source, preparation, path, nullptr) == GeometryStatus::OutOfRange);
    requireUnchanged();

    spans[1] = test_support::MakeSpan(fixture);
    spans[1].DomainWidth = 0.0;
    REQUIRE(TryPreparePath(source, preparation, path, nullptr) == GeometryStatus::InvalidArgument);
    requireUnchanged();

    PathPreparation limited = preparation;
    limited.Limits.MaxSpans = 1;
    REQUIRE(TryPreparePath(source, limited, path, nullptr) == GeometryStatus::BudgetExceeded);
    limited = preparation;
    limited.Limits.MaxPreparedBytes = sizeof(PreparedSpan);
    REQUIRE(TryPreparePath(source, limited, path, nullptr) == GeometryStatus::BudgetExceeded);
    limited = preparation;
    limited.Tolerance.DistanceError = -1.0;
    REQUIRE(TryPreparePath(source, limited, path, nullptr) == GeometryStatus::InvalidArgument);
    requireUnchanged();

    Vector3 position{1.0F, 2.0F, 3.0F};
    REQUIRE(TryEvaluatePath(path.GetView(), SegmentParameter{ .SpanIndex = 1, .U = 0.0 }, position) ==
            GeometryStatus::InvalidDomain);
    REQUIRE(TryEvaluatePath(path.GetView(), SegmentParameter{ .SpanIndex = 0, .U = 1.5 }, position) ==
            GeometryStatus::InvalidDomain);
    REQUIRE(TryEvaluatePath(path.GetView(),
                            SegmentParameter{ .SpanIndex = 0, .U = std::numeric_limits<float64>::quiet_NaN() },
                            position) == GeometryStatus::NonFiniteInput);
    REQUIRE(TryEvaluatePath(PathView{}, SegmentParameter{}, position) == GeometryStatus::InvalidDomain);
    REQUIRE(position == Vector3{1.0F, 2.0F, 3.0F});
}

TEST_CASE("Lines evaluate linearly and joints are classified with domain-scaled derivatives", "[geometry][path]")
{
    StaticArray<PathSpanSource, JOINED_SPAN_COUNT> spans{{
        MakeLine(Vector3d{0.0, 0.0, 0.0}, Vector3d{2.0, 0.0, 0.0}),
        MakeLine(Vector3d{2.0, 0.0, 0.0}, Vector3d{2.0, 3.0, 0.0}),
    }};
    const PathSource source{ .Spans = BorrowedView<const PathSpanSource>(spans.GetData(), spans.GetSize()) };
    PreparedPath path;
    REQUIRE(TryPreparePath(source, test_support::MakePreparation(), path, nullptr) == GeometryStatus::Success);
    const PathView view = path.GetView();

    math::CubicSample3 sample;
    REQUIRE(TryEvaluatePath(view, SegmentParameter{ .SpanIndex = 0, .U = 0.25 }, sample) == GeometryStatus::Success);
    REQUIRE(sample.Position == Vector3{0.5F, 0.0F, 0.0F});
    REQUIRE(sample.FirstDerivative == Vector3{2.0F, 0.0F, 0.0F});
    REQUIRE(sample.SecondDerivative == Vector3{});
    REQUIRE(view.Spans[0].Control[1] == view.Spans[0].Control[0]);

    REQUIRE(view.Joints.GetSize() == 1);
    const JointContinuity& corner = view.Joints[0];
    REQUIRE(corner.IsC0 == true);
    REQUIRE(corner.IsG1 == false);
    REQUIRE(corner.IsC1 == false);
    REQUIRE(IsClose(corner.DirectionAngle, math::kHalfPi, CONVERSION_TOLERANCE) == true);

    // Two collinear lines of different widths are C1 only after domain scaling.
    spans[1] = MakeLine(Vector3d{2.0, 0.0, 0.0}, Vector3d{6.0, 0.0, 0.0});
    REQUIRE(TryPreparePath(source, test_support::MakePreparation(), path, nullptr) == GeometryStatus::Success);
    REQUIRE(path.GetView().Joints[0].IsG1 == true);
    REQUIRE(path.GetView().Joints[0].IsC1 == false);
    spans[1].DomainWidth = 2.0;
    REQUIRE(TryPreparePath(source, test_support::MakePreparation(), path, nullptr) == GeometryStatus::Success);
    REQUIRE(path.GetView().Joints[0].IsC1 == true);
    REQUIRE(path.GetView().Joints[0].IsC2 == true);
}

TEST_CASE("Centripetal Catmull-Rom uses nonuniform knots on uneven chords", "[geometry][path][fixtures]")
{
    const fixtures::CatmullRomFixture& fixture = fixtures::CATMULL_ROM_FIXTURES[0];
    REQUIRE(std::strcmp(fixture.Name, "centripetal_uneven") == 0);
    StaticArray<Vector3d, fixtures::CATMULL_ROM_WAYPOINT_COUNT> waypoints;
    for (usize i = 0; i < waypoints.GetSize(); ++i)
    {
        waypoints[i] = test_support::ToVector(fixture.Waypoint[i]);
    }
    StaticArray<PathSpanSource, UNEVEN_SPAN_COUNT> spans;
    usize spanCount = 0;
    REQUIRE(TryConvertCentripetalCatmullRom(BorrowedView<const Vector3d>(waypoints.GetData(), waypoints.GetSize()),
                                            false,
                                            1e-6,
                                            BorrowedView<PathSpanSource>(spans.GetData(), spans.GetSize()),
                                            spanCount) == GeometryStatus::Success);
    REQUIRE(spanCount == UNEVEN_SPAN_COUNT);
    // Knot steps are sqrt(chord): chords 1, 4, 9 give 1, 2, 3, so the knots are 0, 1, 3, 6.
    float64 knot = 0.0;
    for (usize i = 0; i < spanCount; ++i)
    {
        REQUIRE(knot == fixture.Knot[i]);
        knot += spans[i].DomainWidth;
        REQUIRE(spans[i].SourceElement == i);
    }
    REQUIRE(knot == fixture.Knot[fixtures::CATMULL_ROM_WAYPOINT_COUNT - 1]);

    const PathSpanSource& middle = spans[1];
    REQUIRE(middle.DomainWidth == fixture.SpanWidth);
    for (usize i = 0; i < SPAN_CONTROL_COUNT; ++i)
    {
        REQUIRE(IsClose(middle.Control[i], fixture.SpanControl[i], CONVERSION_TOLERANCE) == true);
    }
    // Hermite rule: tangents are recovered as 3(B1 - B0)/h and 3(B3 - B2)/h per knot unit.
    const float64 scale = 3.0 / middle.DomainWidth;
    const Vector3d startTangent{(middle.Control[1].X - middle.Control[0].X) * scale,
                                (middle.Control[1].Y - middle.Control[0].Y) * scale,
                                0.0};
    const Vector3d endTangent{(middle.Control[3].X - middle.Control[2].X) * scale,
                              (middle.Control[3].Y - middle.Control[2].Y) * scale,
                              0.0};
    REQUIRE(IsClose(startTangent, fixture.StartTangent, CONVERSION_TOLERANCE) == true);
    REQUIRE(IsClose(endTangent, fixture.EndTangent, CONVERSION_TOLERANCE) == true);

    // The converted path is C1 in the knot domain at every interior waypoint.
    PreparedPath path;
    const PathSource source{ .Spans = BorrowedView<const PathSpanSource>(spans.GetData(), spanCount) };
    PathPreparation preparation = test_support::MakePreparation();
    // Classification compares float32-rounded controls, so allow their relative rounding.
    preparation.Tolerance.ContinuityDerivative = 1e-6;
    REQUIRE(TryPreparePath(source, preparation, path, nullptr) == GeometryStatus::Success);
    for (usize i = 0; i < path.GetView().Joints.GetSize(); ++i)
    {
        REQUIRE(path.GetView().Joints[i].IsC1 == true);
        REQUIRE(path.GetView().Joints[i].IsG1 == true);
    }
}

TEST_CASE("Coincident waypoints and closed duplicates are rejected", "[geometry][path][fixtures]")
{
    const fixtures::CatmullRomFixture& fixture = fixtures::CATMULL_ROM_FIXTURES[1];
    REQUIRE(std::strcmp(fixture.Name, "duplicate_waypoint") == 0);
    StaticArray<Vector3d, fixtures::CATMULL_ROM_WAYPOINT_COUNT> waypoints;
    for (usize i = 0; i < waypoints.GetSize(); ++i)
    {
        waypoints[i] = test_support::ToVector(fixture.Waypoint[i]);
    }
    StaticArray<PathSpanSource, LOOP_WAYPOINT_COUNT> spans;
    spans[0].DomainWidth = 42.0;
    usize spanCount = 99;
    const BorrowedView<PathSpanSource> output(spans.GetData(), spans.GetSize());
    REQUIRE(TryConvertCentripetalCatmullRom(BorrowedView<const Vector3d>(waypoints.GetData(), waypoints.GetSize()),
                                            false,
                                            1e-6,
                                            output,
                                            spanCount) == GeometryStatus::InvalidDomain);
    REQUIRE(spans[0].DomainWidth == 42.0);
    REQUIRE(spanCount == 99);

    // A closed loop of four corners converts to four spans that close exactly.
    StaticArray<Vector3d, LOOP_WAYPOINT_COUNT> loop{{
        Vector3d{0.0, 0.0, 0.0},
        Vector3d{4.0, 0.0, 0.0},
        Vector3d{4.0, 4.0, 0.0},
        Vector3d{0.0, 4.0, 0.0},
    }};
    const BorrowedView<const Vector3d> loopView(loop.GetData(), loop.GetSize());
    REQUIRE(TryConvertCentripetalCatmullRom(loopView, true, 1e-6, output, spanCount) == GeometryStatus::Success);
    REQUIRE(spanCount == LOOP_WAYPOINT_COUNT);
    REQUIRE(spans[LOOP_WAYPOINT_COUNT - 1].Control[3] == loop[0]);
    PreparedPath path;
    PathSource source{ .Spans = BorrowedView<const PathSpanSource>(spans.GetData(), spanCount), .IsClosed = true };
    PathPreparation preparation = test_support::MakePreparation();
    // Classification compares float32-rounded controls, so allow their relative rounding.
    preparation.Tolerance.ContinuityDerivative = 1e-6;
    REQUIRE(TryPreparePath(source, preparation, path, nullptr) == GeometryStatus::Success);
    REQUIRE(path.GetView().IsClosed == true);
    REQUIRE(path.GetView().Joints.GetSize() == LOOP_WAYPOINT_COUNT);
    REQUIRE(path.GetView().Joints[LOOP_WAYPOINT_COUNT - 1].IsC1 == true);

    // A closed source must not store its first waypoint again at the end.
    loop[3] = loop[0];
    REQUIRE(TryConvertCentripetalCatmullRom(loopView, true, 1e-6, output, spanCount) == GeometryStatus::InvalidDomain);
    REQUIRE(TryConvertCentripetalCatmullRom(BorrowedView<const Vector3d>(loop.GetData(), 2),
                                            true,
                                            1e-6,
                                            output,
                                            spanCount) == GeometryStatus::InvalidArgument);
    REQUIRE(TryConvertCentripetalCatmullRom(loopView, false, 0.0, output, spanCount) ==
            GeometryStatus::InvalidArgument);
    REQUIRE(TryConvertCentripetalCatmullRom(BorrowedView<const Vector3d>(waypoints.GetData(), 2),
                                            false,
                                            1e-6,
                                            BorrowedView<PathSpanSource>(spans.GetData(), 0),
                                            spanCount) == GeometryStatus::CapacityExceeded);

    // An open path whose last span does not return to its start rejects closure at preparation.
    source.Spans = BorrowedView<const PathSpanSource>(spans.GetData(), 2);
    REQUIRE(TryPreparePath(source, preparation, path, nullptr) == GeometryStatus::InvalidTopology);
}

TEST_CASE("Hermite waypoints convert with explicit domain widths", "[geometry][path]")
{
    StaticArray<HermiteWaypoint, HERMITE_WAYPOINT_COUNT> waypoints{{
        HermiteWaypoint{ .Position = {0.0, 0.0, 0.0}, .OutTangent = {3.0, 0.0, 0.0}, .OutgoingWidth = 2.0 },
        HermiteWaypoint
        {
            .Position = {4.0, 0.0, 0.0},
            .InTangent = {6.0, 0.0, 0.0},
            .OutTangent = {0.0, 3.0, 0.0},
            .OutgoingWidth = 1.0,
        },
        HermiteWaypoint{ .Position = {4.0, 4.0, 0.0}, .InTangent = {0.0, 3.0, 0.0}, .OutgoingWidth = 4.0 },
    }};
    StaticArray<PathSpanSource, HERMITE_WAYPOINT_COUNT> spans;
    usize spanCount = 0;
    const BorrowedView<const HermiteWaypoint> input(waypoints.GetData(), waypoints.GetSize());
    const BorrowedView<PathSpanSource> output(spans.GetData(), spans.GetSize());
    REQUIRE(TryConvertHermite(input, false, output, spanCount) == GeometryStatus::Success);
    REQUIRE(spanCount == 2);
    // B1 = P0 + h*T0/3 = (2, 0, 0) and B2 = P1 - h*T1/3 = (0, 0, 0) for h = 2.
    REQUIRE(spans[0].Control[1] == Vector3d{2.0, 0.0, 0.0});
    REQUIRE(spans[0].Control[2] == Vector3d{0.0, 0.0, 0.0});
    REQUIRE(spans[0].DomainWidth == 2.0);
    REQUIRE(spans[1].Control[1] == Vector3d{4.0, 1.0, 0.0});
    REQUIRE(spans[1].Control[2] == Vector3d{4.0, 3.0, 0.0});

    REQUIRE(TryConvertHermite(input, true, output, spanCount) == GeometryStatus::Success);
    REQUIRE(spanCount == 3);
    REQUIRE(spans[2].Control[3] == waypoints[0].Position);
    REQUIRE(spans[2].DomainWidth == 4.0);

    waypoints[1].OutgoingWidth = 0.0;
    REQUIRE(TryConvertHermite(input, false, output, spanCount) == GeometryStatus::InvalidArgument);
    waypoints[1].OutgoingWidth = 1.0;
    REQUIRE(TryConvertHermite(input, true, BorrowedView<PathSpanSource>(spans.GetData(), 2), spanCount) ==
            GeometryStatus::CapacityExceeded);
    waypoints[2].InTangent.Z = std::numeric_limits<float64>::infinity();
    REQUIRE(TryConvertHermite(input, false, output, spanCount) == GeometryStatus::NonFiniteInput);
}
