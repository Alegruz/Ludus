#include <ludus/gameplay/camera/evaluation.hpp>

#include <ludus/foundation/math/quaternion.hpp>

#include <catch2/catch_test_macros.hpp>

#include <limits>

using namespace ludus::gameplay::camera;
namespace math = ludus::foundation::math;

TEST_CASE("Fixed camera publishes a self-contained pose lens and trace", "[camera]")
{
    CameraRigDefinition definition;
    definition.Pose.Position = {1.0e12 + 0.25, -7, 10};
    definition.Revision = 42;
    const CameraUpdateContext context
    {
        .OutputId = 3,
        .PreviousTick = 7,
        .CurrentTick = 8,
        .RenderAlpha = 0.25F,
        .PresentationSequence = 123,
        .DiscontinuityRevision = 9,
    };
    CameraSample sample;
    CameraTrace trace;
    REQUIRE(TryEvaluateCamera(definition, context, nullptr, sample, &trace) == CameraStatus::Success);
    REQUIRE(sample.Pose.Position == definition.Pose.Position);
    REQUIRE(sample.OutputId == 3);
    REQUIRE(sample.DefinitionRevision == 42);
    REQUIRE(sample.PreviousTick == 7);
    REQUIRE(sample.CurrentTick == 8);
    REQUIRE(sample.RenderAlpha == 0.25F);
    REQUIRE(sample.PresentationSequence == 123);
    REQUIRE(sample.DiscontinuityRevision == 9);
    REQUIRE(trace.Desired.Position == sample.Pose.Position);
    REQUIRE(trace.Published.PresentationSequence == 123);
    REQUIRE(trace.BindingId == 0);
    definition.Pose.Position = {};
    REQUIRE(sample.Pose.Position.X == 1.0e12 + 0.25);
}

TEST_CASE("Planar follow uses the supplied target once and preserves the eye plane", "[camera]")
{
    CameraRigDefinition definition
    {
        .Recipe = CameraRecipe::OrthographicFollow,
        .Pose = { .Position = {0, 0, 15} },
        .FollowOffset = {2, -3},
    };
    const CameraTargetSample target{ .Position = {100.25, 200.5, 999}, .BindingId = 8 };
    CameraSample sample;
    CameraTrace trace;
    REQUIRE(TryEvaluateCamera(definition, { .RenderAlpha = 0.25F }, &target, sample, &trace) == CameraStatus::Success);
    REQUIRE(sample.Pose.Position == math::Vector3d{102.25, 197.5, 15});
    REQUIRE(trace.BindingId == 8);
    // Quaternion sign equivalence does not turn identity into a different recipe.
    definition.Pose.Orientation.W = -1;
    REQUIRE(TryEvaluateCamera(definition, {}, &target, sample) == CameraStatus::Success);
    REQUIRE(sample.Pose.Position == math::Vector3d{102.25, 197.5, 15});
}

TEST_CASE("Camera failures preserve publication and checkpoint", "[camera]")
{
    CameraSample sample;
    sample.Pose.Position = {19, 20, 21};
    CameraTrace trace;
    trace.BindingId = 999;
    CameraRigDefinition definition{ .Recipe = CameraRecipe::OrthographicFollow };
    auto failed = [&](CameraStatus expected, const CameraUpdateContext& context, const CameraTargetSample* target) {
        REQUIRE(TryEvaluateCamera(definition, context, target, sample, &trace) == expected);
        REQUIRE(sample.Pose.Position == math::Vector3d{19, 20, 21});
        REQUIRE(trace.BindingId == 999);
    };
    failed(CameraStatus::MissingTarget, {}, nullptr);
    CameraTargetSample target;
    target.Position.X = std::numeric_limits<ludus::foundation::float64>::infinity();
    failed(CameraStatus::InvalidInput, {}, &target);
    target = {};
    target.BindingId = 0;
    failed(CameraStatus::InvalidInput, {}, &target);
    target.BindingId = 1;
    failed(CameraStatus::InvalidInput, { .RenderAlpha = -0.1F }, &target);
    failed(CameraStatus::InvalidInput, { .PreviousTick = 2, .CurrentTick = 1 }, &target);
    failed(CameraStatus::InvalidInput, { .OutputId = 0 }, &target);
    definition.Pose.Orientation.W = 2;
    failed(CameraStatus::InvalidInput, {}, &target);
    definition.Pose.Orientation = {0, 1, 0, 0};
    failed(CameraStatus::InvalidInput, {}, &target);
    definition.Pose.Orientation = {};
    definition.Lens.VerticalSpan = std::numeric_limits<ludus::foundation::float32>::quiet_NaN();
    failed(CameraStatus::InvalidInput, {}, &target);
    definition.Lens.VerticalSpan = 14;
    definition.Recipe = static_cast<CameraRecipe>(255);
    failed(CameraStatus::InvalidInput, {}, &target);
}

TEST_CASE("Active lens fields validate without infinity sentinels", "[camera]")
{
    CameraSample sample;
    REQUIRE(ValidateCameraSample(sample) == CameraStatus::Success);
    sample.Lens.Projection = CameraProjection::PerspectiveFinite;
    REQUIRE(ValidateCameraSample(sample) == CameraStatus::InvalidInput); // near=0
    sample.Lens.NearPlane = 0.25F;
    REQUIRE(ValidateCameraSample(sample) == CameraStatus::Success);
    sample.Lens.FarPlane = sample.Lens.NearPlane;
    REQUIRE(ValidateCameraSample(sample) == CameraStatus::InvalidInput);
    sample.Lens.Projection = CameraProjection::PerspectiveInfinite;
    REQUIRE(ValidateCameraSample(sample) == CameraStatus::Success);
    sample.Lens.FarPlane = std::numeric_limits<ludus::foundation::float32>::infinity();
    REQUIRE(ValidateCameraSample(sample) == CameraStatus::Success); // inactive field
    sample.Lens.Projection = CameraProjection::PerspectiveFinite;
    REQUIRE(ValidateCameraSample(sample) == CameraStatus::InvalidInput);
    sample.Lens.Projection = static_cast<CameraProjection>(255);
    REQUIRE(ValidateCameraSample(sample) == CameraStatus::InvalidInput);
}
