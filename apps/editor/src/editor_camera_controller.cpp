#include "internal/editor_camera_controller.h"

#include <ludus/foundation/math/scalar.hpp>

namespace ludus::editor
{
using namespace foundation;
namespace math = foundation::math;
namespace camera = gameplay::camera;
namespace
{
// Absolute numerical safety bounds, independent of scene size, in world units.
constexpr float32 MINIMUM_DISTANCE = 0.000001F;
constexpr float32 MAXIMUM_DISTANCE = 10000000;
constexpr float64 MAXIMUM_POSITION_WORLD = 10000000;
constexpr float32 LOOK_RADIANS_PER_PIXEL = math::kPiF / 720;
constexpr float32 MAXIMUM_PITCH_RADIANS = math::kHalfPiF - 0.001F;
math::Vector3d ConvertToWorld(math::Vector3 value) noexcept
{
    return {static_cast<float64>(value.X), static_cast<float64>(value.Y), static_cast<float64>(value.Z)};
}
} // namespace
EditorCameraController::EditorCameraController() noexcept
{
    mSample.Lens.Projection = camera::CameraProjection::PerspectiveInfinite;
    mSample.Lens.NearPlane = 0.01F;
    static_cast<void>(TrySetOrbit({}, {25, 20}, mDistance));
}
bool EditorCameraController::tryPublish(const camera::CameraSample& sample, float32 distance) noexcept
{
    if (math::IsFinite(distance) == false || distance < MINIMUM_DISTANCE || distance > MAXIMUM_DISTANCE ||
        math::Abs(sample.Pose.Position.X) > MAXIMUM_POSITION_WORLD ||
        math::Abs(sample.Pose.Position.Y) > MAXIMUM_POSITION_WORLD ||
        math::Abs(sample.Pose.Position.Z) > MAXIMUM_POSITION_WORLD)
    {
        return false;
    }
    const camera::CameraRigDefinition definition{ .Pose = sample.Pose, .Lens = sample.Lens };
    camera::CameraSample accepted;
    if (camera::TryEvaluateCamera(definition, {}, nullptr, accepted) != camera::CameraStatus::Success)
    {
        return false;
    }
    mSample = accepted;
    mDistance = distance;
    return true;
}
bool EditorCameraController::TryAdopt(const camera::CameraSample& sample, float32 distance) noexcept
{
    return tryPublish(sample, distance);
}
math::Vector3d EditorCameraController::GetPivotWorld() const noexcept
{
    return mSample.Pose.Position +
           ConvertToWorld(math::Rotate(mSample.Pose.Orientation, {0, 0, -1})) * static_cast<float64>(mDistance);
}
float32 EditorCameraController::GetYawDegrees() const noexcept
{
    const math::Vector3 forwardWorld = math::Rotate(mSample.Pose.Orientation, {0, 0, -1});
    return math::RadiansToDegrees(math::Atan2(-forwardWorld.X, -forwardWorld.Z));
}
float32 EditorCameraController::GetPitchDegrees() const noexcept
{
    const math::Vector3 forwardWorld = math::Rotate(mSample.Pose.Orientation, {0, 0, -1});
    return math::RadiansToDegrees(
        math::Atan2(-forwardWorld.Y, math::Sqrt(forwardWorld.X * forwardWorld.X + forwardWorld.Z * forwardWorld.Z)));
}
bool EditorCameraController::TrySetOrbit(math::Vector3d pivotWorld,
                                         math::Vector2 orbitDegrees,
                                         float32 distance) noexcept
{
    if (math::IsFinite(orbitDegrees) == false || math::IsFinite(pivotWorld) == false)
    {
        return false;
    }
    math::Quaternion yaw;
    math::Quaternion pitch;
    if (math::TryFromAxisAngle({0, 1, 0}, math::DegreesToRadians(orbitDegrees.X), yaw) != math::MathStatus::Success ||
        math::TryFromAxisAngle({1, 0, 0}, -math::DegreesToRadians(math::Clamp(orbitDegrees.Y, -89.9F, 89.9F)), pitch) !=
            math::MathStatus::Success)
    {
        return false;
    }
    camera::CameraSample candidate = mSample;
    if (math::TryNormalize(yaw * pitch, candidate.Pose.Orientation) != math::MathStatus::Success)
    {
        return false;
    }
    candidate.Pose.Position = pivotWorld - ConvertToWorld(math::Rotate(candidate.Pose.Orientation, {0, 0, -1})) *
                                               static_cast<float64>(distance);
    return tryPublish(candidate, distance);
}
bool EditorCameraController::TryRotate(float32 deltaX, float32 deltaY, bool isOrbit) noexcept
{
    if (math::IsFinite(deltaX) == false || math::IsFinite(deltaY) == false)
    {
        return false;
    }
    const math::Vector3d pivotWorld = GetPivotWorld();
    const float32 currentPitch = -math::DegreesToRadians(GetPitchDegrees());
    const float32 nextPitch =
        math::Clamp(currentPitch - deltaY * LOOK_RADIANS_PER_PIXEL, -MAXIMUM_PITCH_RADIANS, MAXIMUM_PITCH_RADIANS);
    math::Quaternion yaw;
    math::Quaternion pitch;
    if (math::TryFromAxisAngle({0, 1, 0}, -deltaX * LOOK_RADIANS_PER_PIXEL, yaw) != math::MathStatus::Success ||
        math::TryFromAxisAngle({1, 0, 0}, nextPitch - currentPitch, pitch) != math::MathStatus::Success)
    {
        return false;
    }
    camera::CameraSample candidate = mSample;
    if (math::TryNormalize(yaw * candidate.Pose.Orientation * pitch, candidate.Pose.Orientation) !=
        math::MathStatus::Success)
    {
        return false;
    }
    if (isOrbit == true)
    {
        candidate.Pose.Position = pivotWorld - ConvertToWorld(math::Rotate(candidate.Pose.Orientation, {0, 0, -1})) *
                                                   static_cast<float64>(mDistance);
    }
    return tryPublish(candidate, mDistance);
}
bool EditorCameraController::TryPan(float32 deltaX, float32 deltaY, float32 heightLogical, float32 aspect) noexcept
{
    if (math::IsFinite(deltaX) == false || math::IsFinite(deltaY) == false || math::IsFinite(heightLogical) == false ||
        heightLogical <= 0 || math::IsFinite(aspect) == false || aspect <= 0)
    {
        return false;
    }
    const camera::CameraLens& lens = mSample.Lens;
    const float32 halfFov = lens.VerticalFov * 0.5F;
    const float32 span = lens.Projection == camera::CameraProjection::OrthographicFinite
                             ? math::Max(lens.VerticalSpan, lens.MinimumVisibleWidth / aspect)
                             : 2 * mDistance * math::Sin(halfFov) / math::Cos(halfFov);
    return TryFly({-deltaX, deltaY, 0}, span / heightLogical);
}
bool EditorCameraController::TryFly(math::Vector3 movementLocal, float32 distance) noexcept
{
    if (math::IsFinite(movementLocal) == false || math::IsFinite(distance) == false)
    {
        return false;
    }
    camera::CameraSample candidate = mSample;
    candidate.Pose.Position =
        candidate.Pose.Position +
        ConvertToWorld(math::Rotate(candidate.Pose.Orientation, movementLocal)) * static_cast<float64>(distance);
    return tryPublish(candidate, mDistance);
}
bool EditorCameraController::TryDolly(float32 steps) noexcept
{
    if (math::IsFinite(steps) == false)
    {
        return false;
    }
    const float32 factor = math::Clamp(1 - steps * 0.15F, 0.1F, 10.0F);
    return TrySetDistance(math::Clamp(mDistance * factor, MINIMUM_DISTANCE, MAXIMUM_DISTANCE));
}
bool EditorCameraController::TrySetDistance(float32 distance) noexcept
{
    if (math::IsFinite(distance) == false || distance < MINIMUM_DISTANCE || distance > MAXIMUM_DISTANCE)
    {
        return false;
    }
    camera::CameraSample candidate = mSample;
    if (candidate.Lens.Projection == camera::CameraProjection::OrthographicFinite)
    {
        const float32 factor = distance / mDistance;
        candidate.Lens.VerticalSpan *= factor;
        candidate.Lens.MinimumVisibleWidth *= factor;
    }
    else
    {
        candidate.Pose.Position =
            GetPivotWorld() -
            ConvertToWorld(math::Rotate(candidate.Pose.Orientation, {0, 0, -1})) * static_cast<float64>(distance);
        candidate.Lens.NearPlane = math::Min(candidate.Lens.NearPlane, math::Max(0.0000001F, distance * 0.01F));
    }
    return tryPublish(candidate, distance);
}
bool EditorCameraController::TryFrame(const math::Aabb3& boundsWorld, float32 aspect) noexcept
{
    if (math::IsFinite(boundsWorld) == false || math::IsEmpty(boundsWorld) == true || math::IsFinite(aspect) == false ||
        aspect <= 0)
    {
        return false;
    }
    const math::Vector3 centerWorld = (boundsWorld.Min + boundsWorld.Max) * 0.5F;
    const float32 radius = math::Max(MINIMUM_DISTANCE, math::Length(boundsWorld.Max - centerWorld));
    camera::CameraSample candidate = mSample;
    const float32 verticalHalfFov = candidate.Lens.Projection == camera::CameraProjection::OrthographicFinite
                                        ? 0.5F
                                        : candidate.Lens.VerticalFov * 0.5F;
    const float32 horizontalHalfFov = math::Atan2(math::Sin(verticalHalfFov) * aspect, math::Cos(verticalHalfFov));
    // Ten percent margin around the enclosing sphere; fit the limiting viewport axis.
    const float32 distance = radius * 1.1F / math::Sin(math::Min(verticalHalfFov, horizontalHalfFov));
    if (candidate.Lens.Projection == camera::CameraProjection::OrthographicFinite)
    {
        candidate.Lens.VerticalSpan = radius * 2.2F / math::Min(1.0F, aspect);
        candidate.Lens.MinimumVisibleWidth = 0;
        candidate.Lens.FarPlane = math::Max(candidate.Lens.FarPlane, distance + radius * 2);
    }
    else
    {
        candidate.Lens.Projection = camera::CameraProjection::PerspectiveInfinite;
    }
    candidate.Lens.NearPlane = math::Clamp(radius * 0.001F, 0.0000001F, 0.01F);
    candidate.Pose.Position =
        ConvertToWorld(centerWorld) -
        ConvertToWorld(math::Rotate(candidate.Pose.Orientation, {0, 0, -1})) * static_cast<float64>(distance);
    return tryPublish(candidate, distance);
}
} // namespace ludus::editor
