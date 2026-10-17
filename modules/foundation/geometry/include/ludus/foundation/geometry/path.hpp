#pragma once

// Prepared spatial paths (curves and surfaces architecture, phase G2).
//
// A source path is a list of line or cubic Bezier spans in float64 coordinates.
// TryPreparePath validates it against a ToleranceProfile, subtracts the chunk
// origin in float64, narrows controls to float32 and classifies the continuity
// of every joint. The resulting PreparedPath is immutable; PathView borrows it
// for allocation-free sampling. Hermite and centripetal Catmull-Rom sources are
// converted to cubic spans first. Distance caches live in distance.hpp.

#include <ludus/foundation/base/core.h>

#include <ludus/foundation/containers/array.hpp>
#include <ludus/foundation/containers/borrowed_view.hpp>
#include <ludus/foundation/containers/static_array.hpp>
#include <ludus/foundation/geometry/status.hpp>
#include <ludus/foundation/math/cubic.hpp>
#include <ludus/foundation/math/geometry.hpp>
#include <ludus/foundation/math/vector.hpp>

namespace ludus::foundation::geometry
{
/// Controls stored for every span; a line uses Control[0] and Control[3].
inline constexpr usize SPAN_CONTROL_COUNT = 4;

/// @brief Polynomial kind of one span.
enum class SpanKind : uint8
{
    Line,  ///< Linear segment from Control[0] to Control[3]; Control[1] and Control[2] are ignored on input.
    Cubic, ///< Cubic Bezier span with four controls.
};

/// @brief One source span in float64 asset coordinates.
struct PathSpanSource final
{
    SpanKind Kind = SpanKind::Cubic; ///< Line or cubic.
    core::StaticArray<math::Vector3d, SPAN_CONTROL_COUNT>
        Control{};             ///< Asset-space controls; endpoints are [0] and [3].
    float64 DomainWidth = 1.0; ///< Width of the source domain (knot or other units) used to scale continuity checks.
    usize SourceElement = 0;   ///< Source waypoint or span index reported by diagnostics and inspection.
};

/// @brief Whole source path for TryPreparePath.
struct PathSource final
{
    math::Vector3d Origin{};                  ///< Float64 chunk origin subtracted before narrowing.
    BorrowedView<const PathSpanSource> Spans; ///< At least one span, borrowed for the call only.
    bool IsClosed = false;    ///< The last span ends where the first starts; enables BoundaryPolicy::Wrap.
    float64 AssetScale = 0.0; ///< Declared asset scale for RelativePositionError; finite and >= 0.
    uint64 Revision = 0;      ///< Owner-assigned snapshot revision copied into caches built from this path.
};

/// @brief Profile and limits for TryPreparePath.
struct PathPreparation final
{
    ToleranceProfile Tolerance{}; ///< Validated as a whole; copied into the prepared path.
    WorkLimits Limits{};          ///< MaxSpans and MaxPreparedBytes apply.
};

/// @brief Continuity classification of one joint, a threshold decision on Rounded values.
///
/// Derivatives are scaled by each span's DomainWidth before comparison, so raw
/// handles of spans with different widths are never compared directly.
struct JointContinuity final
{
    float64 PositionGap = 0.0;                ///< Distance between the two stored endpoints, asset units.
    float64 DirectionAngle = 0.0;             ///< Angle between first derivatives, radians; 0 when either is zero.
    float64 DerivativeDifference = 0.0;       ///< Relative difference of domain-scaled first derivatives.
    float64 SecondDerivativeDifference = 0.0; ///< Relative difference of domain-scaled second derivatives.
    bool IsC0 = false;                        ///< PositionGap <= ContinuityPosition.
    bool IsG1 = false;                        ///< Both directions defined and DirectionAngle <= ContinuityAngle.
    bool IsC1 = false;                        ///< DerivativeDifference <= ContinuityDerivative.
    bool IsC2 = false;                        ///< IsC1 and SecondDerivativeDifference <= ContinuityDerivative.
};

/// @brief One prepared span with chunk-relative float32 controls.
struct PreparedSpan final
{
    SpanKind Kind = SpanKind::Cubic; ///< Line or cubic.
    /// Chunk-relative controls. A line repeats its endpoints in [1] and [2], so the control hull is the segment.
    core::StaticArray<math::Vector3, SPAN_CONTROL_COUNT> Control{};
    float64 DomainWidth = 1.0; ///< Source domain width used for continuity scaling.
    usize SourceElement = 0;   ///< Source mapping copied from PathSpanSource.
};

/// @brief Borrowed, allocation-free view of a PreparedPath.
///
/// The view does not own storage; it is valid while its PreparedPath is alive
/// and unmodified. A default view has no spans.
struct PathView final
{
    BorrowedView<const PreparedSpan> Spans;     ///< Prepared spans in path order.
    BorrowedView<const JointContinuity> Joints; ///< Joint i follows span i; a closed path's last joint closes it.
    math::Vector3d Origin{};                    ///< Float64 chunk origin of every control.
    bool IsClosed = false;                      ///< Whether Wrap is valid.
    uint64 Revision = 0;                        ///< Owner-assigned revision from PathSource.
    float64 PositionFloor = 0.0;                ///< ADR 0024 floor of the prepared controls (Exact).
    float64 EffectivePositionError = 0.0;       ///< max(PositionError, RelativePositionError x AssetScale).
    ToleranceProfile Tolerance{};               ///< Profile the path was prepared with, including thresholds.
};

/// @brief Immutable prepared path owner; move-only.
///
/// Created only by TryPreparePath. Sampling borrows GetView() and never
/// allocates or mutates the owner. Single owner; external synchronization is
/// required to replace it while readers hold views.
class PreparedPath final
{
public:
    /// Construct an empty owner with no spans.
    PreparedPath() noexcept = default;
    /// Transfer the prepared storage; the source becomes empty.
    PreparedPath(PreparedPath&& other) noexcept = default;
    /// Release this owner's storage and take the other's; the source becomes empty.
    PreparedPath& operator=(PreparedPath&& other) noexcept = default;
    /// Copying is disabled: one owner holds the prepared storage.
    PreparedPath(const PreparedPath&) = delete;
    /// Copy assignment is disabled: one owner holds the prepared storage.
    PreparedPath& operator=(const PreparedPath&) = delete;
    /// Release the prepared storage; views into it become invalid.
    ~PreparedPath() = default;

