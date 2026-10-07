#include "internal/camera_view.h"

#include <ludus/foundation/math/projection.hpp>
#include <ludus/foundation/math/quaternion.hpp>
#include <ludus/foundation/math/scalar.hpp>
#include <ludus/foundation/math/transform.hpp>
#include <ludus/gameplay/camera/evaluation.hpp>

#include <catch2/catch_test_macros.hpp>

#include <limits>

using namespace ludus::world_demo;
using namespace ludus::gameplay::camera;
namespace math = ludus::foundation::math;

TEST_CASE("Canonical orthographic mapping agrees with the procedural renderer on resize", "[camera][view]")
{
    CameraSample sample;
    sample.Pose.Position = {3, -4, 10};
    sample.Lens.MinimumVisibleWidth = 24;
    for (const auto viewport :
         {CameraViewport{0, 0, 1280, 720}, CameraViewport{0, 0, 480, 740}, CameraViewport{0, 0, 4096, 256}})
    {
        CameraRenderView view;
        PlanarCameraView planar;
        REQUIRE(TryBuildCameraView(sample, viewport, {}, 1.0e6, view) == CameraStatus::Success);
        REQUIRE(TryBuildPlanarCameraView(sample, viewport, {}, 1.0e6, planar) == CameraStatus::Success);
        const auto aspect = static_cast<ludus::foundation::float32>(viewport.Width) /
                            static_cast<ludus::foundation::float32>(viewport.Height);
        REQUIRE(math::NearlyEqual(planar.VerticalSpan, math::Max(14.0F, 24.0F / aspect), 1e-5F, 1e-6F));
        for (const auto uv : {math::Vector2{0, 0}, math::Vector2{1, 1}, math::Vector2{0.25F, 0.75F}})
        {
            const math::Vector4 point{planar.Center.X + (uv.X - 0.5F) * planar.VerticalSpan * aspect,
                                      planar.Center.Y - (uv.Y - 0.5F) * planar.VerticalSpan,
                                      0,
                                      1};
            math::Vector3 ndc;
            REQUIRE(math::TryPerspectiveDivide(view.ViewProjection * point, 0, ndc) == math::MathStatus::Success);
            math::Vector2 pixel;
            REQUIRE(math::TryNdcToFramebuffer(ndc,
                                              {0,
                                               0,
                                               static_cast<ludus::foundation::float32>(viewport.Width),
                                               static_cast<ludus::foundation::float32>(viewport.Height)},
                                              pixel) == math::MathStatus::Success);
            REQUIRE(math::NearlyEqual(pixel.X,
                                      uv.X * static_cast<ludus::foundation::float32>(viewport.Width),
                                      0.001F,
                                      1e-6F));
            REQUIRE(math::NearlyEqual(pixel.Y,
                                      uv.Y * static_cast<ludus::foundation::float32>(viewport.Height),
                                      0.001F,
                                      1e-6F));
        }
    }
}

TEST_CASE("Renderer uses rigid inverse, relative origin and canonical reverse Z", "[camera][view]")
{
    CameraSample sample;
    sample.Pose.Position = {1e12 + 0.25, 0, 0};
    REQUIRE(math::TryFromAxisAngle({0, 1, 0}, math::kHalfPiF, sample.Pose.Orientation) == math::MathStatus::Success);
    sample.Lens.Projection = CameraProjection::PerspectiveFinite;
    sample.Lens.NearPlane = 0.5F;
    sample.Lens.FarPlane = 20;
    CameraRenderView view;
    REQUIRE(TryBuildCameraView(sample, {13, 27, 800, 600}, {1e12, 0, 0}, 100, view) == CameraStatus::Success);
    REQUIRE(view.Origin.X == 1e12);
    REQUIRE(view.Viewport.X == 13);
    const auto forward = math::Rotate(sample.Pose.Orientation, {0, 0, -1});
    for (const auto depth : {0.5F, 20.0F})
    {
        const auto point = math::Vector3{0.25F, 0, 0} + forward * depth;
        math::Vector3 ndc;
        REQUIRE(math::TryPerspectiveDivide(view.ViewProjection * math::Vector4{point.X, point.Y, point.Z, 1}, 0, ndc) ==
                math::MathStatus::Success);
        REQUIRE(math::Abs(ndc.X) < 1e-5F);
        REQUIRE(math::NearlyEqual(ndc.Z, depth == 0.5F ? 1.0F : 0.0F, 1e-5F, 1e-6F));
    }
    sample.Lens.Projection = CameraProjection::PerspectiveInfinite;
    REQUIRE(TryBuildCameraView(sample, {0, 0, 800, 600}, {1e12, 0, 0}, 100, view) == CameraStatus::Success);
    math::Vector3 ndc;
    const auto point = math::Vector3{0.25F, 0, 0} + forward * 10.0F;
    REQUIRE(math::TryPerspectiveDivide(view.ViewProjection * math::Vector4{point.X, point.Y, point.Z, 1}, 0, ndc) ==
            math::MathStatus::Success);
    REQUIRE(math::NearlyEqual(ndc.Z, 0.05F, 1e-6F, 1e-6F));
}

