#pragma once

// Bounded arc-length map and distance inversion (curves and surfaces
// architecture, "Arc length and distance inversion", phase G2).
//
// Each leaf's exact arc length lies between its chord and its control-polygon
// length. Leaves are split until the whole-path gap between the summed upper
// and lower lengths fits ToleranceProfile::DistanceError. Bounds use the
// outward-rounded FoundationMath enclosures, so length intervals, prefix
// enclosures and inversion residuals are Certified. The midpoint estimate that
// defines the PathDistance domain is Estimated and lies inside the interval.

#include <ludus/foundation/base/core.h>

#include <ludus/foundation/containers/array.hpp>
#include <ludus/foundation/containers/borrowed_view.hpp>
#include <ludus/foundation/geometry/path.hpp>
#include <ludus/foundation/geometry/status.hpp>

namespace ludus::foundation::geometry
{
/// @brief One refined parameter interval of a span with its length and prefix enclosures.
///
/// Leaves are ordered by span and parameter and partition every span's [0,1].
/// Lengths are asset units. Prefix values describe the path length before U0.
struct DistanceLeaf final
{
    usize SpanIndex = 0;          ///< Prepared span.
    float64 U0 = 0.0;             ///< Start parameter; a dyadic rational, so it is exact.
    float64 U1 = 0.0;             ///< End parameter.
    float64 LengthLower = 0.0;    ///< Certified lower bound of the leaf's arc length (chord).
    float64 LengthUpper = 0.0;    ///< Certified upper bound of the leaf's arc length (control polygon).
    float64 PrefixLower = 0.0;    ///< Certified lower bound of the arc length before U0.
    float64 PrefixUpper = 0.0;    ///< Certified upper bound of the arc length before U0.
    float64 PrefixEstimate = 0.0; ///< Estimated length before U0: the running sum of leaf midpoints.
};

/// @brief Borrowed view of a PreparedDistanceMap; valid while the map is alive and unmodified.
struct DistanceMapView final
{
    BorrowedView<const DistanceLeaf> Leaves; ///< Empty for a map that was never built.
    uint64 PathRevision = 0;                 ///< Revision of the path the map was built from.
    usize PathSpanCount = 0;                 ///< Span count of that path.
    float64 TotalLower = 0.0;                ///< Certified lower bound of the whole path length.
    float64 TotalUpper = 0.0;                ///< Certified upper bound of the whole path length.
    float64 TotalEstimate = 0.0;             ///< Estimated total; PathDistance is defined on [0, TotalEstimate].
    float64 DistanceError = 0.0;             ///< Profile gap that TotalUpper - TotalLower satisfies.
    GuaranteeClass Guarantee = GuaranteeClass::Certified; ///< Class of every Lower/Upper bound.
    usize SubdivisionCount = 0;                           ///< Leaf splits performed while building.
    usize DeepestLevel = 0;                               ///< Largest leaf depth below its span.
};

/// @brief Precision and limits for TryBuildDistanceMap; the gap comes from the path's ToleranceProfile.
struct DistancePreparation final
{
    PrecisionProfile Precision = PrecisionProfile::Strict; ///< Strict accepts only Certified bounds.
    WorkLimits Limits{}; ///< MaxLeaves, MaxDepth, MaxSubdivisions and MaxPreparedBytes apply.
};

/// @brief Immutable distance cache for one prepared path revision; move-only.
///
/// Built only by TryBuildDistanceMap. A default map is empty, and queries
/// through its view return MissingCache. Single owner; synchronize replacement externally.
class PreparedDistanceMap final
{
public:
    /// Construct an empty map; queries report MissingCache.
    PreparedDistanceMap() noexcept = default;
    /// Transfer the cache; the source becomes empty.
    PreparedDistanceMap(PreparedDistanceMap&& other) noexcept = default;
    /// Release this cache and take the other's; the source becomes empty.
    PreparedDistanceMap& operator=(PreparedDistanceMap&& other) noexcept = default;
    /// Copying is disabled: one owner holds the prepared storage.
    PreparedDistanceMap(const PreparedDistanceMap&) = delete;
    /// Copy assignment is disabled: one owner holds the prepared storage.
    PreparedDistanceMap& operator=(const PreparedDistanceMap&) = delete;
    /// Release the prepared storage; views into it become invalid.
    ~PreparedDistanceMap() = default;

    /// Borrow the cache; invalidated by moving or destroying the map.
    [[nodiscard]] DistanceMapView GetView() const noexcept;
    /// Whether no map has been built into this owner.
    [[nodiscard]] bool IsEmpty() const noexcept;

private:
    friend GeometryStatus TryBuildDistanceMap(const PathView& path,
                                              const DistancePreparation& preparation,
                                              PreparedDistanceMap& outMap,
                                              GeometryDiagnostic* outDiagnosticOrNull) noexcept;

