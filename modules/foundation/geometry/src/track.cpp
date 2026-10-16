// Prepared scalar tracks: validation, span lookup and boundary policies (see track.hpp).

#include <ludus/foundation/geometry/track.hpp>

#include "internal/arc_length.hpp"

#include <ludus/foundation/math/scalar.hpp>

namespace ludus::foundation::geometry
{
using math::CubicBezier1;

namespace
{
// Largest finite float32, the range of narrowed derivatives.
constexpr float64 FLOAT32_LARGEST = 3.4028234663852886e38;

[[nodiscard]] GeometryStatus Fail(GeometryDiagnostic* outDiagnosticOrNull,
                                  GeometryStatus status,
                                  usize keyIndex,
                                  const char* property,
                                  float64 time) noexcept
{
    if (outDiagnosticOrNull != nullptr)
    {
        *outDiagnosticOrNull = GeometryDiagnostic
        {
            .Code = status,
            .SourceElement = keyIndex,
            .Property = property,
            .Domain = DomainKind::TrackTime,
            .RangeStart = time,
            .RangeEnd = time,
        };
    }
    return status;
}

[[nodiscard]] bool IsKnownInterpolation(TrackInterpolation interpolation) noexcept
{
    return interpolation == TrackInterpolation::Step || interpolation == TrackInterpolation::Linear ||
           interpolation == TrackInterpolation::Hermite;
}

// Span and local parameter of a time inside [start, end] under the span-side rule.
struct SpanLocation final
{
    usize SpanIndex = 0;
    float64 U = 0.0;
};

[[nodiscard]] SpanLocation LocateTime(const TrackView& track, float64 time, SpanSide side) noexcept
{
    const usize spanCount = track.Spans.GetSize();
    if (time >= track.EndTime)
    {
        // The terminal endpoint selects the last span at u = 1.
        return SpanLocation{ .SpanIndex = spanCount - 1, .U = 1.0 };
    }
    // Last span whose start time is at or before time.
    usize low = 0;
    usize high = spanCount - 1;
    while (low < high)
    {
        const usize middle = low + (high - low + 1) / 2;
        if (track.Spans[middle].StartTime <= time)
        {
            low = middle;
        }
        else
        {
            high = middle - 1;
        }
    }
    const PreparedTrackSpan& span = track.Spans[low];
    if (time == span.StartTime && side == SpanSide::Incoming && low > 0)
    {
        return SpanLocation{ .SpanIndex = low - 1, .U = 1.0 };
    }
    const float64 u = math::Clamp((time - span.StartTime) / span.Duration, 0.0, 1.0);
    return SpanLocation{ .SpanIndex = low, .U = u };
}

[[nodiscard]] GeometryStatus TryEvaluateSpan(const PreparedTrackSpan& span, float64 u, TrackSample& outSample) noexcept
{
    const float32 startValue = span.Curve.Control[0];
    const float32 endValue = span.Curve.Control[3];
    TrackSample sample;
    switch (span.Interpolation)
    {
        case TrackInterpolation::Step:
            sample.Value = u < 1.0 ? startValue : endValue;
            sample.DerivativePerSecond = 0.0F;
            break;
        case TrackInterpolation::Linear: {
            const float64 slope = (static_cast<float64>(endValue) - static_cast<float64>(startValue)) / span.Duration;
            if (math::Abs(slope) > FLOAT32_LARGEST)
            {
                return GeometryStatus::OutOfRange;
            }
            sample.Value =
                static_cast<float32>(math::Lerp(static_cast<float64>(startValue), static_cast<float64>(endValue), u));
            sample.DerivativePerSecond = static_cast<float32>(slope);
            break;
        }
        case TrackInterpolation::Hermite: {
            math::CubicSample1 cubic;
            const GeometryStatus status = internal::ToGeometryStatus(math::TryEvaluateCubic(span.Curve, u, cubic));
            if (status != GeometryStatus::Success)
            {
                return status;
            }
            // Kernel derivatives are per normalized u; divide by the span duration for per-second units.
            const float64 slope = static_cast<float64>(cubic.FirstDerivative) / span.Duration;
            if (math::Abs(slope) > FLOAT32_LARGEST)
            {
                return GeometryStatus::OutOfRange;
            }
            sample.Value = cubic.Position;
            sample.DerivativePerSecond = static_cast<float32>(slope);
            break;
        }
    }
    outSample = sample;
    return GeometryStatus::Success;
}

// mod(offset, period) in [0, period) for a positive period; rounding at the seam maps to 0.
[[nodiscard]] float64 ComputeModulo(float64 offset, float64 period) noexcept
{
    float64 remainder = offset - period * math::Floor(offset / period);
    if ((remainder >= 0.0 && remainder < period) == false)
    {
        remainder = 0.0;
    }
    return remainder;
}
} // namespace

TrackView PreparedTrack::GetView() const noexcept
{
    return TrackView
    {
        .Spans = BorrowedView<const PreparedTrackSpan>(mSpans.GetData(), mSpans.GetSize()),
        .StartTime = mStartTime,
        .EndTime = mEndTime,
    };
}

bool PreparedTrack::IsEmpty() const noexcept
{
    return mSpans.IsEmpty();
}

GeometryStatus TryPrepareTrack(BorrowedView<const TrackKey> keys,
                               const WorkLimits& limits,
                               PreparedTrack& outTrack,
                               GeometryDiagnostic* outDiagnosticOrNull) noexcept
{
    const usize keyCount = keys.GetSize();
    if (keys.IsValid() == false || keyCount < 2)
    {
        return Fail(outDiagnosticOrNull, GeometryStatus::InvalidArgument, 0, "Keys", 0.0);
    }
    if (keyCount > limits.MaxSpans)
    {
        return Fail(outDiagnosticOrNull, GeometryStatus::BudgetExceeded, 0, "Keys", 0.0);
    }
    const usize spanCount = keyCount - 1;
    if (spanCount * sizeof(PreparedTrackSpan) > limits.MaxPreparedBytes)
    {
        return Fail(outDiagnosticOrNull, GeometryStatus::BudgetExceeded, 0, "Limits.MaxPreparedBytes", 0.0);
    }
    for (usize i = 0; i < keyCount; ++i)
    {
        const TrackKey& key = keys[i];
        const bool isFinite = math::IsFinite(key.Time) == true && math::IsFinite(key.Value) == true &&
                              math::IsFinite(key.InSlope) == true && math::IsFinite(key.OutSlope) == true;
        if (isFinite == false)
        {
            return Fail(outDiagnosticOrNull, GeometryStatus::NonFiniteInput, i, "Keys", key.Time);
        }
        if (IsKnownInterpolation(key.Interpolation) == false)
        {
            return Fail(outDiagnosticOrNull, GeometryStatus::InvalidArgument, i, "Keys.Interpolation", key.Time);
        }
        if (i > 0 && (key.Time > keys[i - 1].Time) == false)
        {
            return Fail(outDiagnosticOrNull, GeometryStatus::InvalidDomain, i, "Keys.Time", key.Time);
        }
    }

    PreparedTrack candidate;
    if (candidate.mSpans.TryResize(spanCount) == false)
    {
        return Fail(outDiagnosticOrNull, GeometryStatus::OutOfMemory, 0, "Keys", 0.0);
    }
    for (usize i = 0; i < spanCount; ++i)
    {
        const TrackKey& start = keys[i];
        const TrackKey& end = keys[i + 1];
        const float64 duration = end.Time - start.Time;
        if ((duration > 0.0 && math::IsFinite(duration) == true) == false)
        {
            return Fail(outDiagnosticOrNull, GeometryStatus::OutOfRange, i + 1, "Keys.Time", end.Time);
        }
        PreparedTrackSpan& span = candidate.mSpans[i];
        span.StartTime = start.Time;
        span.Duration = duration;
        span.Interpolation = start.Interpolation;
        span.Curve = CubicBezier1{{start.Value, start.Value, end.Value, end.Value}};
        if (start.Interpolation == TrackInterpolation::Hermite)
        {
            const math::CubicHermite1 hermite
            {
                .Start = start.Value,
                .End = end.Value,
                .StartDerivative = start.OutSlope,
                .EndDerivative = end.InSlope,
            };
            const GeometryStatus status =
                internal::ToGeometryStatus(math::TryFromHermite(hermite, duration, span.Curve));
            if (status != GeometryStatus::Success)
            {
                return Fail(outDiagnosticOrNull, status, i, "Keys.OutSlope", start.Time);
            }
        }
    }
    candidate.mStartTime = keys[0].Time;
    candidate.mEndTime = keys[keyCount - 1].Time;
    outTrack = Move(candidate);
    return GeometryStatus::Success;
}

GeometryStatus TryEvaluateTrack(const TrackView& track,
                                TrackTime time,
                                BoundaryPolicy policy,
                                SpanSide side,
                                TrackSample& outSample) noexcept
{
    const bool isKnownSide = side == SpanSide::Outgoing || side == SpanSide::Incoming;
    const bool isTrackPolicy = policy == BoundaryPolicy::Reject || policy == BoundaryPolicy::Clamp ||
                               policy == BoundaryPolicy::Hold || policy == BoundaryPolicy::Loop ||
                               policy == BoundaryPolicy::PingPong;
    if (isKnownSide == false || isTrackPolicy == false)
    {
        return GeometryStatus::InvalidArgument;
    }
    if (track.Spans.IsEmpty() == true)
    {
        return GeometryStatus::MissingCache;
    }
    const float64 seconds = time.Seconds;
    if (math::IsFinite(seconds) == false)
    {
        return GeometryStatus::NonFiniteInput;
    }
    const float64 start = track.StartTime;
    const float64 end = track.EndTime;
    const float64 duration = end - start;
    float64 mapped = seconds;
    float32 derivativeSign = 1.0F;
    bool isHeld = false;
    if (seconds < start || seconds > end)
    {
        switch (policy)
        {
            case BoundaryPolicy::Reject:
                return GeometryStatus::InvalidDomain;
            case BoundaryPolicy::Clamp:
                mapped = seconds < start ? start : end;
                break;
            case BoundaryPolicy::Hold:
                mapped = seconds < start ? start : end;
                isHeld = true;
                break;
            case BoundaryPolicy::Loop:
                mapped = start + ComputeModulo(seconds - start, duration);
                break;
            case BoundaryPolicy::PingPong: {
                const float64 phase = ComputeModulo(seconds - start, 2.0 * duration);
                if (phase <= duration)
                {
                    mapped = start + phase;
                }
                else
                {
                    mapped = start + (2.0 * duration - phase);
                    derivativeSign = -1.0F;
                }
                break;
            }
            case BoundaryPolicy::Wrap:
                return GeometryStatus::InvalidArgument;
        }
    }
    const SpanLocation location = LocateTime(track, mapped, side);
    TrackSample sample;
    const GeometryStatus status = TryEvaluateSpan(track.Spans[location.SpanIndex], location.U, sample);
    if (status != GeometryStatus::Success)
    {
        return status;
    }
    sample.DerivativePerSecond = isHeld == true ? 0.0F : sample.DerivativePerSecond * derivativeSign;
    outSample = sample;
    return GeometryStatus::Success;
}
} // namespace ludus::foundation::geometry
