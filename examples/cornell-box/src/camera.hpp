#pragma once

#include <ludus/foundation/base/core.h>

#include <ludus/foundation/math/vector.hpp>
#include <ludus/gameplay/camera/evaluation.hpp>

namespace cornell
{
/// Renderer-owned world ray basis; each vector occupies one shader float4.
struct alignas(16) CameraRays final
{
    ludus::foundation::math::Vector4 Eye{};
    ludus::foundation::math::Vector4 Right{};
    ludus::foundation::math::Vector4 Up{};
    ludus::foundation::math::Vector4 Forward{};
};

/// Fixed C0 shot preserving the original eye and tan(vertical FOV / 2) = 0.4.
[[nodiscard]] ludus::gameplay::camera::CameraRigDefinition DefaultCamera() noexcept;

/// Extract perspective rays for the acquired physical framebuffer, with NDC Y up.
/// Uses origin zero and a 10,000-world-unit relative-position limit for this room.
/// Orthographic samples are unsupported; any failure preserves output.
[[nodiscard]] ludus::gameplay::camera::CameraStatus
TryBuildCameraRays(const ludus::gameplay::camera::CameraSample& sample,
                   ludus::foundation::uint32 width,
                   ludus::foundation::uint32 height,
                   CameraRays& output) noexcept;
} // namespace cornell
