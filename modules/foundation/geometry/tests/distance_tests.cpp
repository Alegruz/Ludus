// Distance map gates: Certified length and prefix enclosures, inversion residuals,
// zero speed, zero-length runs, boundary policies, revisions and budget exhaustion.

#include "geometry_test_support.hpp"

#include <ludus/foundation/geometry/distance.hpp>
#include <ludus/foundation/math/scalar.hpp>

#include <catch2/catch_test_macros.hpp>

#include <limits>

using namespace ludus::foundation::geometry;
using ludus::foundation::BorrowedView;
using ludus::foundation::core::StaticArray;
namespace fixtures = ludus::geometry_fixtures;
namespace math = ludus::foundation::math;
using math::Vector3;
using math::Vector3d;
using test_support::CORPUS_DISTANCE_ERROR;
using test_support::CORPUS_QUERY_ITERATIONS;

namespace
{
constexpr usize STATIONARY_PATH_SPAN_COUNT = 3;
constexpr usize LOOP_SPAN_COUNT = 4;
// Parameter regression envelope for a resolved distance where the speed is regular.
constexpr float64 REGULAR_PARAMETER_TOLERANCE = 1e-5;
// Near a zero-speed parameter u(s) is ill-conditioned: |du| grows like sqrt(residual).
constexpr float64 SINGULAR_PARAMETER_TOLERANCE = 2e-3;

[[nodiscard]] bool IsZeroSpeedParameter(const fixtures::CubicFixture& fixture, float64 u) noexcept
{
    for (usize i = 0; i < fixture.ZeroSpeedU.GetSize(); ++i)
    {
        if (fixture.ZeroSpeedU[i] == u)
        {
            return true;
        }
    }
    return false;
}

[[nodiscard]] DistanceQuery MakeQuery(float64 distance, BoundaryPolicy policy = BoundaryPolicy::Reject) noexcept
{
    return DistanceQuery
    {
        .Distance = PathDistance{ .Value = distance },
        .Policy = policy,
        .Side = SpanSide::Outgoing,
        .MaxIterations = CORPUS_QUERY_ITERATIONS,
    };
}

[[nodiscard]] PathSpanSource MakeLine(const Vector3d& start, const Vector3d& end) noexcept
{
    PathSpanSource span;
    span.Kind = SpanKind::Line;
    span.Control[0] = start;
    span.Control[3] = end;
    return span;
}

[[nodiscard]] bool IsSameResolution(const DistanceResolution& left, const DistanceResolution& right) noexcept
{
    return left.Parameter.SpanIndex == right.Parameter.SpanIndex && left.Parameter.U == right.Parameter.U &&
           left.ResidualBound == right.ResidualBound;
}
} // namespace

