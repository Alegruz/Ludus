// Frozen ADR 0024 vocabulary, tolerance validation and the position floor.

#include "geometry_test_support.hpp"

#include <ludus/foundation/geometry/status.hpp>
#include <ludus/foundation/math/scalar.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <limits>

using namespace ludus::foundation::geometry;
namespace math = ludus::foundation::math;
namespace fixtures = ludus::geometry_fixtures;
using test_support::MakeProfile;

namespace
{
[[nodiscard]] bool IsNamed(const char* actual, const char* expected) noexcept
{
    return std::strcmp(actual, expected) == 0;
}
} // namespace

TEST_CASE("GeometryStatus names match the frozen enumerators", "[geometry][status]")
{
    REQUIRE(IsNamed(GetStatusName(GeometryStatus::Success), "Success"));
    REQUIRE(IsNamed(GetStatusName(GeometryStatus::NonFiniteInput), "NonFiniteInput"));
    REQUIRE(IsNamed(GetStatusName(GeometryStatus::InvalidArgument), "InvalidArgument"));
    REQUIRE(IsNamed(GetStatusName(GeometryStatus::InvalidDomain), "InvalidDomain"));
    REQUIRE(IsNamed(GetStatusName(GeometryStatus::InvalidTopology), "InvalidTopology"));
    REQUIRE(IsNamed(GetStatusName(GeometryStatus::MissingCache), "MissingCache"));
    REQUIRE(IsNamed(GetStatusName(GeometryStatus::StaleRevision), "StaleRevision"));
    REQUIRE(IsNamed(GetStatusName(GeometryStatus::Degenerate), "Degenerate"));
    REQUIRE(IsNamed(GetStatusName(GeometryStatus::DegenerateFrame), "DegenerateFrame"));
    REQUIRE(IsNamed(GetStatusName(GeometryStatus::IllConditioned), "IllConditioned"));
    REQUIRE(IsNamed(GetStatusName(GeometryStatus::OutOfRange), "OutOfRange"));
    REQUIRE(IsNamed(GetStatusName(GeometryStatus::UnsupportedRepresentation), "UnsupportedRepresentation"));
    REQUIRE(IsNamed(GetStatusName(GeometryStatus::UnsupportedPrecision), "UnsupportedPrecision"));
    REQUIRE(IsNamed(GetStatusName(GeometryStatus::BudgetExceeded), "BudgetExceeded"));
    REQUIRE(IsNamed(GetStatusName(GeometryStatus::CapacityExceeded), "CapacityExceeded"));
    REQUIRE(IsNamed(GetStatusName(GeometryStatus::OutOfMemory), "OutOfMemory"));
    REQUIRE(IsNamed(GetStatusName(GeometryStatus::Cancelled), "Cancelled"));
    REQUIRE(IsNamed(GetStatusName(static_cast<GeometryStatus>(200)), "Unknown"));
    REQUIRE(IsSuccess(GeometryStatus::Success) == true);
    REQUIRE(IsSuccess(GeometryStatus::Cancelled) == false);
}

TEST_CASE("Only Exact and Certified bounds satisfy a strict profile", "[geometry][status]")
{
    REQUIRE(IsStrictGuarantee(GuaranteeClass::Exact) == true);
    REQUIRE(IsStrictGuarantee(GuaranteeClass::Certified) == true);
    REQUIRE(IsStrictGuarantee(GuaranteeClass::Rounded) == false);
    REQUIRE(IsStrictGuarantee(GuaranteeClass::Estimated) == false);
    REQUIRE(IsStrictGuarantee(GuaranteeClass::Approximate) == false);
    REQUIRE(IsNamed(GetGuaranteeClassName(GuaranteeClass::Certified), "Certified"));
    REQUIRE(IsNamed(GetGuaranteeClassName(GuaranteeClass::Approximate), "Approximate"));
}

TEST_CASE("Tolerance profiles are validated field by field", "[geometry][status]")
{
    REQUIRE(TryValidateToleranceProfile(MakeProfile()) == GeometryStatus::Success);
    REQUIRE(TryValidateToleranceProfile(ToleranceProfile{}) == GeometryStatus::InvalidArgument);

    ToleranceProfile profile = MakeProfile();
    profile.DistanceError = std::numeric_limits<float64>::quiet_NaN();
    REQUIRE(TryValidateToleranceProfile(profile) == GeometryStatus::NonFiniteInput);
    profile = MakeProfile();
    profile.PositionError = -1.0;
    REQUIRE(TryValidateToleranceProfile(profile) == GeometryStatus::InvalidArgument);
    profile = MakeProfile();
    profile.RelativePositionError = 1.0;
    REQUIRE(TryValidateToleranceProfile(profile) == GeometryStatus::InvalidArgument);
    profile = MakeProfile();
    profile.TangentAngleError = 2.0;
    REQUIRE(TryValidateToleranceProfile(profile) == GeometryStatus::InvalidArgument);
    profile = MakeProfile();
    profile.ContinuityPosition = 0.0;
    profile.ContinuityAngle = 0.0;
    REQUIRE(TryValidateToleranceProfile(profile) == GeometryStatus::Success);
    profile.ContinuityAngle = -1e-9;
    REQUIRE(TryValidateToleranceProfile(profile) == GeometryStatus::InvalidArgument);
    profile = MakeProfile();
    profile.CoincidenceDistance = 0.0;
    REQUIRE(TryValidateToleranceProfile(profile) == GeometryStatus::InvalidArgument);
    profile = MakeProfile();
    profile.NormalMinSinAngle = std::numeric_limits<float64>::infinity();
    REQUIRE(TryValidateToleranceProfile(profile) == GeometryStatus::NonFiniteInput);
}

TEST_CASE("Position floor is two float32 units at the control extent", "[geometry][status]")
{
    for (const fixtures::CubicFixture& fixture : fixtures::CUBIC_FIXTURES)
    {
        float64 extent = 0.0;
        for (const fixtures::ControlPoint& control : fixture.Control)
        {
            extent = math::Max(extent, math::Abs(static_cast<float64>(control.X)));
            extent = math::Max(extent, math::Abs(static_cast<float64>(control.Y)));
            extent = math::Max(extent, math::Abs(static_cast<float64>(control.Z)));
        }
        INFO(fixture.Name);
        REQUIRE(ComputePositionFloor(extent) == fixture.PositionFloor);
    }
    for (const fixtures::PrecisionFixture& fixture : fixtures::PRECISION_FIXTURES)
    {
        // MaxAbsComponent is the declared narrowing range; the floor uses the largest stored component.
        float64 extent = 0.0;
        for (const fixtures::ControlPoint& control : fixture.Relative)
        {
            extent = math::Max(extent, math::Abs(static_cast<float64>(control.X)));
            extent = math::Max(extent, math::Abs(static_cast<float64>(control.Y)));
            extent = math::Max(extent, math::Abs(static_cast<float64>(control.Z)));
        }
        REQUIRE(ComputePositionFloor(extent) == fixture.RelativePositionFloor);
        REQUIRE(ComputePositionFloor(1073741825.5) == fixture.AbsolutePositionFloor);
    }
    // Powers of two use the spacing above them.
    REQUIRE(ComputePositionFloor(1.0) == 2.0 * 0x1p-23);
    REQUIRE(ComputePositionFloor(1.5) == 2.0 * 0x1p-23);
    REQUIRE(ComputePositionFloor(0.0) == 0x1p-148);
    REQUIRE(ComputePositionFloor(0x1p-130) == 0x1p-148);
}
