// GeometryStatus names, tolerance validation and the float32 position floor (ADR 0024).

#include <ludus/foundation/geometry/status.hpp>

#include <ludus/foundation/containers/static_array.hpp>
#include <ludus/foundation/math/scalar.hpp>

namespace ludus::foundation::geometry
{
namespace
{
// Number of ToleranceProfile fields; every one is checked for finiteness.
constexpr usize TOLERANCE_FIELD_COUNT = 12;
// Float32 has 23 explicit significand bits; ulp32(E) = 2^(floor(log2 E) - 23).
constexpr float64 FLOAT32_SIGNIFICAND_SCALE = 1.0 / 8388608.0;
// Smallest float32 normal value, 2^-126.
constexpr float64 FLOAT32_SMALLEST_NORMAL = 1.1754943508222875e-38;
// Two float32 subnormal units, 2 * 2^-149.
constexpr float64 FLOAT32_SUBNORMAL_FLOOR = 2.802596928649634e-45;

[[nodiscard]] bool IsInUnitInterval(float64 value) noexcept
{
    return value >= 0.0 && value < 1.0;
}

[[nodiscard]] bool IsOpenAngle(float64 value) noexcept
{
    return value > 0.0 && value < math::kHalfPi;
}
} // namespace

const char* GetStatusName(GeometryStatus status) noexcept
{
    switch (status)
    {
        case GeometryStatus::Success:
            return "Success";
        case GeometryStatus::NonFiniteInput:
            return "NonFiniteInput";
        case GeometryStatus::InvalidArgument:
            return "InvalidArgument";
        case GeometryStatus::InvalidDomain:
            return "InvalidDomain";
        case GeometryStatus::InvalidTopology:
            return "InvalidTopology";
        case GeometryStatus::MissingCache:
            return "MissingCache";
        case GeometryStatus::StaleRevision:
            return "StaleRevision";
        case GeometryStatus::Degenerate:
            return "Degenerate";
        case GeometryStatus::DegenerateFrame:
            return "DegenerateFrame";
        case GeometryStatus::IllConditioned:
            return "IllConditioned";
        case GeometryStatus::OutOfRange:
            return "OutOfRange";
        case GeometryStatus::UnsupportedRepresentation:
            return "UnsupportedRepresentation";
        case GeometryStatus::UnsupportedPrecision:
            return "UnsupportedPrecision";
        case GeometryStatus::BudgetExceeded:
            return "BudgetExceeded";
        case GeometryStatus::CapacityExceeded:
            return "CapacityExceeded";
        case GeometryStatus::OutOfMemory:
            return "OutOfMemory";
        case GeometryStatus::Cancelled:
            return "Cancelled";
    }
    return "Unknown";
}

const char* GetGuaranteeClassName(GuaranteeClass guarantee) noexcept
{
    switch (guarantee)
    {
        case GuaranteeClass::Exact:
            return "Exact";
        case GuaranteeClass::Rounded:
            return "Rounded";
        case GuaranteeClass::Certified:
            return "Certified";
        case GuaranteeClass::Estimated:
            return "Estimated";
        case GuaranteeClass::Approximate:
            return "Approximate";
    }
    return "Unknown";
}

GeometryStatus TryValidateToleranceProfile(const ToleranceProfile& profile) noexcept
{
    const core::StaticArray<float64, TOLERANCE_FIELD_COUNT> fields{{
        profile.PositionError,
        profile.RelativePositionError,
        profile.DistanceError,
        profile.TangentAngleError,
        profile.ProjectionGap,
        profile.ContinuityPosition,
        profile.ContinuityDerivative,
        profile.ContinuityAngle,
        profile.NormalMinDerivativeLength,
        profile.NormalMinSinAngle,
        profile.NormalAngleError,
        profile.CoincidenceDistance,
    }};
    for (const float64 field : fields)
    {
        if (math::IsFinite(field) == false)
        {
            return GeometryStatus::NonFiniteInput;
        }
    }
    const bool isValid = profile.PositionError > 0.0 && IsInUnitInterval(profile.RelativePositionError) == true &&
                         profile.DistanceError > 0.0 && IsOpenAngle(profile.TangentAngleError) == true &&
                         profile.ProjectionGap > 0.0 && profile.ContinuityPosition >= 0.0 &&
                         IsInUnitInterval(profile.ContinuityDerivative) == true && profile.ContinuityAngle >= 0.0 &&
                         profile.ContinuityAngle < math::kHalfPi && profile.NormalMinDerivativeLength >= 0.0 &&
                         IsInUnitInterval(profile.NormalMinSinAngle) == true &&
                         IsOpenAngle(profile.NormalAngleError) == true && profile.CoincidenceDistance > 0.0;
    return isValid == true ? GeometryStatus::Success : GeometryStatus::InvalidArgument;
}

float64 ComputePositionFloor(float64 maxAbsComponent) noexcept
{
    if ((maxAbsComponent >= FLOAT32_SMALLEST_NORMAL) == false)
    {
        return FLOAT32_SUBNORMAL_FLOOR;
    }
    // Largest power of two not above E, found with exact power-of-two steps.
    float64 power = 1.0;
    while (power * 2.0 <= maxAbsComponent)
    {
        power *= 2.0;
    }
    while (power > maxAbsComponent)
    {
        power *= 0.5;
    }
    return 2.0 * power * FLOAT32_SIGNIFICAND_SCALE;
}
} // namespace ludus::foundation::geometry