TEST_CASE("Certified length and prefix enclosures contain every corpus reference", "[geometry][distance][fixtures]")
{
    for (const fixtures::CubicFixture& fixture : fixtures::CUBIC_FIXTURES)
    {
        INFO(fixture.Name);
        PathSpanSource span;
        PreparedPath path;
        REQUIRE(test_support::PrepareFixture(fixture, span, path) == GeometryStatus::Success);
        PreparedDistanceMap map;
        REQUIRE(TryBuildDistanceMap(path.GetView(), test_support::MakeDistancePreparation(), map, nullptr) ==
                GeometryStatus::Success);
        const DistanceMapView view = map.GetView();
        REQUIRE(view.Guarantee == GuaranteeClass::Certified);
        REQUIRE(IsStrictGuarantee(view.Guarantee) == true);
        REQUIRE(view.TotalLower <= fixture.ArcLength);
        REQUIRE(fixture.ArcLength <= view.TotalUpper);
        REQUIRE(view.TotalUpper - view.TotalLower <= CORPUS_DISTANCE_ERROR);
        REQUIRE((view.TotalEstimate >= view.TotalLower && view.TotalEstimate <= view.TotalUpper));

        // Leaves partition [0,1] with exact shared boundaries and monotone prefixes.
        REQUIRE(view.Leaves[0].U0 == 0.0);
        REQUIRE(view.Leaves[view.Leaves.GetSize() - 1].U1 == 1.0);
        for (usize i = 1; i < view.Leaves.GetSize(); ++i)
        {
            REQUIRE(view.Leaves[i].U0 == view.Leaves[i - 1].U1);
            REQUIRE(view.Leaves[i].PrefixLower >= view.Leaves[i - 1].PrefixLower);
            REQUIRE(view.Leaves[i].PrefixUpper >= view.Leaves[i - 1].PrefixUpper);
            REQUIRE(view.Leaves[i].PrefixEstimate >= view.Leaves[i - 1].PrefixEstimate);
        }

        for (usize i = 0; i < fixture.Distances.GetSize(); ++i)
        {
            const fixtures::DistanceReference& reference = fixture.Distances[i];
            DistanceBound bound;
            REQUIRE(TryBoundDistance(path.GetView(),
                                     view,
                                     SegmentParameter
                                     {
                                         .SpanIndex = 0,
                                         .U = reference.U,
                                     },
                                     bound) == GeometryStatus::Success);
            // The bound includes every preceding leaf's uncertainty.
            REQUIRE(bound.Lower <= reference.Distance);
            REQUIRE(reference.Distance <= bound.Upper);
            REQUIRE(bound.Upper - bound.Lower <= CORPUS_DISTANCE_ERROR);
            REQUIRE(bound.Guarantee == GuaranteeClass::Certified);
        }
    }
}

TEST_CASE("Distance inversion meets the residual bound across zero-speed parameters", "[geometry][distance][fixtures]")
{
    for (const fixtures::CubicFixture& fixture : fixtures::CUBIC_FIXTURES)
    {
        INFO(fixture.Name);
        PathSpanSource span;
        PreparedPath path;
        REQUIRE(test_support::PrepareFixture(fixture, span, path) == GeometryStatus::Success);
        PreparedDistanceMap map;
        REQUIRE(TryBuildDistanceMap(path.GetView(), test_support::MakeDistancePreparation(), map, nullptr) ==
                GeometryStatus::Success);
        for (usize i = 0; i < fixture.Distances.GetSize(); ++i)
        {
            const fixtures::DistanceReference& reference = fixture.Distances[i];
            INFO(reference.Distance);
            DistanceResolution resolution;
            REQUIRE(TryResolveDistance(path.GetView(), map.GetView(), MakeQuery(reference.Distance), resolution) ==
                    GeometryStatus::Success);
            REQUIRE(resolution.Guarantee == GuaranteeClass::Certified);
            REQUIRE(resolution.ResidualBound <= CORPUS_DISTANCE_ERROR);
            REQUIRE(resolution.Parameter.SpanIndex == 0);
            const float64 tolerance = IsZeroSpeedParameter(fixture, reference.U) == true ? SINGULAR_PARAMETER_TOLERANCE
                                                                                         : REGULAR_PARAMETER_TOLERANCE;
            REQUIRE(math::Abs(resolution.Parameter.U - reference.U) <= tolerance);

            // The residual is consistent with an independent enclosure at the returned parameter.
            DistanceBound bound;
            REQUIRE(TryBoundDistance(path.GetView(), map.GetView(), resolution.Parameter, bound) ==
                    GeometryStatus::Success);
            REQUIRE(bound.Lower - resolution.ResidualBound <= reference.Distance);
            REQUIRE(reference.Distance <= bound.Upper + resolution.ResidualBound);
        }

        // Distances just before and after each zero-speed parameter still resolve with bounded residuals.
        for (usize i = 0; i < fixture.ZeroSpeedU.GetSize(); ++i)
        {
            DistanceBound atRoot;
            REQUIRE(TryBoundDistance(path.GetView(),
                                     map.GetView(),
                                     SegmentParameter{ .SpanIndex = 0, .U = fixture.ZeroSpeedU[i] },
                                     atRoot) == GeometryStatus::Success);
            for (const float64 offset : {-1e-3, 0.0, 1e-3})
            {
                const float64 target = math::Clamp(atRoot.Estimate + offset, 0.0, map.GetView().TotalEstimate);
                DistanceResolution resolution;
                REQUIRE(TryResolveDistance(path.GetView(), map.GetView(), MakeQuery(target), resolution) ==
                        GeometryStatus::Success);
                REQUIRE(resolution.ResidualBound <= CORPUS_DISTANCE_ERROR);
            }
        }
    }
}

