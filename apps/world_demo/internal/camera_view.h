#pragma once

#include <ludus/foundation/base/core.h>

#include <ludus/foundation/math/matrix.hpp>
#include <ludus/foundation/math/vector.hpp>
#include <ludus/gameplay/camera/camera.hpp>

namespace ludus::world_demo
{
/// Physical framebuffer rectangle; positive extents, top-left origin, Y down.
struct CameraViewport final
{
    foundation::uint32 X = 0;
    foundation::uint32 Y = 0;
    foundation::uint32 Width = 0;
    foundation::uint32 Height = 0;
};
/// Renderer-owned canonical matrices; no backend adjustment, jitter or history.
struct CameraRenderView final
{
    foundation::math::Vector3d Origin{};
    CameraViewport Viewport{};
    foundation::math::Matrix4 View{};
    foundation::math::Matrix4 Projection{};
    foundation::math::Matrix4 ViewProjection{};
    foundation::float32 VerticalSpan = 0;
};
/// Procedural rectangle uniforms expressed relative to the same render origin.
struct PlanarCameraView final
{
    foundation::math::Vector2 Center{};
    foundation::float32 VerticalSpan = 0;
};
/// Construct a rigid inverse and checked reverse-Z projection. Output unchanged on failure.
[[nodiscard]] gameplay::camera::CameraStatus TryBuildCameraView(const gameplay::camera::CameraSample& sample,
                                                                CameraViewport viewport,
                                                                foundation::math::Vector3d origin,
                                                                foundation::float64 maxRelative,
                                                                CameraRenderView& output) noexcept;
/// Adapt the checked view to the demo's XY identity-rotation orthographic renderer.
/// Reject perspective, rotation and viewport offsets that its fullscreen shader cannot honor.
[[nodiscard]] gameplay::camera::CameraStatus TryBuildPlanarCameraView(const gameplay::camera::CameraSample& sample,
                                                                      CameraViewport viewport,
                                                                      foundation::math::Vector3d origin,
                                                                      foundation::float64 maxRelative,
                                                                      PlanarCameraView& output) noexcept;
} // namespace ludus::world_demo
