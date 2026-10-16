// Bounded arc-length map, prefix enclosures and distance inversion (see distance.hpp).

#include <ludus/foundation/geometry/distance.hpp>

#include "internal/arc_length.hpp"

#include <ludus/foundation/containers/sort.hpp>
#include <ludus/foundation/math/enclosure.hpp>
#include <ludus/foundation/math/scalar.hpp>

namespace ludus::foundation::geometry
{
using core::Array;

namespace
{
using math::Enclosure;
using math::MakePointEnclosure;

// Refinement state of one leaf while building.
struct WorkLeaf final
{
    usize SpanIndex = 0;
    float64 U0 = 0.0;
    float64 U1 = 0.0;
    usize Depth = 0;
    float64 LengthLower = 0.0;
    float64 LengthUpper = 0.0;
    float64 Gap = 0.0; ///< LengthUpper - LengthLower rounded up.
};

// Whether left is split before right: larger gap first, then earlier span and parameter.
[[nodiscard]] bool IsSplitBefore(const WorkLeaf& left, const WorkLeaf& right) noexcept
{
    if (left.Gap != right.Gap)
    {
        return left.Gap > right.Gap;
    }
    if (left.SpanIndex != right.SpanIndex)
    {
        return left.SpanIndex < right.SpanIndex;
    }
    return left.U0 < right.U0;
}

[[nodiscard]] bool IsEarlierLeaf(const WorkLeaf& left, const WorkLeaf& right) noexcept
{
    return left.SpanIndex != right.SpanIndex ? left.SpanIndex < right.SpanIndex : left.U0 < right.U0;
}

void SwapIndices(usize& inoutLeft, usize& inoutRight) noexcept
{
    const usize held = inoutLeft;
    inoutLeft = inoutRight;
    inoutRight = held;
}

void SiftUp(Array<usize>& inoutHeap, const Array<WorkLeaf>& leaves, usize position) noexcept
{
    while (position > 0)
    {
        const usize parent = (position - 1) / 2;
        if (IsSplitBefore(leaves[inoutHeap[position]], leaves[inoutHeap[parent]]) == false)
        {
            return;
        }
        SwapIndices(inoutHeap[position], inoutHeap[parent]);
        position = parent;
    }
}

void SiftDown(Array<usize>& inoutHeap, const Array<WorkLeaf>& leaves, usize position) noexcept
{
    const usize count = inoutHeap.GetSize();
    while (true)
    {
        const usize left = 2 * position + 1;
        if (left >= count)
        {
            return;
        }
        const usize right = left + 1;
        usize first = left;
        if (right < count && IsSplitBefore(leaves[inoutHeap[right]], leaves[inoutHeap[left]]) == true)
        {
            first = right;
        }
        if (IsSplitBefore(leaves[inoutHeap[first]], leaves[inoutHeap[position]]) == false)
        {
            return;
        }
        SwapIndices(inoutHeap[position], inoutHeap[first]);
        position = first;
    }
}

[[nodiscard]] bool TryPushLeaf(Array<usize>& inoutHeap, const Array<WorkLeaf>& leaves, usize leafIndex) noexcept
{
    if (inoutHeap.TryAdd(leafIndex) == false)
    {
        return false;
    }
    SiftUp(inoutHeap, leaves, inoutHeap.GetSize() - 1);
    return true;
}

void PopLeaf(Array<usize>& inoutHeap, const Array<WorkLeaf>& leaves) noexcept
{
    const usize last = inoutHeap.GetSize() - 1;
    SwapIndices(inoutHeap[0], inoutHeap[last]);
    inoutHeap.RemoveLast();
    SiftDown(inoutHeap, leaves, 0);
}

[[nodiscard]] GeometryStatus TryRebuildHeap(Array<usize>& inoutHeap, const Array<WorkLeaf>& leaves) noexcept
{
    inoutHeap.Clear();
    if (inoutHeap.TryEnsureCapacity(leaves.GetSize()) == false)
    {
        return GeometryStatus::OutOfMemory;
    }
    for (usize i = 0; i < leaves.GetSize(); ++i)
    {
        if (TryPushLeaf(inoutHeap, leaves, i) == false)
        {
            return GeometryStatus::OutOfMemory;
        }
    }
    return GeometryStatus::Success;
}

// Upper bound of upper - lower.
[[nodiscard]] GeometryStatus TryBoundDifference(float64 upper, float64 lower, float64& outDifference) noexcept
{
    Enclosure difference;
    const math::MathStatus status =
        math::TrySubtractOutward(MakePointEnclosure(upper), MakePointEnclosure(lower), difference);
    if (status != math::MathStatus::Success)
    {
        return internal::ToGeometryStatus(status);
    }
    outDifference = math::Max(0.0, difference.Upper);
    return GeometryStatus::Success;
}

[[nodiscard]] GeometryStatus TryEncloseLeaf(const PathView& path, WorkLeaf& inoutLeaf) noexcept
{
    Enclosure length;
    GeometryStatus status =
        internal::TryEncloseArcLength(path.Spans[inoutLeaf.SpanIndex], inoutLeaf.U0, inoutLeaf.U1, 1, length);
    float64 gap = 0.0;
    if (status == GeometryStatus::Success)
    {
        status = TryBoundDifference(length.Upper, length.Lower, gap);
    }
    if (status == GeometryStatus::Success)
    {
        inoutLeaf.LengthLower = length.Lower;
        inoutLeaf.LengthUpper = length.Upper;
        inoutLeaf.Gap = gap;
    }
    return status;
}

// Totals of an ordered leaf list with outward accumulation.
struct LeafTotals final
{
    float64 Lower = 0.0;
    float64 Upper = 0.0;
    float64 Estimate = 0.0;
    float64 Gap = 0.0;
};

[[nodiscard]] GeometryStatus
TryAccumulate(float64& inoutLower, float64& inoutUpper, float64 lowerTerm, float64 upperTerm) noexcept
{
    Enclosure lower;
    math::MathStatus status = math::TryAddOutward(MakePointEnclosure(inoutLower), MakePointEnclosure(lowerTerm), lower);
    Enclosure upper;
    if (status == math::MathStatus::Success)
    {
        status = math::TryAddOutward(MakePointEnclosure(inoutUpper), MakePointEnclosure(upperTerm), upper);
    }
    if (status != math::MathStatus::Success)
    {
        return internal::ToGeometryStatus(status);
    }
    inoutLower = lower.Lower;
    inoutUpper = upper.Upper;
    return GeometryStatus::Success;
}

[[nodiscard]] GeometryStatus TryComputeTotals(const Array<WorkLeaf>& leaves, LeafTotals& outTotals) noexcept
{
    LeafTotals totals;
    for (usize i = 0; i < leaves.GetSize(); ++i)
    {
        const GeometryStatus status =
            TryAccumulate(totals.Lower, totals.Upper, leaves[i].LengthLower, leaves[i].LengthUpper);
        if (status != GeometryStatus::Success)
        {
            return status;
        }
        totals.Estimate += 0.5 * (leaves[i].LengthLower + leaves[i].LengthUpper);
    }
    const GeometryStatus status = TryBoundDifference(totals.Upper, totals.Lower, totals.Gap);
    if (status == GeometryStatus::Success)
    {
        outTotals = totals;
    }
    return status;
}

[[nodiscard]] GeometryStatus FailBuild(GeometryDiagnostic* outDiagnosticOrNull,
                                       GeometryStatus status,
                                       const PathView& path,
                                       const WorkLeaf* leafOrNull,
                                       float64 requested,
                                       float64 achieved) noexcept
{
    if (outDiagnosticOrNull != nullptr)
    {
        GeometryDiagnostic diagnostic
        {
            .Code = status,
            .Property = "Tolerance.DistanceError",
            .RequestedTolerance = requested,
            .AchievedBound = achieved,
            .AchievedGuarantee = GuaranteeClass::Certified,
        };
        if (leafOrNull != nullptr)
        {
            diagnostic.SourceElement = path.Spans[leafOrNull->SpanIndex].SourceElement;
            diagnostic.Domain = DomainKind::SegmentParameter;
            diagnostic.RangeStart = static_cast<float64>(leafOrNull->SpanIndex) + leafOrNull->U0;
            diagnostic.RangeEnd = static_cast<float64>(leafOrNull->SpanIndex) + leafOrNull->U1;
        }
        *outDiagnosticOrNull = diagnostic;
    }
    return status;
}

[[nodiscard]] bool IsKnownPolicy(BoundaryPolicy policy) noexcept
{
    return policy == BoundaryPolicy::Reject || policy == BoundaryPolicy::Clamp || policy == BoundaryPolicy::Hold ||
           policy == BoundaryPolicy::Loop || policy == BoundaryPolicy::PingPong || policy == BoundaryPolicy::Wrap;
}

[[nodiscard]] GeometryStatus ValidateMap(const PathView& path, const DistanceMapView& map) noexcept
{
    if (map.Leaves.IsEmpty() == true)
    {
        return GeometryStatus::MissingCache;
    }
    if (map.PathRevision != path.Revision || map.PathSpanCount != path.Spans.GetSize())
    {
        return GeometryStatus::StaleRevision;
    }
    return GeometryStatus::Success;
}

// Index of the last leaf that starts at or before (spanIndex, u).
[[nodiscard]] usize FindLeaf(const DistanceMapView& map, usize spanIndex, float64 u) noexcept
{
    usize low = 0;
    usize high = map.Leaves.GetSize() - 1;
    while (low < high)
    {
        const usize middle = low + (high - low + 1) / 2;
        const DistanceLeaf& leaf = map.Leaves[middle];
        const bool isAfter = leaf.SpanIndex > spanIndex || (leaf.SpanIndex == spanIndex && leaf.U0 > u);
        if (isAfter == true)
        {
            high = middle - 1;
        }
        else
        {
            low = middle;
        }
    }
    return low;
}

// Estimated prefix length at the end of leaf index.
[[nodiscard]] float64 GetEndEstimate(const DistanceMapView& map, usize index) noexcept
{
    return index + 1 < map.Leaves.GetSize() ? map.Leaves[index + 1].PrefixEstimate : map.TotalEstimate;
}

// Certified prefix enclosure at u inside leaf, using pieceCount partial pieces.
[[nodiscard]] GeometryStatus TryBoundInLeaf(const PathView& path,
                                            const DistanceLeaf& leaf,
                                            float64 u,
                                            usize pieceCount,
                                            DistanceBound& outBound) noexcept
{
    Enclosure partial = MakePointEnclosure(0.0);
    GeometryStatus status = GeometryStatus::Success;
    if (u > leaf.U0)
    {
        status = internal::TryEncloseArcLength(path.Spans[leaf.SpanIndex], leaf.U0, u, pieceCount, partial);
    }
    DistanceBound bound;
    bound.Lower = leaf.PrefixLower;
    bound.Upper = leaf.PrefixUpper;
    if (status == GeometryStatus::Success)
    {
        status = TryAccumulate(bound.Lower, bound.Upper, partial.Lower, partial.Upper);
    }
    if (status != GeometryStatus::Success)
    {
        return status;
    }
    // The estimate is defined inside the Certified interval.
    bound.Estimate = math::Clamp(leaf.PrefixEstimate + 0.5 * (partial.Lower + partial.Upper), bound.Lower, bound.Upper);
    bound.Guarantee = GuaranteeClass::Certified;
    outBound = bound;
    return GeometryStatus::Success;
}

// Upper bound of |L - target| for L in [bound.Lower, bound.Upper].
[[nodiscard]] GeometryStatus
TryComputeResidual(const DistanceBound& bound, float64 target, float64& outResidual) noexcept
{
    float64 below = 0.0;
    GeometryStatus status = TryBoundDifference(target, bound.Lower, below);
    float64 above = 0.0;
    if (status == GeometryStatus::Success)
    {
        status = TryBoundDifference(bound.Upper, target, above);
    }
    if (status == GeometryStatus::Success)
    {
        outResidual = math::Max(below, above);
    }
    return status;
}
} // namespace

DistanceMapView PreparedDistanceMap::GetView() const noexcept
{
    return DistanceMapView
    {
        .Leaves = BorrowedView<const DistanceLeaf>(mLeaves.GetData(), mLeaves.GetSize()),
        .PathRevision = mPathRevision,
        .PathSpanCount = mPathSpanCount,
        .TotalLower = mTotalLower,
        .TotalUpper = mTotalUpper,
        .TotalEstimate = mTotalEstimate,
        .DistanceError = mDistanceError,
        .Guarantee = mGuarantee,
        .SubdivisionCount = mSubdivisionCount,
        .DeepestLevel = mDeepestLevel,
    };
}

bool PreparedDistanceMap::IsEmpty() const noexcept
{
    return mLeaves.IsEmpty();
}

GeometryStatus TryBuildDistanceMap(const PathView& path,
                                   const DistancePreparation& preparation,
                                   PreparedDistanceMap& outMap,
                                   GeometryDiagnostic* outDiagnosticOrNull) noexcept
{
    const usize spanCount = path.Spans.GetSize();
    const float64 errorBudget = path.Tolerance.DistanceError;
    const bool isKnownPrecision =
        preparation.Precision == PrecisionProfile::Strict || preparation.Precision == PrecisionProfile::Visual;
    if (spanCount == 0 || isKnownPrecision == false || (errorBudget > 0.0) == false)
    {
        return FailBuild(outDiagnosticOrNull, GeometryStatus::InvalidArgument, path, nullptr, errorBudget, 0.0);
    }
    const WorkLimits& limits = preparation.Limits;
    if (spanCount > limits.MaxLeaves)
    {
        return FailBuild(outDiagnosticOrNull, GeometryStatus::BudgetExceeded, path, nullptr, errorBudget, 0.0);
    }

    Array<WorkLeaf> leaves;
    Array<usize> heap;
    if (leaves.TryEnsureCapacity(spanCount) == false || heap.TryEnsureCapacity(spanCount) == false)
    {
        return FailBuild(outDiagnosticOrNull, GeometryStatus::OutOfMemory, path, nullptr, errorBudget, 0.0);
    }
    float64 gapEstimate = 0.0;
    for (usize spanIndex = 0; spanIndex < spanCount; ++spanIndex)
    {
        WorkLeaf leaf{ .SpanIndex = spanIndex, .U0 = 0.0, .U1 = 1.0 };
        const GeometryStatus status = TryEncloseLeaf(path, leaf);
        if (status != GeometryStatus::Success)
        {
            return FailBuild(outDiagnosticOrNull, status, path, &leaf, errorBudget, 0.0);
        }
        if (leaves.TryAdd(leaf) == false || TryPushLeaf(heap, leaves, spanIndex) == false)
        {
            return FailBuild(outDiagnosticOrNull, GeometryStatus::OutOfMemory, path, nullptr, errorBudget, 0.0);
        }
        gapEstimate += leaf.Gap;
    }

    usize subdivisionCount = 0;
    usize deepestLevel = 0;
    LeafTotals totals;
    while (true)
    {
        if (gapEstimate <= errorBudget)
        {
            // Certify the gap in path order, the order the prefixes are stored in.
            Sort(leaves.GetData(), leaves.GetSize(), [](const WorkLeaf& left, const WorkLeaf& right) noexcept {
                return IsEarlierLeaf(left, right);
            });
            GeometryStatus status = TryComputeTotals(leaves, totals);
            if (status != GeometryStatus::Success)
            {
                return FailBuild(outDiagnosticOrNull, status, path, nullptr, errorBudget, 0.0);
            }
            if (totals.Gap <= errorBudget)
            {
                break;
            }
            status = TryRebuildHeap(heap, leaves);
            if (status != GeometryStatus::Success)
            {
                return FailBuild(outDiagnosticOrNull, status, path, nullptr, errorBudget, totals.Gap);
            }
            gapEstimate = totals.Gap;
        }
        const usize index = heap[0];
        const WorkLeaf parent = leaves[index];
        if (parent.Gap == 0.0)
        {
            // Every leaf is exact, yet outward accumulation alone exceeds the requested gap.
            return FailBuild(outDiagnosticOrNull,
                             GeometryStatus::UnsupportedPrecision,
                             path,
                             nullptr,
                             errorBudget,
                             gapEstimate);
        }
        const bool isWithinLimits = parent.Depth < limits.MaxDepth && subdivisionCount < limits.MaxSubdivisions &&
                                    leaves.GetSize() < limits.MaxLeaves;
        if (isWithinLimits == false)
        {
            LeafTotals achieved;
            const GeometryStatus totalStatus = TryComputeTotals(leaves, achieved);
            const float64 achievedGap = totalStatus == GeometryStatus::Success ? achieved.Gap : gapEstimate;
            return FailBuild(outDiagnosticOrNull,
                             GeometryStatus::BudgetExceeded,
                             path,
                             &parent,
                             errorBudget,
                             achievedGap);
        }
        // Dyadic midpoints are exact, so neighboring leaves share their boundary parameter.
        const float64 middle = parent.U0 + (parent.U1 - parent.U0) * 0.5;
        WorkLeaf left{ .SpanIndex = parent.SpanIndex, .U0 = parent.U0, .U1 = middle, .Depth = parent.Depth + 1 };
        WorkLeaf right{ .SpanIndex = parent.SpanIndex, .U0 = middle, .U1 = parent.U1, .Depth = parent.Depth + 1 };
        GeometryStatus status = TryEncloseLeaf(path, left);
        if (status == GeometryStatus::Success)
        {
            status = TryEncloseLeaf(path, right);
        }
        if (status != GeometryStatus::Success)
        {
            return FailBuild(outDiagnosticOrNull, status, path, &parent, errorBudget, gapEstimate);
        }
        PopLeaf(heap, leaves);
        leaves[index] = left;
        if (leaves.TryAdd(right) == false || TryPushLeaf(heap, leaves, index) == false ||
            TryPushLeaf(heap, leaves, leaves.GetSize() - 1) == false)
        {
            return FailBuild(outDiagnosticOrNull, GeometryStatus::OutOfMemory, path, &parent, errorBudget, gapEstimate);
        }
        gapEstimate += left.Gap + right.Gap - parent.Gap;
        ++subdivisionCount;
        deepestLevel = math::Max(deepestLevel, left.Depth);
    }

    const usize leafCount = leaves.GetSize();
    if (leafCount * sizeof(DistanceLeaf) > limits.MaxPreparedBytes)
    {
        return FailBuild(outDiagnosticOrNull, GeometryStatus::BudgetExceeded, path, nullptr, errorBudget, totals.Gap);
    }
    PreparedDistanceMap candidate;
    if (candidate.mLeaves.TryResize(leafCount) == false)
    {
        return FailBuild(outDiagnosticOrNull, GeometryStatus::OutOfMemory, path, nullptr, errorBudget, totals.Gap);
    }
    float64 prefixLower = 0.0;
    float64 prefixUpper = 0.0;
    float64 prefixEstimate = 0.0;
    for (usize i = 0; i < leafCount; ++i)
    {
        const WorkLeaf& leaf = leaves[i];
        candidate.mLeaves[i] = DistanceLeaf
        {
            .SpanIndex = leaf.SpanIndex,
            .U0 = leaf.U0,
            .U1 = leaf.U1,
            .LengthLower = leaf.LengthLower,
            .LengthUpper = leaf.LengthUpper,
            .PrefixLower = prefixLower,
            .PrefixUpper = prefixUpper,
            .PrefixEstimate = prefixEstimate,
        };
        // Same order and operations as TryComputeTotals, which already succeeded.
        const GeometryStatus status = TryAccumulate(prefixLower, prefixUpper, leaf.LengthLower, leaf.LengthUpper);
        if (status != GeometryStatus::Success)
        {
            return FailBuild(outDiagnosticOrNull, status, path, nullptr, errorBudget, totals.Gap);
        }
        prefixEstimate += 0.5 * (leaf.LengthLower + leaf.LengthUpper);
    }
    candidate.mPathRevision = path.Revision;
    candidate.mPathSpanCount = spanCount;
    candidate.mTotalLower = totals.Lower;
    candidate.mTotalUpper = totals.Upper;
    candidate.mTotalEstimate = math::Clamp(totals.Estimate, totals.Lower, totals.Upper);
    candidate.mDistanceError = errorBudget;
    candidate.mGuarantee = GuaranteeClass::Certified;
    candidate.mSubdivisionCount = subdivisionCount;
    candidate.mDeepestLevel = deepestLevel;
    outMap = Move(candidate);
    return GeometryStatus::Success;
}

GeometryStatus TryBoundDistance(const PathView& path,
                                const DistanceMapView& map,
                                SegmentParameter parameter,
                                DistanceBound& outBound) noexcept
{
    GeometryStatus status = ValidateMap(path, map);
    if (status != GeometryStatus::Success)
    {
        return status;
    }
    if (math::IsFinite(parameter.U) == false)
    {
        return GeometryStatus::NonFiniteInput;
    }
    if (parameter.SpanIndex >= path.Spans.GetSize() || parameter.U < 0.0 || parameter.U > 1.0)
    {
        return GeometryStatus::InvalidDomain;
    }
    const DistanceLeaf& leaf = map.Leaves[FindLeaf(map, parameter.SpanIndex, parameter.U)];
    return TryBoundInLeaf(path, leaf, parameter.U, 1, outBound);
}

GeometryStatus TryResolveDistance(const PathView& path,
                                  const DistanceMapView& map,
                                  const DistanceQuery& query,
                                  DistanceResolution& outResolution) noexcept
{
    const bool isKnownSide = query.Side == SpanSide::Outgoing || query.Side == SpanSide::Incoming;
    if (IsKnownPolicy(query.Policy) == false || isKnownSide == false)
    {
        return GeometryStatus::InvalidArgument;
    }
    const bool isPathPolicy = query.Policy == BoundaryPolicy::Reject || query.Policy == BoundaryPolicy::Clamp ||
                              (query.Policy == BoundaryPolicy::Wrap && path.IsClosed == true);
    if (isPathPolicy == false)
    {
        return GeometryStatus::InvalidArgument;
    }
    GeometryStatus status = ValidateMap(path, map);
    if (status != GeometryStatus::Success)
    {
        return status;
    }
    float64 target = query.Distance.Value;
    if (math::IsFinite(target) == false)
    {
        return GeometryStatus::NonFiniteInput;
    }
    const float64 total = map.TotalEstimate;
    if (target < 0.0 || target > total)
    {
        if (query.Policy == BoundaryPolicy::Reject)
        {
            return GeometryStatus::InvalidDomain;
        }
        if (query.Policy == BoundaryPolicy::Clamp)
        {
            target = math::Clamp(target, 0.0, total);
        }
        else
        {
            // Wrap: start + mod(x - start, D) in [0, D); a stationary loop maps everything to 0.
            target = total > 0.0 ? target - total * math::Floor(target / total) : 0.0;
            if ((target >= 0.0 && target < total) == false)
            {
                target = 0.0;
            }
        }
    }

    const usize lastSpan = path.Spans.GetSize() - 1;
    SegmentParameter parameter;
    DistanceBound bound;
    usize work = 0;
    if (target <= 0.0)
    {
        parameter = SegmentParameter{ .SpanIndex = 0, .U = 0.0 };
        bound = DistanceBound{ .Lower = 0.0, .Upper = 0.0, .Estimate = 0.0 };
    }
    else if (target >= total)
    {
        parameter = SegmentParameter{ .SpanIndex = lastSpan, .U = 1.0 };
        bound = DistanceBound{ .Lower = map.TotalLower, .Upper = map.TotalUpper, .Estimate = total };
    }
    else
    {
        // First leaf whose end estimate reaches the target; the leaf before it ends below the target.
        usize low = 0;
        usize high = map.Leaves.GetSize() - 1;
        while (low < high)
        {
            const usize middle = low + (high - low) / 2;
            if (GetEndEstimate(map, middle) >= target)
            {
                high = middle;
            }
            else
            {
                low = middle + 1;
            }
        }
        const DistanceLeaf& leaf = map.Leaves[low];
        // Bisect for the earliest parameter whose estimate reaches the target. A target equal to the
        // leaf's end estimate resolves to the leaf end, so stored boundary distances map to exact boundaries.
        const bool isAtLeafEnd = GetEndEstimate(map, low) == target;
        float64 below = leaf.U0;
        float64 above = leaf.U1;
        while (isAtLeafEnd == false && work < query.MaxIterations)
        {
            const float64 middle = below + (above - below) * 0.5;
            if (middle <= below || middle >= above)
            {
                break;
            }
            DistanceBound middleBound;
            status = TryBoundInLeaf(path, leaf, middle, 1, middleBound);
            ++work;
            if (status != GeometryStatus::Success)
            {
                return status;
            }
            if (middleBound.Estimate >= target)
            {
                above = middle;
            }
            else
            {
                below = middle;
            }
        }
        // Accept by the Certified residual, refining the partial enclosure while work remains.
        float64 residual = 0.0;
        bool isAccepted = false;
        for (usize pieceCount = 1; isAccepted == false && work + pieceCount <= query.MaxIterations; pieceCount *= 2)
        {
            status = TryBoundInLeaf(path, leaf, above, pieceCount, bound);
            work += pieceCount;
            if (status == GeometryStatus::Success)
            {
                status = TryComputeResidual(bound, target, residual);
            }
            if (status != GeometryStatus::Success)
            {
                return status;
            }
            isAccepted = residual <= map.DistanceError;
        }
        if (isAccepted == false)
        {
            return GeometryStatus::BudgetExceeded;
        }
        parameter = SegmentParameter{ .SpanIndex = leaf.SpanIndex, .U = above };
    }

    float64 residual = 0.0;
    status = TryComputeResidual(bound, target, residual);
    if (status != GeometryStatus::Success)
    {
        return status;
    }
    if (residual > map.DistanceError)
    {
        return GeometryStatus::BudgetExceeded;
    }
    // An exact span boundary selects the outgoing span unless the incoming side is requested.
    if (parameter.U == 1.0 && parameter.SpanIndex < lastSpan)
    {
        parameter = SegmentParameter{ .SpanIndex = parameter.SpanIndex + 1, .U = 0.0 };
    }
    if (query.Side == SpanSide::Incoming && parameter.U == 0.0 && parameter.SpanIndex > 0)
    {
        parameter = SegmentParameter{ .SpanIndex = parameter.SpanIndex - 1, .U = 1.0 };
    }
    outResolution = DistanceResolution
    {
        .Parameter = parameter,
        .ResidualBound = residual,
        .Guarantee = map.Guarantee,
    };
    return GeometryStatus::Success;
}
} // namespace ludus::foundation::geometry