TEST_CASE("Path ends, boundary policies and span sides follow the frozen rules", "[geometry][distance]")
{
    StaticArray<PathSpanSource, LOOP_SPAN_COUNT> spans{{
        MakeLine(Vector3d{0.0, 0.0, 0.0}, Vector3d{2.0, 0.0, 0.0}),
        MakeLine(Vector3d{2.0, 0.0, 0.0}, Vector3d{2.0, 2.0, 0.0}),
        MakeLine(Vector3d{2.0, 2.0, 0.0}, Vector3d{0.0, 2.0, 0.0}),
        MakeLine(Vector3d{0.0, 2.0, 0.0}, Vector3d{0.0, 0.0, 0.0}),
    }};
    PathSource source{ .Spans = BorrowedView<const PathSpanSource>(spans.GetData(), spans.GetSize()), .Revision = 5 };
    PreparedPath open;
    REQUIRE(TryPreparePath(source, test_support::MakePreparation(), open, nullptr) == GeometryStatus::Success);
    source.IsClosed = true;
    PreparedPath closed;
    REQUIRE(TryPreparePath(source, test_support::MakePreparation(), closed, nullptr) == GeometryStatus::Success);
    PreparedDistanceMap openMap;
    REQUIRE(TryBuildDistanceMap(open.GetView(), test_support::MakeDistancePreparation(), openMap, nullptr) ==
            GeometryStatus::Success);
    PreparedDistanceMap closedMap;
    REQUIRE(TryBuildDistanceMap(closed.GetView(), test_support::MakeDistancePreparation(), closedMap, nullptr) ==
            GeometryStatus::Success);
    const float64 total = openMap.GetView().TotalEstimate;
    REQUIRE(math::Abs(total - 8.0) <= CORPUS_DISTANCE_ERROR);

    DistanceResolution resolution;
    REQUIRE(TryResolveDistance(open.GetView(), openMap.GetView(), MakeQuery(0.0), resolution) ==
            GeometryStatus::Success);
    REQUIRE((resolution.Parameter.SpanIndex == 0 && resolution.Parameter.U == 0.0));
    REQUIRE(resolution.ResidualBound == 0.0);
    // The terminal endpoint selects the last span at u = 1.
    REQUIRE(TryResolveDistance(open.GetView(), openMap.GetView(), MakeQuery(total), resolution) ==
            GeometryStatus::Success);
    REQUIRE((resolution.Parameter.SpanIndex == 3 && resolution.Parameter.U == 1.0));

    // A stored boundary distance resolves to the exact boundary: outgoing by default, incoming on request.
    const float64 boundary = openMap.GetView().Leaves[1].PrefixEstimate;
    REQUIRE(TryResolveDistance(open.GetView(), openMap.GetView(), MakeQuery(boundary), resolution) ==
            GeometryStatus::Success);
    REQUIRE((resolution.Parameter.SpanIndex == 1 && resolution.Parameter.U == 0.0));
    DistanceQuery incoming = MakeQuery(boundary);
    incoming.Side = SpanSide::Incoming;
    REQUIRE(TryResolveDistance(open.GetView(), openMap.GetView(), incoming, resolution) == GeometryStatus::Success);
    REQUIRE((resolution.Parameter.SpanIndex == 0 && resolution.Parameter.U == 1.0));
    incoming.Distance.Value = 0.0;
    REQUIRE(TryResolveDistance(open.GetView(), openMap.GetView(), incoming, resolution) == GeometryStatus::Success);
    REQUIRE((resolution.Parameter.SpanIndex == 0 && resolution.Parameter.U == 0.0));

    const DistanceResolution sentinel{ .Parameter = { .SpanIndex = 77, .U = 0.5 }, .ResidualBound = 9.0 };
    resolution = sentinel;
    REQUIRE(TryResolveDistance(open.GetView(), openMap.GetView(), MakeQuery(-1.0), resolution) ==
            GeometryStatus::InvalidDomain);
    REQUIRE(TryResolveDistance(open.GetView(), openMap.GetView(), MakeQuery(total + 1.0), resolution) ==
            GeometryStatus::InvalidDomain);
    REQUIRE(TryResolveDistance(open.GetView(), openMap.GetView(), MakeQuery(3.0, BoundaryPolicy::Wrap), resolution) ==
            GeometryStatus::InvalidArgument);
    REQUIRE(TryResolveDistance(open.GetView(), openMap.GetView(), MakeQuery(3.0, BoundaryPolicy::Hold), resolution) ==
            GeometryStatus::InvalidArgument);
    REQUIRE(TryResolveDistance(open.GetView(), openMap.GetView(), MakeQuery(3.0, BoundaryPolicy::Loop), resolution) ==
            GeometryStatus::InvalidArgument);
    REQUIRE(TryResolveDistance(open.GetView(),
                               openMap.GetView(),
                               MakeQuery(std::numeric_limits<float64>::infinity()),
                               resolution) == GeometryStatus::NonFiniteInput);
    REQUIRE(IsSameResolution(resolution, sentinel) == true);

    REQUIRE(TryResolveDistance(open.GetView(), openMap.GetView(), MakeQuery(-1.0, BoundaryPolicy::Clamp), resolution) ==
            GeometryStatus::Success);
    REQUIRE((resolution.Parameter.SpanIndex == 0 && resolution.Parameter.U == 0.0));
    REQUIRE(TryResolveDistance(open.GetView(),
                               openMap.GetView(),
                               MakeQuery(total + 1.0, BoundaryPolicy::Clamp),
                               resolution) == GeometryStatus::Success);
    REQUIRE((resolution.Parameter.SpanIndex == 3 && resolution.Parameter.U == 1.0));

    // Wrap is valid on the closed loop and lands in [0, total).
    DistanceResolution wrapped;
    REQUIRE(TryResolveDistance(closed.GetView(),
                               closedMap.GetView(),
                               MakeQuery(total + 3.0, BoundaryPolicy::Wrap),
                               wrapped) == GeometryStatus::Success);
    DistanceResolution direct;
    REQUIRE(TryResolveDistance(closed.GetView(), closedMap.GetView(), MakeQuery(3.0), direct) ==
            GeometryStatus::Success);
    REQUIRE(wrapped.Parameter.SpanIndex == 1);
    REQUIRE(math::Abs(wrapped.Parameter.U - direct.Parameter.U) <= REGULAR_PARAMETER_TOLERANCE);
    REQUIRE(TryResolveDistance(closed.GetView(), closedMap.GetView(), MakeQuery(-1.0, BoundaryPolicy::Wrap), wrapped) ==
            GeometryStatus::Success);
    REQUIRE(wrapped.Parameter.SpanIndex == 3);
    REQUIRE(
        TryResolveDistance(closed.GetView(), closedMap.GetView(), MakeQuery(total, BoundaryPolicy::Wrap), wrapped) ==
        GeometryStatus::Success);
    REQUIRE((wrapped.Parameter.SpanIndex == 3 && wrapped.Parameter.U == 1.0));
}

