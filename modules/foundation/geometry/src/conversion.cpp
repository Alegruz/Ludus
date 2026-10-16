// Hermite and centripetal Catmull-Rom conversion to cubic Bezier spans.
//
// Thanks to Phillip J. Barry and Ronald N. Goldman, "A recursive evaluation
// algorithm for a class of Catmull-Rom splines", SIGGRAPH 1988 (Computer
// Graphics 22(4), pages 199-204): the nonuniform Catmull-Rom tangent at knot i
// is (P_i - P_{i-1})/(t_i - t_{i-1}) - (P_{i+1} - P_{i-1})/(t_{i+1} - t_{i-1})
// + (P_{i+1} - P_i)/(t_{i+1} - t_i), which reproduces their pyramid exactly when
// converted with the Hermite rule below (checked by the G0 corpus generator).
// Thanks to Cem Yuksel, Scott Schaefer and John Keyser, "Parameterization and
// applications of Catmull-Rom curves", Computer-Aided Design 43(7), 2011,
// https://www.cemyuksel.com/research/catmullrom_param/: knot steps are the
// square root of chord length (centripetal). Departure: open endpoints use the
// one-sided chord tangent rather than a phantom point, and coincident
// consecutive waypoints are rejected instead of merged.

#include <ludus/foundation/geometry/path.hpp>

#include <ludus/foundation/math/scalar.hpp>

