// Certified chord/control-polygon length enclosures (see internal/arc_length.hpp).

#include "internal/arc_length.hpp"

namespace ludus::foundation::geometry::internal
{
namespace
{
using math::MakePointEnclosure;
using math::MathStatus;

// A cubic blossom takes one parameter per degree.
constexpr usize BLOSSOM_ARGUMENT_COUNT = 3;
// Spatial axes of a control point.
constexpr usize AXIS_COUNT = 3;

[[nodiscard]] ControlBox MakePointBox(const Vector3& point) noexcept
{
    return ControlBox
    {
        .X = MakePointEnclosure(static_cast<float64>(point.X)),
        .Y = MakePointEnclosure(static_cast<float64>(point.Y)),
        .Z = MakePointEnclosure(static_cast<float64>(point.Z)),
    };
}

// (1 - t) * start + t * end for one axis, enclosing the exact value for exact t.
// NOLINTBEGIN(bugprone-easily-swappable-parameters): interpolation endpoints and weights share one type.
[[nodiscard]] MathStatus
TryInterpolateAxis(Enclosure start, Enclosure end, Enclosure oneMinusT, Enclosure t, Enclosure& outValue) noexcept
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    Enclosure startPart;
    MathStatus status = math::TryMultiplyOutward(oneMinusT, start, startPart);
    Enclosure endPart;
    if (status == MathStatus::Success)
    {
        status = math::TryMultiplyOutward(t, end, endPart);
    }
    if (status == MathStatus::Success)
    {
        status = math::TryAddOutward(startPart, endPart, outValue);
    }
    return status;
}

[[nodiscard]] MathStatus
TryInterpolateBox(const ControlBox& start, const ControlBox& end, float64 t, ControlBox& outBox) noexcept
{
    Enclosure oneMinusT;
    MathStatus status = math::TrySubtractOutward(MakePointEnclosure(1.0), MakePointEnclosure(t), oneMinusT);
    const Enclosure parameter = MakePointEnclosure(t);
    ControlBox result;
    if (status == MathStatus::Success)
    {
        status = TryInterpolateAxis(start.X, end.X, oneMinusT, parameter, result.X);
    }
    if (status == MathStatus::Success)
    {
        status = TryInterpolateAxis(start.Y, end.Y, oneMinusT, parameter, result.Y);
    }
    if (status == MathStatus::Success)
    {
        status = TryInterpolateAxis(start.Z, end.Z, oneMinusT, parameter, result.Z);
    }
    if (status == MathStatus::Success)
    {
        outBox = result;
    }
    return status;
}

// Blossom f(first, second, third) of a cubic with exact controls.
[[nodiscard]] MathStatus TryEvaluateBlossom(const StaticArray<ControlBox, SPAN_CONTROL_COUNT>& control,
                                            float64 first,
                                            float64 second,
                                            float64 third,
                                            ControlBox& outPoint) noexcept
{
    StaticArray<ControlBox, SPAN_CONTROL_COUNT> level = control;
    const StaticArray<float64, BLOSSOM_ARGUMENT_COUNT> parameters{{first, second, third}};
    MathStatus status = MathStatus::Success;
    for (usize round = 0; round < parameters.GetSize() && status == MathStatus::Success; ++round)
    {
        const usize count = SPAN_CONTROL_COUNT - 1 - round;
        for (usize i = 0; i < count && status == MathStatus::Success; ++i)
        {
            status = TryInterpolateBox(level[i], level[i + 1], parameters[round], level[i]);
        }
    }
    if (status == MathStatus::Success)
    {
        outPoint = level[0];
    }
    return status;
}

// Enclosure of the distance between two exact points inside the boxes.
[[nodiscard]] MathStatus
TryEncloseDistance(const ControlBox& start, const ControlBox& end, Enclosure& outDistance) noexcept
{
    const StaticArray<Enclosure, AXIS_COUNT> startAxes{{start.X, start.Y, start.Z}};
    const StaticArray<Enclosure, AXIS_COUNT> endAxes{{end.X, end.Y, end.Z}};
    Enclosure sum = MakePointEnclosure(0.0);
    MathStatus status = MathStatus::Success;
    for (usize axis = 0; axis < startAxes.GetSize() && status == MathStatus::Success; ++axis)
    {
        Enclosure difference;
        status = math::TrySubtractOutward(endAxes[axis], startAxes[axis], difference);
        Enclosure square;
        if (status == MathStatus::Success)
        {
            status = math::TrySquareOutward(difference, square);
        }
        if (status == MathStatus::Success)
        {
            status = math::TryAddOutward(sum, square, sum);
        }
    }
    if (status == MathStatus::Success)
    {
        status = math::TrySquareRootOutward(sum, outDistance);
    }
    return status;
}

// Chord lower bound and control-polygon upper bound of one piece.
[[nodiscard]] MathStatus TryEnclosePieceLength(const StaticArray<ControlBox, SPAN_CONTROL_COUNT>& control,
                                               Enclosure& outLength) noexcept
{
    Enclosure chord;
    MathStatus status = TryEncloseDistance(control[0], control[SPAN_CONTROL_COUNT - 1], chord);
    Enclosure polygon = MakePointEnclosure(0.0);
    for (usize i = 0; i + 1 < SPAN_CONTROL_COUNT && status == MathStatus::Success; ++i)
    {
        Enclosure edge;
        status = TryEncloseDistance(control[i], control[i + 1], edge);
        if (status == MathStatus::Success)
        {
            status = math::TryAddOutward(polygon, edge, polygon);
        }
    }
    if (status == MathStatus::Success)
    {
        // The exact length lies between the exact chord and the exact polygon length.
        outLength = Enclosure{ .Lower = chord.Lower, .Upper = polygon.Upper };
    }
    return status;
}
} // namespace