TEST_CASE("Zero-length runs resolve to their earliest parameter", "[geometry][distance]")
{
    PathSpanSource stationary;
    stationary.Kind = SpanKind::Cubic;
    for (usize i = 0; i < SPAN_CONTROL_COUNT; ++i)
    {
        stationary.Control[i] = Vector3d{3.0, 0.0, 0.0};
    }
    StaticArray<PathSpanSource, STATIONARY_PATH_SPAN_COUNT> spans{{
        MakeLine(Vector3d{0.0, 0.0, 0.0}, Vector3d{3.0, 0.0, 0.0}),
        stationary,
        MakeLine(Vector3d{3.0, 0.0, 0.0}, Vector3d{6.0, 0.0, 0.0}),
    }};
    const PathSource source{ .Spans = BorrowedView<const PathSpanSource>(spans.GetData(), spans.GetSize()) };
    PreparedPath path;
    REQUIRE(TryPreparePath(source, test_support::MakePreparation(), path, nullptr) == GeometryStatus::Success);
    PreparedDistanceMap map;
    REQUIRE(TryBuildDistanceMap(path.GetView(), test_support::MakeDistancePreparation(), map, nullptr) ==
            GeometryStatus::Success);
    const DistanceMapView view = map.GetView();
    // The stationary span occupies no positive distance interval.
    REQUIRE(view.Leaves[1].SpanIndex == 1);
    REQUIRE((view.Leaves[1].LengthLower == 0.0 && view.Leaves[1].LengthUpper == 0.0));

    DistanceResolution resolution;
    REQUIRE(TryResolveDistance(path.GetView(), view, MakeQuery(view.Leaves[1].PrefixEstimate), resolution) ==
            GeometryStatus::Success);
    REQUIRE((resolution.Parameter.SpanIndex == 1 && resolution.Parameter.U == 0.0));
    // The position is stable even though the direction at a stationary span is undefined.
    Vector3 position;
    REQUIRE(TryEvaluatePath(path.GetView(), resolution.Parameter, position) == GeometryStatus::Success);
    REQUIRE(position == Vector3{3.0F, 0.0F, 0.0F});
    Vector3 tangent;
    REQUIRE(TryEvaluatePathTangent(path.GetView(), SegmentParameter{ .SpanIndex = 1, .U = 0.5 }, 0.0, tangent) ==
            GeometryStatus::Degenerate);

    // An entirely stationary path has a zero-length domain and resolves only s = 0.
    const PathSource still{ .Spans = BorrowedView<const PathSpanSource>(&stationary, 1) };
    REQUIRE(TryPreparePath(still, test_support::MakePreparation(), path, nullptr) == GeometryStatus::Success);
    REQUIRE(TryBuildDistanceMap(path.GetView(), test_support::MakeDistancePreparation(), map, nullptr) ==
            GeometryStatus::Success);
    REQUIRE(map.GetView().TotalEstimate == 0.0);
    REQUIRE(TryResolveDistance(path.GetView(), map.GetView(), MakeQuery(0.0), resolution) == GeometryStatus::Success);
    REQUIRE((resolution.Parameter.SpanIndex == 0 && resolution.Parameter.U == 0.0));
    REQUIRE(TryResolveDistance(path.GetView(), map.GetView(), MakeQuery(1.0), resolution) ==
            GeometryStatus::InvalidDomain);
}

