#pragma once

#include <ludus/foundation/base/core.h>

#include <ludus/foundation/math/geometry.hpp>
#include <ludus/foundation/math/quaternion.hpp>
#include <ludus/foundation/math/vector.hpp>
#include <ludus/gameplay/camera/evaluation.hpp>

namespace ludus::editor
{
/// Private editor navigation state. Inputs are logical pixels, radians or world units;
/// failures preserve the accepted C0 publication. No World, Qt or GPU ownership.
class EditorCameraController final
{
public:
    EditorCameraController() noexcept;
    [[nodiscard]] LUDUS_INLINE const gameplay::camera::CameraSample& GetSample() const noexcept
    {
        return mSample;
    }
    [[nodiscard]] LUDUS_INLINE foundation::float32 GetDistance() const noexcept
    {
        return mDistance;
    }
    [[nodiscard]] foundation::math::Vector3d GetPivotWorld() const noexcept;
    [[nodiscard]] foundation::float32 GetYawDegrees() const noexcept;
    [[nodiscard]] foundation::float32 GetPitchDegrees() const noexcept;
    [[nodiscard]] bool TryAdopt(const gameplay::camera::CameraSample& sample, foundation::float32 distance) noexcept;
    /// Set a world pivot, yaw/pitch pair in degrees, and absolute focus distance in world units.
    [[nodiscard]] bool TrySetOrbit(foundation::math::Vector3d pivotWorld,
                                   foundation::math::Vector2 orbitDegrees,
                                   foundation::float32 distance) noexcept;
    [[nodiscard]] bool TryRotate(foundation::float32 deltaX, foundation::float32 deltaY, bool isOrbit) noexcept;
    [[nodiscard]] bool TryPan(foundation::float32 deltaX,
                              foundation::float32 deltaY,
                              foundation::float32 heightLogical,
                              foundation::float32 aspect = 1) noexcept;
    [[nodiscard]] bool TrySetDistance(foundation::float32 distance) noexcept;
    [[nodiscard]] bool TryDolly(foundation::float32 steps) noexcept;
    [[nodiscard]] bool TryFly(foundation::math::Vector3 movementLocal, foundation::float32 distance) noexcept;
    [[nodiscard]] bool TryFrame(const foundation::math::Aabb3& boundsWorld, foundation::float32 aspect) noexcept;

private:
    [[nodiscard]] bool tryPublish(const gameplay::camera::CameraSample& sample, foundation::float32 distance) noexcept;
    gameplay::camera::CameraSample mSample;
    foundation::float32 mDistance = 3;
};
} // namespace ludus::editor