GeometryStatus ToGeometryStatus(math::MathStatus status) noexcept
{
    switch (status)
    {
        case MathStatus::Success:
            return GeometryStatus::Success;
        case MathStatus::NonFiniteInput:
            return GeometryStatus::NonFiniteInput;
        case MathStatus::InvalidArgument:
            // A kernel parameter outside its domain (ADR 0024 boundary mapping).
            return GeometryStatus::InvalidDomain;
        case MathStatus::Degenerate:
            return GeometryStatus::Degenerate;
        case MathStatus::IllConditioned:
            return GeometryStatus::IllConditioned;
        case MathStatus::OutOfRange:
            return GeometryStatus::OutOfRange;
        case MathStatus::SizeMismatch:
            return GeometryStatus::InvalidArgument;
    }
    return GeometryStatus::InvalidArgument;
}

GeometryStatus TryEncloseSubspanControls(const PreparedSpan& span,
                                         float64 a,
                                         float64 b,
                                         StaticArray<ControlBox, SPAN_CONTROL_COUNT>& outControls) noexcept
{
    StaticArray<ControlBox, SPAN_CONTROL_COUNT> exact;
    for (usize i = 0; i < SPAN_CONTROL_COUNT; ++i)
    {
        exact[i] = MakePointBox(span.Control[i]);
    }
    if (a == 0.0 && b == 1.0)
    {
        outControls = exact;
        return GeometryStatus::Success;
    }
    StaticArray<ControlBox, SPAN_CONTROL_COUNT> result;
    MathStatus status = MathStatus::Success;
    if (span.Kind == SpanKind::Line)
    {
        // A line's subsegment is its endpoints; the inner controls repeat them, as in PreparedSpan.
        status = TryInterpolateBox(exact[0], exact[SPAN_CONTROL_COUNT - 1], a, result[0]);
        if (status == MathStatus::Success)
        {
            status = TryInterpolateBox(exact[0], exact[SPAN_CONTROL_COUNT - 1], b, result[SPAN_CONTROL_COUNT - 1]);
        }
        result[1] = result[0];
        result[2] = result[SPAN_CONTROL_COUNT - 1];
    }
    else
    {
        status = TryEvaluateBlossom(exact, a, a, a, result[0]);
        if (status == MathStatus::Success)
        {
            status = TryEvaluateBlossom(exact, a, a, b, result[1]);
        }
        if (status == MathStatus::Success)
        {
            status = TryEvaluateBlossom(exact, a, b, b, result[2]);
        }
        if (status == MathStatus::Success)
        {
            status = TryEvaluateBlossom(exact, b, b, b, result[3]);
        }
    }
    if (status != MathStatus::Success)
    {
        return ToGeometryStatus(status);
    }
    outControls = result;
    return GeometryStatus::Success;
}

GeometryStatus
TryEncloseArcLength(const PreparedSpan& span, float64 a, float64 b, usize pieceCount, Enclosure& outLength) noexcept
{
    LUDUS_ASSERT(pieceCount > 0 && a <= b);
    Enclosure total = MakePointEnclosure(0.0);
    const float64 width = b - a;
    float64 pieceStart = a;
    for (usize piece = 0; piece < pieceCount; ++piece)
    {
        // Consecutive pieces share their exact boundary value, so they partition [a, b].
        const float64 pieceEnd = piece + 1 == pieceCount
                                     ? b
                                     : a + width * (static_cast<float64>(piece + 1) / static_cast<float64>(pieceCount));
        StaticArray<ControlBox, SPAN_CONTROL_COUNT> control;
        GeometryStatus status = TryEncloseSubspanControls(span, pieceStart, pieceEnd, control);
        Enclosure length;
        if (status == GeometryStatus::Success)
        {
            status = ToGeometryStatus(TryEnclosePieceLength(control, length));
        }
        if (status == GeometryStatus::Success)
        {
            status = ToGeometryStatus(math::TryAddOutward(total, length, total));
        }
        if (status != GeometryStatus::Success)
        {
            return status;
        }
        pieceStart = pieceEnd;
    }
    outLength = total;
    return GeometryStatus::Success;
}
} // namespace ludus::foundation::geometry::internal