TEST_CASE("Missing and stale caches are reported, never rebuilt", "[geometry][distance]")
{
    PathSpanSource span;
    PreparedPath path;
    REQUIRE(test_support::PrepareFixture(test_support::FindCubic("arch"), span, path) == GeometryStatus::Success);
    DistanceResolution resolution;
    const PreparedDistanceMap empty;
    REQUIRE(empty.IsEmpty() == true);
    REQUIRE(TryResolveDistance(path.GetView(), empty.GetView(), MakeQuery(1.0), resolution) ==
            GeometryStatus::MissingCache);
    DistanceBound bound;
    REQUIRE(TryBoundDistance(path.GetView(), empty.GetView(), SegmentParameter{}, bound) ==
            GeometryStatus::MissingCache);

    PreparedDistanceMap map;
    REQUIRE(TryBuildDistanceMap(path.GetView(), test_support::MakeDistancePreparation(), map, nullptr) ==
            GeometryStatus::Success);
    const PathSource next{ .Spans = BorrowedView<const PathSpanSource>(&span, 1), .Revision = 2 };
    PreparedPath republished;
    REQUIRE(TryPreparePath(next, test_support::MakePreparation(), republished, nullptr) == GeometryStatus::Success);
    REQUIRE(TryResolveDistance(republished.GetView(), map.GetView(), MakeQuery(1.0), resolution) ==
            GeometryStatus::StaleRevision);
    REQUIRE(TryBoundDistance(republished.GetView(), map.GetView(), SegmentParameter{}, bound) ==
            GeometryStatus::StaleRevision);
}