namespace ludus::foundation::geometry
{
using math::Vector3d;

namespace
{
[[nodiscard]] bool IsFiniteVector(const Vector3d& value) noexcept
{
    return math::IsFinite(value.X) == true && math::IsFinite(value.Y) == true && math::IsFinite(value.Z) == true;
}

[[nodiscard]] Vector3d Add(const Vector3d& left, const Vector3d& right) noexcept
{
    return Vector3d{left.X + right.X, left.Y + right.Y, left.Z + right.Z};
}

[[nodiscard]] Vector3d Subtract(const Vector3d& left, const Vector3d& right) noexcept
{
    return Vector3d{left.X - right.X, left.Y - right.Y, left.Z - right.Z};
}

[[nodiscard]] Vector3d Scale(const Vector3d& value, float64 factor) noexcept
{
    return Vector3d{value.X * factor, value.Y * factor, value.Z * factor};
}

// Hermite rule: B1 = P0 + h*T0/3, B2 = P1 - h*T1/3.
[[nodiscard]] PathSpanSource
MakeHermiteSpan(const Vector3d& start,
                const Vector3d& end,
                const Vector3d& startTangent,
                const Vector3d& endTangent,
                float64 width, // NOLINT(bugprone-easily-swappable-parameters): Hermite rule inputs
                usize sourceElement) noexcept
{
    PathSpanSource span;
    span.Kind = SpanKind::Cubic;
    span.Control[0] = start;
    span.Control[1] = Add(start, Scale(startTangent, width / 3.0));
    span.Control[2] = Subtract(end, Scale(endTangent, width / 3.0));
    span.Control[3] = end;
    span.DomainWidth = width;
    span.SourceElement = sourceElement;
    return span;
}

} // namespace

GeometryStatus TryConvertHermite(BorrowedView<const HermiteWaypoint> waypoints,
                                 bool isClosed,
                                 BorrowedView<PathSpanSource> outSpans,
                                 usize& outSpanCount) noexcept
{
    const usize count = waypoints.GetSize();
    const usize minimumCount = isClosed == true ? 3 : 2;
    if (waypoints.IsValid() == false || outSpans.IsValid() == false || count < minimumCount)
    {
        return GeometryStatus::InvalidArgument;
    }
    for (usize i = 0; i < count; ++i)
    {
        const HermiteWaypoint& waypoint = waypoints[i];
        if (IsFiniteVector(waypoint.Position) == false || IsFiniteVector(waypoint.InTangent) == false ||
            IsFiniteVector(waypoint.OutTangent) == false || math::IsFinite(waypoint.OutgoingWidth) == false)
        {
            return GeometryStatus::NonFiniteInput;
        }
        const bool hasOutgoingSpan = isClosed == true || i + 1 < count;
        if (hasOutgoingSpan == true && waypoint.OutgoingWidth <= 0.0)
        {
            return GeometryStatus::InvalidArgument;
        }
    }
    const usize spanCount = isClosed == true ? count : count - 1;
    if (outSpans.GetSize() < spanCount)
    {
        return GeometryStatus::CapacityExceeded;
    }
    // Compute every span before writing, so a failure leaves outSpans unchanged.
    for (usize i = 0; i < spanCount; ++i)
    {
        const HermiteWaypoint& start = waypoints[i];
        const HermiteWaypoint& end = waypoints[(i + 1) % count];
        const PathSpanSource span =
            MakeHermiteSpan(start.Position, end.Position, start.OutTangent, end.InTangent, start.OutgoingWidth, i);
        if (IsFiniteVector(span.Control[1]) == false || IsFiniteVector(span.Control[2]) == false)
        {
            return GeometryStatus::OutOfRange;
        }
    }
    for (usize i = 0; i < spanCount; ++i)
    {
        const HermiteWaypoint& start = waypoints[i];
        const HermiteWaypoint& end = waypoints[(i + 1) % count];
        outSpans[i] =
            MakeHermiteSpan(start.Position, end.Position, start.OutTangent, end.InTangent, start.OutgoingWidth, i);
    }
    outSpanCount = spanCount;
    return GeometryStatus::Success;
}

GeometryStatus TryConvertCentripetalCatmullRom(BorrowedView<const Vector3d> waypoints,
                                               bool isClosed,
                                               float64 coincidenceDistance,
                                               BorrowedView<PathSpanSource> outSpans,
                                               usize& outSpanCount) noexcept
{
    const usize count = waypoints.GetSize();
    const usize minimumCount = isClosed == true ? 3 : 2;
    if (waypoints.IsValid() == false || outSpans.IsValid() == false || count < minimumCount)
    {
        return GeometryStatus::InvalidArgument;
    }
    if (math::IsFinite(coincidenceDistance) == false)
    {
        return GeometryStatus::NonFiniteInput;
    }
    if (coincidenceDistance <= 0.0)
    {
        return GeometryStatus::InvalidArgument;
    }
    for (usize i = 0; i < count; ++i)
    {
        if (IsFiniteVector(waypoints[i]) == false)
        {
            return GeometryStatus::NonFiniteInput;
        }
    }
    const usize spanCount = isClosed == true ? count : count - 1;
    // Reject coincident consecutive waypoints (including the closing pair) before any division.
    for (usize i = 0; i < spanCount; ++i)
    {
        const float64 chord = math::Length(Subtract(waypoints[(i + 1) % count], waypoints[i]));
        if (math::IsFinite(chord) == false)
        {
            return GeometryStatus::OutOfRange;
        }
        if (chord <= coincidenceDistance)
        {
            return GeometryStatus::InvalidDomain;
        }
    }
    if (outSpans.GetSize() < spanCount)
    {
        return GeometryStatus::CapacityExceeded;
    }

    // Knot width of the span from waypoint i to its successor: sqrt(|P_{i+1} - P_i|).
    const auto computeWidth = [&](usize spanIndex) noexcept -> float64 {
        return math::Sqrt(math::Length(Subtract(waypoints[(spanIndex + 1) % count], waypoints[spanIndex])));
    };
    // Tangent at waypoint i per knot unit.
    const auto computeTangent = [&](usize index) noexcept -> Vector3d {
        const bool hasPrevious = isClosed == true || index > 0;
        const bool hasNext = isClosed == true || index + 1 < count;
        const usize previous = (index + count - 1) % count;
        const usize next = (index + 1) % count;
        if (hasPrevious == false)
        {
            return Scale(Subtract(waypoints[next], waypoints[index]), 1.0 / computeWidth(index));
        }
        if (hasNext == false)
        {
            return Scale(Subtract(waypoints[index], waypoints[previous]), 1.0 / computeWidth(previous));
        }
        const float64 widthBefore = computeWidth(previous);
        const float64 widthAfter = computeWidth(index);
        const Vector3d before = Scale(Subtract(waypoints[index], waypoints[previous]), 1.0 / widthBefore);
        const Vector3d across = Scale(Subtract(waypoints[next], waypoints[previous]), 1.0 / (widthBefore + widthAfter));
        const Vector3d after = Scale(Subtract(waypoints[next], waypoints[index]), 1.0 / widthAfter);
        return Add(Subtract(before, across), after);
    };

    for (usize i = 0; i < spanCount; ++i)
    {
        const PathSpanSource span = MakeHermiteSpan(waypoints[i],
                                                    waypoints[(i + 1) % count],
                                                    computeTangent(i),
                                                    computeTangent((i + 1) % count),
                                                    computeWidth(i),
                                                    i);
        if (IsFiniteVector(span.Control[1]) == false || IsFiniteVector(span.Control[2]) == false)
        {
            return GeometryStatus::OutOfRange;
        }
    }
    for (usize i = 0; i < spanCount; ++i)
    {
        outSpans[i] = MakeHermiteSpan(waypoints[i],
                                      waypoints[(i + 1) % count],
                                      computeTangent(i),
                                      computeTangent((i + 1) % count),
                                      computeWidth(i),
                                      i);
    }
    outSpanCount = spanCount;
    return GeometryStatus::Success;
}
} // namespace ludus::foundation::geometry