    /// Borrow the prepared data; the view is invalidated by moving or destroying the owner.
    [[nodiscard]] PathView GetView() const noexcept;
    /// Whether no path has been prepared into this owner.
    [[nodiscard]] bool IsEmpty() const noexcept;

private:
    friend GeometryStatus TryPreparePath(const PathSource& source,
                                         const PathPreparation& preparation,
                                         PreparedPath& outPath,
                                         GeometryDiagnostic* outDiagnosticOrNull) noexcept;

    core::Array<PreparedSpan> mSpans;
    core::Array<JointContinuity> mJoints;
    math::Vector3d mOrigin{};
    bool mIsClosed = false;
    uint64 mRevision = 0;
    float64 mPositionFloor = 0.0;
    float64 mEffectivePositionError = 0.0;
    ToleranceProfile mTolerance{};
};

/// @brief Validate a source path and build a new prepared owner.
///
/// Checks: a valid tolerance profile; at least one span within Limits.MaxSpans;
/// finite controls, origin and positive domain widths; a known span kind;
/// consecutive spans (and the closing joint of a closed path) whose stored
/// endpoints are within ContinuityPosition; the float32 position floor of the
/// narrowed controls and their narrowing error against the effective position
/// error; and the prepared byte size against Limits.MaxPreparedBytes.
/// @param source Source spans and origin, borrowed for the call only.
/// @param preparation Tolerance profile and limits; the profile is copied into the path.
/// @param outPath Replaced by the new owner only on Success.
/// @param outDiagnosticOrNull Optional; receives the first failure's details.
/// @return Success; NonFiniteInput; InvalidArgument; InvalidTopology for a gap; UnsupportedPrecision when the
/// effective position error is below the floor or narrowing error; OutOfRange when a relative control
/// exceeds float32; BudgetExceeded; OutOfMemory. outPath is replaced only on Success.
[[nodiscard]] GeometryStatus TryPreparePath(const PathSource& source,
                                            const PathPreparation& preparation,
                                            PreparedPath& outPath,
                                            GeometryDiagnostic* outDiagnosticOrNull) noexcept;

/// @brief Evaluate a chunk-relative position at a segment parameter (Rounded; endpoints Exact).
/// @return InvalidDomain for an empty view, a span index out of range or U outside [0,1]; NonFiniteInput for a
/// nonfinite U; OutOfRange. outPosition is unchanged on failure.
[[nodiscard]] GeometryStatus
TryEvaluatePath(const PathView& path, SegmentParameter parameter, math::Vector3& outPosition) noexcept;
/// @brief Evaluate position and first/second derivatives per normalized u, with the position overload's contract.
[[nodiscard]] GeometryStatus
TryEvaluatePath(const PathView& path, SegmentParameter parameter, math::CubicSample3& outSample) noexcept;

/// @brief Unit tangent dP/du / |dP/du| at a segment parameter (Rounded).
/// @param path Borrowed prepared path.
/// @param parameter Span index and u in [0,1].
/// @param outTangent Unit direction written only on Success.
/// @param minimumSpeed Asset units per unit u; finite and >= 0. The direction is undefined at or below it.
/// @return Degenerate when |dP/du| <= minimumSpeed (for example at a cusp or a zero-speed endpoint), plus the
/// TryEvaluatePath statuses and InvalidArgument for a negative minimumSpeed. outTangent is unchanged on failure.
[[nodiscard]] GeometryStatus TryEvaluatePathTangent(const PathView& path,
                                                    SegmentParameter parameter,
                                                    float64 minimumSpeed,
                                                    math::Vector3& outTangent) noexcept;

/// @brief Axis-aligned box of a span's stored controls, which encloses the exact span (Exact).
/// @return InvalidDomain for an out-of-range span index; outBounds is unchanged on failure.
[[nodiscard]] GeometryStatus
TryGetSpanControlBounds(const PathView& path, usize spanIndex, math::Aabb3& outBounds) noexcept;

/// @brief Hermite waypoint for TryConvertHermite; tangents are per unit of the span domain.
struct HermiteWaypoint final
{
    math::Vector3d Position{};   ///< Asset-space waypoint position.
    math::Vector3d InTangent{};  ///< Derivative arriving at the waypoint, per domain unit.
    math::Vector3d OutTangent{}; ///< Derivative leaving the waypoint, per domain unit.
    float64 OutgoingWidth = 1.0; ///< Domain width of the span leaving this waypoint; finite and > 0.
};

/// @brief Convert Hermite waypoints to cubic spans: B1 = P0 + h*T0/3 and B2 = P1 - h*T1/3.
///
/// An open path of n waypoints yields n - 1 spans; a closed path yields n
/// spans, the last returning to the first waypoint. The last waypoint's
/// OutgoingWidth is used only when closed. Spans carry their width as DomainWidth
/// and their starting waypoint index as SourceElement. Arithmetic is float64 (Rounded).
/// @param waypoints Source waypoints, borrowed for the call only.
/// @param isClosed Whether the last waypoint connects back to the first.
/// @param outSpans Caller-owned range; capacity is checked before anything is written.
/// @param outSpanCount Number of spans written, set only on Success.
/// @return InvalidArgument for fewer than 2 waypoints (3 when closed) or a nonpositive width; NonFiniteInput;
/// OutOfRange for an overflowing control; CapacityExceeded. Outputs are unchanged on failure.
[[nodiscard]] GeometryStatus TryConvertHermite(BorrowedView<const HermiteWaypoint> waypoints,
                                               bool isClosed,
                                               BorrowedView<PathSpanSource> outSpans,
                                               usize& outSpanCount) noexcept;

/// @brief Convert waypoints to cubic spans with centripetal Catmull-Rom (knot step sqrt of chord length).
///
/// Interior tangents use the nonuniform Barry-Goldman formula; an open path's
/// endpoints use the one-sided chord tangent. A closed path wraps neighbors and
/// must not store its first waypoint again at the end. Spans carry their knot
/// width as DomainWidth and their starting waypoint index as SourceElement.
/// Arithmetic is float64 (Rounded).
/// @param waypoints Source waypoints in asset units, borrowed for the call only.
/// @param isClosed Whether the path wraps from the last waypoint to the first.
/// @param outSpans Caller-owned range; capacity is checked before anything is written.
/// @param outSpanCount Number of spans written, set only on Success.
/// @param coincidenceDistance Asset units; finite and > 0. Consecutive waypoints (including the closing pair)
/// at or below this distance are rejected.
/// @return InvalidArgument for fewer than 2 waypoints (3 when closed) or a bad coincidence distance;
/// InvalidDomain for coincident consecutive waypoints; NonFiniteInput; OutOfRange; CapacityExceeded.
/// Outputs are unchanged on failure.
[[nodiscard]] GeometryStatus TryConvertCentripetalCatmullRom(BorrowedView<const math::Vector3d> waypoints,
                                                             bool isClosed,
                                                             float64 coincidenceDistance,
                                                             BorrowedView<PathSpanSource> outSpans,
                                                             usize& outSpanCount) noexcept;
} // namespace ludus::foundation::geometry