TEST_CASE("Refinement is deterministic and budget exhaustion preserves the active map", "[geometry][distance]")
{
    PathSpanSource span;
    PreparedPath path;
    REQUIRE(test_support::PrepareFixture(test_support::FindCubic("cusp"), span, path) == GeometryStatus::Success);
    PreparedDistanceMap first;
    PreparedDistanceMap second;
    REQUIRE(TryBuildDistanceMap(path.GetView(), test_support::MakeDistancePreparation(), first, nullptr) ==
            GeometryStatus::Success);
    REQUIRE(TryBuildDistanceMap(path.GetView(), test_support::MakeDistancePreparation(), second, nullptr) ==
            GeometryStatus::Success);
    REQUIRE(first.GetView().Leaves.GetSize() == second.GetView().Leaves.GetSize());
    for (usize i = 0; i < first.GetView().Leaves.GetSize(); ++i)
    {
        REQUIRE(first.GetView().Leaves[i].U0 == second.GetView().Leaves[i].U0);
        REQUIRE(first.GetView().Leaves[i].PrefixUpper == second.GetView().Leaves[i].PrefixUpper);
    }
    const usize activeLeafCount = first.GetView().Leaves.GetSize();
    const float64 activeTotal = first.GetView().TotalEstimate;

    const auto requireBudgetFailure = [&](const DistancePreparation& preparation) {
        GeometryDiagnostic diagnostic;
        REQUIRE(TryBuildDistanceMap(path.GetView(), preparation, first, &diagnostic) == GeometryStatus::BudgetExceeded);
        REQUIRE(diagnostic.Code == GeometryStatus::BudgetExceeded);
        REQUIRE(diagnostic.RequestedTolerance == CORPUS_DISTANCE_ERROR);
        REQUIRE(diagnostic.AchievedBound > CORPUS_DISTANCE_ERROR);
        REQUIRE(diagnostic.AchievedGuarantee == GuaranteeClass::Certified);
        REQUIRE(first.GetView().Leaves.GetSize() == activeLeafCount);
        REQUIRE(first.GetView().TotalEstimate == activeTotal);
    };
    DistancePreparation limited = test_support::MakeDistancePreparation();
    limited.Limits.MaxLeaves = 8;
    requireBudgetFailure(limited);
    limited = test_support::MakeDistancePreparation();
    limited.Limits.MaxDepth = 2;
    requireBudgetFailure(limited);
    limited = test_support::MakeDistancePreparation();
    limited.Limits.MaxSubdivisions = 3;
    requireBudgetFailure(limited);
    limited = test_support::MakeDistancePreparation();
    limited.Limits.MaxPreparedBytes = sizeof(DistanceLeaf);
    REQUIRE(TryBuildDistanceMap(path.GetView(), limited, first, nullptr) == GeometryStatus::BudgetExceeded);
    REQUIRE(first.GetView().Leaves.GetSize() == activeLeafCount);
    limited = test_support::MakeDistancePreparation();
    limited.Limits.MaxLeaves = 0;
    REQUIRE(TryBuildDistanceMap(path.GetView(), limited, first, nullptr) == GeometryStatus::BudgetExceeded);
    REQUIRE(TryBuildDistanceMap(PathView{}, test_support::MakeDistancePreparation(), first, nullptr) ==
            GeometryStatus::InvalidArgument);
    REQUIRE(first.GetView().Leaves.GetSize() == activeLeafCount);

    // Inversion with too little work fails without writing its output.
    DistanceQuery query = MakeQuery(1.0);
    query.MaxIterations = 2;
    const DistanceResolution sentinel{ .Parameter = { .SpanIndex = 5, .U = 0.25 }, .ResidualBound = 4.0 };
    DistanceResolution resolution = sentinel;
    REQUIRE(TryResolveDistance(path.GetView(), first.GetView(), query, resolution) == GeometryStatus::BudgetExceeded);
    REQUIRE(IsSameResolution(resolution, sentinel) == true);
    query.MaxIterations = 0;
    REQUIRE(TryResolveDistance(path.GetView(), first.GetView(), query, resolution) == GeometryStatus::BudgetExceeded);
}

