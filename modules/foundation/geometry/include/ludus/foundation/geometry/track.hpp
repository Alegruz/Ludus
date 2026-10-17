#pragma once

// Prepared scalar tracks over TrackTime (curves and surfaces architecture,
// "Parameter, continuity and timing contracts", phase G2).
//
// A track is a list of keys with strictly increasing times. Each key names the
// interpolation of the span that starts at it: Step, Linear or authored
// Hermite. Hermite slopes are value units per second and may overshoot; the
// runtime never clamps them. Evaluation applies the frozen span-side and
// boundary-policy rules of ADR 0024 and never allocates.

#include <ludus/foundation/base/core.h>

#include <ludus/foundation/containers/array.hpp>
#include <ludus/foundation/containers/borrowed_view.hpp>
#include <ludus/foundation/geometry/status.hpp>
#include <ludus/foundation/math/cubic.hpp>

namespace ludus::foundation::geometry
{
/// @brief Interpolation of the span that starts at a key.
enum class TrackInterpolation : uint8
{
    Step,    ///< Left key's value on [t0, t1), right key's value at t1; zero derivative.
    Linear,  ///< Linear between the two key values.
    Hermite, ///< Cubic from the left key's OutSlope to the right key's InSlope, per second.
};

/// @brief One source key. Slopes are value units per second.
struct TrackKey final
{
    float64 Time = 0.0;      ///< Seconds from the local epoch; strictly increasing across keys.
    float32 Value = 0.0F;    ///< Value at Time.
    float32 InSlope = 0.0F;  ///< Derivative arriving at the key; used when the previous span is Hermite.
    float32 OutSlope = 0.0F; ///< Derivative leaving the key; used when this key's span is Hermite.
    TrackInterpolation Interpolation = TrackInterpolation::Linear; ///< Span starting here; ignored on the last key.
};

/// @brief One prepared span from a key to the next.
struct PreparedTrackSpan final
{
    float64 StartTime = 0.0;                                       ///< Seconds at u = 0.
    float64 Duration = 0.0;                                        ///< Seconds; > 0.
    TrackInterpolation Interpolation = TrackInterpolation::Linear; ///< Span kind.
    math::CubicBezier1 Curve{}; ///< Bezier controls; Step and Linear use Control[0] and Control[3] only.
};

/// @brief Borrowed, allocation-free view of a PreparedTrack.
struct TrackView final
{
    BorrowedView<const PreparedTrackSpan> Spans; ///< Spans in time order; empty for an unprepared track.
    float64 StartTime = 0.0;                     ///< First key time, seconds.
    float64 EndTime = 0.0;                       ///< Last key time, seconds.
};

/// @brief Immutable prepared scalar track; move-only.
class PreparedTrack final
{
public:
    /// Construct an empty track; evaluation reports MissingCache.
    PreparedTrack() noexcept = default;
    /// Transfer the prepared storage; the source becomes empty.
    PreparedTrack(PreparedTrack&& other) noexcept = default;
    /// Release this owner's storage and take the other's; the source becomes empty.
    PreparedTrack& operator=(PreparedTrack&& other) noexcept = default;
    /// Copying is disabled: one owner holds the prepared storage.
    PreparedTrack(const PreparedTrack&) = delete;
    /// Copy assignment is disabled: one owner holds the prepared storage.
    PreparedTrack& operator=(const PreparedTrack&) = delete;
    /// Release the prepared storage; views into it become invalid.
    ~PreparedTrack() = default;

    /// Borrow the prepared spans; invalidated by moving or destroying the owner.
    [[nodiscard]] TrackView GetView() const noexcept;
    /// Whether no track has been prepared into this owner.
    [[nodiscard]] bool IsEmpty() const noexcept;

private:
    friend GeometryStatus TryPrepareTrack(BorrowedView<const TrackKey> keys,
                                          const WorkLimits& limits,
                                          PreparedTrack& outTrack,
                                          GeometryDiagnostic* outDiagnosticOrNull) noexcept;

    core::Array<PreparedTrackSpan> mSpans;
    float64 mStartTime = 0.0;
    float64 mEndTime = 0.0;
};

/// @brief Validate keys and build a prepared track.
///
/// Requires at least two keys within Limits.MaxSpans, finite times, values and
/// slopes, strictly increasing times and known interpolation values. Equal
/// times are rejected: the runtime never resolves an undocumented tie.
/// @param keys Source keys, borrowed for the call only.
/// @param limits MaxSpans (key count) and MaxPreparedBytes apply.
/// @param outTrack Replaced by the new owner only on Success.
/// @param outDiagnosticOrNull Optional; receives the offending key index and property.
/// @return Success; InvalidArgument; NonFiniteInput; InvalidDomain for nonincreasing times; OutOfRange when a
/// Hermite control overflows float32; BudgetExceeded; OutOfMemory. outTrack is replaced only on Success.
[[nodiscard]] GeometryStatus TryPrepareTrack(BorrowedView<const TrackKey> keys,
                                             const WorkLimits& limits,
                                             PreparedTrack& outTrack,
                                             GeometryDiagnostic* outDiagnosticOrNull) noexcept;

/// @brief Track value and derivative per second (Rounded; stored key values are Exact).
struct TrackSample final
{
    float32 Value = 0.0F;               ///< Value at the evaluated time.
    float32 DerivativePerSecond = 0.0F; ///< Value units per second; zero on Step spans and under Hold.
};

/// @brief Evaluate a track at a time under a boundary policy and span side.
///
/// Inside [start, end] every policy evaluates the same value. An exact interior
/// key selects the outgoing span unless side is Incoming; the last key selects
/// the last span at u = 1. Clamp evaluates the nearest endpoint with its
/// one-sided derivative from inside; Hold returns the endpoint value with zero
/// derivative; Loop maps into [start, end); PingPong reflects with period 2D and
/// negates the derivative on the reflected half.
/// @return MissingCache for an empty view; NonFiniteInput; InvalidDomain outside the domain under Reject;
/// InvalidArgument for Wrap or an unknown policy or side; OutOfRange. outSample is unchanged on failure.
[[nodiscard]] GeometryStatus TryEvaluateTrack(const TrackView& track,
                                              TrackTime time,
                                              BoundaryPolicy policy,
                                              SpanSide side,
                                              TrackSample& outSample) noexcept;
} // namespace ludus::foundation::geometry