    core::Array<DistanceLeaf> mLeaves;
    uint64 mPathRevision = 0;
    usize mPathSpanCount = 0;
    float64 mTotalLower = 0.0;
    float64 mTotalUpper = 0.0;
    float64 mTotalEstimate = 0.0;
    float64 mDistanceError = 0.0;
    GuaranteeClass mGuarantee = GuaranteeClass::Certified;
    usize mSubdivisionCount = 0;
    usize mDeepestLevel = 0;
};

/// @brief Build a distance map whose whole-path gap is at most path.Tolerance.DistanceError.
///
/// Refinement splits the leaf with the largest gap at its parameter midpoint;
/// ties go to the earlier span and parameter, so the result is deterministic.
/// The gap is rechecked with outward accumulation before success.
/// @param path Borrowed prepared path; its Revision is recorded in the map.
/// @param preparation Precision profile and work limits.
/// @param outMap Replaced by the new map only on Success.
/// @param outDiagnosticOrNull Optional; on BudgetExceeded it receives the achieved Certified gap.
/// @return Success; InvalidArgument for an empty path or zero limits; BudgetExceeded when a leaf, depth,
/// subdivision or byte limit stops refinement; UnsupportedPrecision when every leaf is exact but outward
/// accumulation still exceeds the gap; OutOfRange; OutOfMemory. outMap is replaced only on Success.
[[nodiscard]] GeometryStatus TryBuildDistanceMap(const PathView& path,
                                                 const DistancePreparation& preparation,
                                                 PreparedDistanceMap& outMap,
                                                 GeometryDiagnostic* outDiagnosticOrNull) noexcept;

/// @brief Certified enclosure of the true prefix length at a parameter, with its estimate.
struct DistanceBound final
{
    float64 Lower = 0.0;                                  ///< Lower bound of the prefix length, asset units.
    float64 Upper = 0.0;                                  ///< Upper bound of the prefix length, asset units.
    float64 Estimate = 0.0;                               ///< Estimated prefix length inside [Lower, Upper].
    GuaranteeClass Guarantee = GuaranteeClass::Certified; ///< Class of Lower and Upper.
};

/// @brief Enclose the arc length from the path start to parameter, including all preceding uncertainty.
/// @return MissingCache for an empty map; StaleRevision when the map does not belong to path; InvalidDomain or
/// NonFiniteInput for a bad parameter; OutOfRange. outBound is unchanged on failure.
[[nodiscard]] GeometryStatus TryBoundDistance(const PathView& path,
                                              const DistanceMapView& map,
                                              SegmentParameter parameter,
                                              DistanceBound& outBound) noexcept;

/// @brief Distance inversion request.
struct DistanceQuery final
{
    PathDistance Distance{};                        ///< Target prefix length.
    BoundaryPolicy Policy = BoundaryPolicy::Reject; ///< Reject, Clamp, or Wrap for closed paths.
    SpanSide Side = SpanSide::Outgoing;             ///< Side selected at an exact span boundary.
    usize MaxIterations = 0;                        ///< Bisection and residual refinement work limit.
};

/// @brief Parameter whose true prefix length is within ResidualBound of the requested distance.
struct DistanceResolution final
{
    SegmentParameter Parameter{};                         ///< Earliest parameter found, by the span-side rule.
    float64 ResidualBound = 0.0;                          ///< Upper bound of |L(Parameter) - distance|.
    GuaranteeClass Guarantee = GuaranteeClass::Certified; ///< Class of ResidualBound.
};

/// @brief Find the earliest parameter whose prefix length matches a distance, with a residual bound.
///
/// The leaf is proposed by monotone lookup over prefix estimates, and the
/// parameter by bisection on partial-span estimates. Acceptance uses the
/// Certified residual bound, which must not exceed the map's DistanceError.
/// Zero-length runs resolve to their earliest parameter; the path end resolves
/// to the last span at u = 1. Near zero speed the parameter is ill-conditioned
/// even though the residual is bounded; request a tangent to detect that.
/// @return MissingCache; StaleRevision; NonFiniteInput; InvalidDomain outside [0, TotalEstimate] under Reject;
/// InvalidArgument for Hold, Loop, PingPong, an unknown policy or side, or Wrap on an open path;
/// BudgetExceeded when MaxIterations ends before the residual fits; OutOfRange. outResolution is unchanged
/// on failure.
[[nodiscard]] GeometryStatus TryResolveDistance(const PathView& path,
                                                const DistanceMapView& map,
                                                const DistanceQuery& query,
                                                DistanceResolution& outResolution) noexcept;
} // namespace ludus::foundation::geometry