TEST_CASE("Uneven Catmull-Rom paths invert across span boundaries", "[geometry][distance][fixtures]")
{
    const fixtures::CatmullRomFixture& fixture = fixtures::CATMULL_ROM_FIXTURES[0];
    StaticArray<Vector3d, fixtures::CATMULL_ROM_WAYPOINT_COUNT> waypoints;
    for (usize i = 0; i < waypoints.GetSize(); ++i)
    {
        waypoints[i] = test_support::ToVector(fixture.Waypoint[i]);
    }
    StaticArray<PathSpanSource, fixtures::CATMULL_ROM_WAYPOINT_COUNT - 1> spans;
    usize spanCount = 0;
    REQUIRE(TryConvertCentripetalCatmullRom(BorrowedView<const Vector3d>(waypoints.GetData(), waypoints.GetSize()),
                                            false,
                                            1e-6,
                                            BorrowedView<PathSpanSource>(spans.GetData(), spans.GetSize()),
                                            spanCount) == GeometryStatus::Success);
    PreparedPath path;
    // Consecutive spans share the waypoint value exactly, so the joints have no gap.
    REQUIRE(TryPreparePath(PathSource{ .Spans = BorrowedView<const PathSpanSource>(spans.GetData(), spanCount) },
                           test_support::MakePreparation(),
                           path,
                           nullptr) == GeometryStatus::Success);
    PreparedDistanceMap map;
    REQUIRE(TryBuildDistanceMap(path.GetView(), test_support::MakeDistancePreparation(), map, nullptr) ==
            GeometryStatus::Success);
    const DistanceMapView view = map.GetView();
    // The chord between consecutive waypoints bounds each span's length from below.
    REQUIRE(view.TotalLower >= 1.0 + 4.0 + 9.0 - CORPUS_DISTANCE_ERROR);
    float64 previousSpanIndex = 0.0;
    for (usize step = 0; step <= 64; ++step)
    {
        const float64 target = view.TotalEstimate * static_cast<float64>(step) / 64.0;
        DistanceResolution resolution;
        REQUIRE(TryResolveDistance(path.GetView(), view, MakeQuery(target), resolution) == GeometryStatus::Success);
        REQUIRE(resolution.ResidualBound <= CORPUS_DISTANCE_ERROR);
        // Resolved parameters advance monotonically through the spans.
        const float64 ordered = static_cast<float64>(resolution.Parameter.SpanIndex) + resolution.Parameter.U;
        REQUIRE(ordered >= previousSpanIndex);
        previousSpanIndex = ordered;
    }
    REQUIRE(previousSpanIndex == 3.0);
}