TEST_CASE("Renderer rejects invalid ranges and unsupported procedural views transactionally", "[camera][view]")
{
    CameraSample sample;
    CameraRenderView view;
    view.Origin.X = 42;
    REQUIRE(TryBuildCameraView(sample, {}, {}, 100, view) == CameraStatus::InvalidInput);
    REQUIRE(view.Origin.X == 42);
    sample.Pose.Position.X = 101;
    REQUIRE(TryBuildCameraView(sample, {0, 0, 800, 600}, {}, 100, view) == CameraStatus::OutOfRange);
    REQUIRE(view.Origin.X == 42);
    sample.Pose.Position.X = 0;
    sample.Lens.VerticalSpan = std::numeric_limits<ludus::foundation::float32>::max();
    REQUIRE(TryBuildCameraView(sample, {0, 0, 800, 1}, {}, 100, view) == CameraStatus::OutOfRange);
    REQUIRE(view.Origin.X == 42);
    sample = {};
    PlanarCameraView planar{ .Center = {17, 18} };
    REQUIRE(TryBuildPlanarCameraView(sample, {1, 0, 800, 600}, {}, 100, planar) == CameraStatus::InvalidInput);
    sample.Pose.Orientation = {0, 1, 0, 0};
    REQUIRE(TryBuildPlanarCameraView(sample, {0, 0, 800, 600}, {}, 100, planar) == CameraStatus::InvalidInput);
    REQUIRE(planar.Center.X == 17);
    sample.Pose.Orientation = {0, 0, 0, -1};
    REQUIRE(TryBuildPlanarCameraView(sample, {0, 0, 800, 600}, {}, 100, planar) == CameraStatus::Success);
    sample.Lens.Projection = CameraProjection::PerspectiveInfinite;
    sample.Lens.NearPlane = 1;
    REQUIRE(TryBuildPlanarCameraView(sample, {0, 0, 800, 600}, {}, 100, planar) == CameraStatus::InvalidInput);
}

TEST_CASE("Immediate follow has no render-rate dependent integration or second interpolation", "[camera][cadence]")
{
    const CameraRigDefinition definition{ .Recipe = CameraRecipe::OrthographicFollow };
    for (const ludus::foundation::uint64 rate : {30U, 60U, 120U, 240U})
    {
        CameraSample sample;
        for (ludus::foundation::uint64 frame = 0; frame < rate * 2; ++frame)
        {
            const auto scaled = frame * 60;
            const auto tick = scaled / rate;
            const auto alpha =
                static_cast<ludus::foundation::float32>(scaled % rate) / static_cast<ludus::foundation::float32>(rate);
            const auto previous = tick == 0 ? 0 : tick - 1;
            const CameraTargetSample target
            {
                .Position = {static_cast<ludus::foundation::float64>(previous) +
                                 static_cast<ludus::foundation::float64>(alpha),
                             0,
                             0},
            };
            REQUIRE(TryEvaluateCamera(definition,
                                      {
                                          .PreviousTick = previous,
                                          .CurrentTick = tick,
                                          .RenderAlpha = alpha,
                                          .PresentationSequence = frame,
                                      },
                                      &target,
                                      sample) == CameraStatus::Success);
            REQUIRE(sample.Pose.Position.X == target.Position.X);
        }
    }
}
