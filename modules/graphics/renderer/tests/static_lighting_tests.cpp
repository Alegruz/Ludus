#include <ludus/graphics/renderer/static_lighting.hpp>

#include <ludus/content/scene_format/luscn.h>
#include <ludus/foundation/containers/array.hpp>
#include <ludus/foundation/containers/static_array.hpp>
#include <ludus/foundation/math/scalar.hpp>
#include <ludus/graphics/renderer/lighting.hpp>
#include <ludus/runtime/resources/scene.h>

#include <catch2/catch_test_macros.hpp>
#include <cstring>

namespace
{
using namespace ludus::foundation;
namespace rr = ludus::graphics::renderer;
namespace rhi = ludus::graphics::rhi;
namespace resources = ludus::resources;
namespace math = ludus::foundation::math;
constexpr usize SYNTHETIC_EMITTER_COUNT = 13;
constexpr usize TRIANGLE_CORNERS = 3;
constexpr usize PATCH_VERTICES = 4;
constexpr usize PATCH_INDICES = 6;
resources::SceneDefinition MakeBakeScene(bool hasAreaEmitters, bool hasBlocker, bool hasPointLight)
{
    resources::SceneDefinition scene;
    REQUIRE(scene.Textures.TryResize(1) == true);
    scene.Textures[0].Width = scene.Textures[0].Height = 1;
    REQUIRE(scene.Textures[0].Pixels.TryResize(4, 255) == true);
    REQUIRE(scene.Materials.TryResize(2) == true);
    scene.Materials[0].Texture = 0;
    scene.Materials[0].Roughness = 1;
    scene.Materials[1].Emission = hasAreaEmitters == true ? math::Vector3{10, 20, 30} : math::Vector3{};
    scene.Materials[1].AreaSourceId = 20;
    scene.Materials[1].IsEmissionTwoSided = false;
    REQUIRE(scene.Meshes.TryResize(2) == true);
    for (resources::SceneMesh& mesh : scene.Meshes)
    {
        REQUIRE(mesh.Positions.TryResize(TRIANGLE_CORNERS) == true);
        REQUIRE(mesh.Normals.TryResize(TRIANGLE_CORNERS) == true);
        REQUIRE(mesh.Texcoords.TryResize(TRIANGLE_CORNERS) == true);
        REQUIRE(mesh.Indices.TryResize(TRIANGLE_CORNERS) == true);
        for (usize i = 0; i < TRIANGLE_CORNERS; ++i)
        {
            mesh.Indices[i] = static_cast<uint32>(i);
        }
    }
    scene.Meshes[0].Positions[0] = {-1, -1, 0};
    scene.Meshes[0].Positions[1] = {1, -1, 0};
    scene.Meshes[0].Positions[2] = {0, 2, 0};
    scene.Meshes[1].Positions[0] = {-.3F, -.3F, 0};
    scene.Meshes[1].Positions[1] = {0, .3F, 0};
    scene.Meshes[1].Positions[2] = {.3F, -.3F, 0};
    for (usize i = 0; i < TRIANGLE_CORNERS; ++i)
    {
        scene.Meshes[0].Normals[i] = {0, 0, 1};
        scene.Meshes[1].Normals[i] = {0, 0, -1};
    }
    const usize objectCount =
        1 + (hasAreaEmitters == true ? SYNTHETIC_EMITTER_COUNT : 0) + (hasBlocker == true ? 1 : 0);
    REQUIRE(scene.Objects.TryResize(objectCount) == true);
    scene.Objects[0].SourceId = 1;
    for (usize i = 1; i <= (hasAreaEmitters == true ? SYNTHETIC_EMITTER_COUNT : 0); ++i)
    {
        scene.Objects[i].SourceId = i + 1;
        scene.Objects[i].Mesh = 1;
        scene.Objects[i].Material = 1;
        scene.Objects[i].Transform.Translation = {float32(i) * .1F - .7F, 0, 2};
    }
    if (hasBlocker == true)
    {
        resources::SceneObject& blocker = scene.Objects[objectCount - 1];
        blocker.SourceId = 100;
        blocker.Mesh = 1;
        blocker.Material = hasPointLight == true ? 0 : 1;
        blocker.Transform.Columns[0].X = 1000;
        blocker.Transform.Columns[1].Y = 1000;
        blocker.Transform.Translation.Z = 1;
    }
    if (hasPointLight == true)
    {
        REQUIRE(scene.Lights.TryResize(1) == true);
        scene.Lights[0].SourceId = 200;
        scene.Lights[0].Kind = resources::SceneLightKind::Point;
        scene.Lights[0].IntensityRgb = {50, 0, 0};
        scene.Lights[0].PositionWorld = {0, 0, .5F};
        scene.Lights[0].Range = 10;
    }
    return scene;
}
rr::StaticLightingBakeDescription MakeBakeDescription(uint32 samples = 256)
{
    rr::StaticLightingBakeDescription description;
    description.SceneRevision = 7;
    description.DependencyRevision = 11;
    description.SamplesPerTriangle = samples;
    description.MaxDepth = 2;
    return description;
}
rr::StaticLightingChart CompleteBake(rr::StaticLightingBake& inoutBake, uint32 step)
{
    rhi::RasterStatus status = rhi::RasterStatus::Pending;
    while (status == rhi::RasterStatus::Pending)
    {
        status = inoutBake.Step(step);
    }
    REQUIRE(status == rhi::RasterStatus::Ready);
    rr::StaticLightingChart chart;
    REQUIRE(inoutBake.GetChart(0, chart) == rhi::RasterStatus::Ready);
    return chart;
}
math::Vector3 ReadChart(const rr::StaticLightingChart& chart)
{
    return {float32(chart.Pixels[0]) / 255 * chart.Scale.X,
            float32(chart.Pixels[1]) / 255 * chart.Scale.Y,
            float32(chart.Pixels[2]) / 255 * chart.Scale.Z};
}
} // namespace
TEST_CASE("Static area bake lights all thirteen emitters, respects blockers and preserves pending charts",
          "[renderer][static-lighting]")
{
    resources::SceneDefinition scene = MakeBakeScene(true, false, false);
    rr::StaticLightingBake bake;
    REQUIRE(bake.Initialize(scene, MakeBakeDescription()) == rhi::RasterStatus::Pending);
    rr::StaticLightingChart pending;
    pending.SourceId = 999;
    REQUIRE(bake.GetChart(0, pending) == rhi::RasterStatus::NotReady);
    REQUIRE(pending.SourceId == 999);
    const rr::StaticLightingChart chart = CompleteBake(bake, 7);
    const math::Vector3 lit = ReadChart(chart);
    REQUIRE(lit.X > 1);
    REQUIRE(lit.Z > lit.X * 2.5F);
    REQUIRE(chart.SourceId == 1);
    REQUIRE(chart.SceneRevision == 7);
    REQUIRE(chart.DependencyRevision == 11);
    REQUIRE(chart.Width == 4);
    REQUIRE(chart.Height == 4);
    for (usize i = 0; i < chart.Pixels.GetSize(); i += 4)
    {
        REQUIRE(chart.Pixels[i] == chart.Pixels[0]);
        REQUIRE(chart.Pixels[i + 3] == 255);
    }
    rr::StaticLightingChart emitter;
    REQUIRE(bake.GetChart(SYNTHETIC_EMITTER_COUNT, emitter) == rhi::RasterStatus::Ready);
    REQUIRE(emitter.SourceId == SYNTHETIC_EMITTER_COUNT + 1);
    resources::SceneDefinition blockedScene = MakeBakeScene(true, true, false);
    blockedScene.Materials[1].IsEmissionTwoSided = false;
    // Absorbing blocker uses its own material; its downward orientation faces the receiver.
    REQUIRE(blockedScene.Materials.TryResize(3) == true);
    blockedScene.Objects[blockedScene.Objects.GetSize() - 1].Material = 2;
    rr::StaticLightingBake blocked;
    REQUIRE(blocked.Initialize(blockedScene, MakeBakeDescription()) == rhi::RasterStatus::Pending);
    const math::Vector3 dark = ReadChart(CompleteBake(blocked, 17));
    REQUIRE(dark.X < lit.X * .01F);
    REQUIRE(dark.Z < lit.Z * .01F);
}
TEST_CASE("Static bake respects mirrored geometric emitter sidedness", "[renderer][static-lighting]")
{
    resources::SceneDefinition scene = MakeBakeScene(true, false, false);
    for (usize i = 1; i < scene.Objects.GetSize(); ++i)
    {
        scene.Objects[i].Transform.Columns[2].Z = -1;
    }
    {
        rr::StaticLightingBake oneSided;
        REQUIRE(oneSided.Initialize(scene, MakeBakeDescription()) == rhi::RasterStatus::Pending);
        REQUIRE(ReadChart(CompleteBake(oneSided, 13)).X == 0);
    }
    // Destroy the worker job before mutating its borrowed source.
    scene.Materials[1].IsEmissionTwoSided = true;
    rr::StaticLightingBake twoSided;
    REQUIRE(twoSided.Initialize(scene, MakeBakeDescription()) == rhi::RasterStatus::Pending);
    REQUIRE(ReadChart(CompleteBake(twoSided, 13)).X > 1);
}
TEST_CASE("Static bake separates analytic direct from indirect and is independent of step partition",
          "[renderer][static-lighting]")
{
    resources::SceneDefinition emptyScene = MakeBakeScene(false, false, true);
    rr::StaticLightingBake directOnly;
    REQUIRE(directOnly.Initialize(emptyScene, MakeBakeDescription()) == rhi::RasterStatus::Pending);
    const math::Vector3 direct = ReadChart(CompleteBake(directOnly, 256));
    REQUIRE(direct.X == 0);
    resources::SceneDefinition scene = MakeBakeScene(false, true, true);
    rr::StaticLightingBake first;
    rr::StaticLightingBake second;
    REQUIRE(first.Initialize(scene, MakeBakeDescription()) == rhi::RasterStatus::Pending);
    REQUIRE(second.Initialize(scene, MakeBakeDescription()) == rhi::RasterStatus::Pending);
    const rr::StaticLightingChart chart = CompleteBake(first, 1);
    const rr::StaticLightingChart same = CompleteBake(second, 19);
    REQUIRE(ReadChart(chart).X > 1);
    REQUIRE(ReadChart(chart).Y == 0);
    REQUIRE(chart.Scale == same.Scale);
    REQUIRE(chart.Pixels.GetSize() == same.Pixels.GetSize());
    REQUIRE(std::memcmp(chart.Pixels.GetDataOrNull(), same.Pixels.GetDataOrNull(), chart.Pixels.GetSize()) == 0);
}
TEST_CASE("Static bake rejects budgets and malformed chart inputs without binding a job", "[renderer][static-lighting]")
{
    resources::SceneDefinition scene = MakeBakeScene(true, false, false);
    rr::StaticLightingBake bake;
    rr::StaticLightingBakeDescription description = MakeBakeDescription();
    description.CpuByteBudget = 1;
    REQUIRE(bake.Initialize(scene, description) == rhi::RasterStatus::CapacityExceeded);
    description = MakeBakeDescription(1);
    REQUIRE(scene.Meshes[0].BakeTexcoords.TryResize(TRIANGLE_CORNERS) == true);
    REQUIRE(bake.Initialize(scene, description) == rhi::RasterStatus::Unsupported);
    scene.Meshes[0].BakeTexcoords.Clear();
    REQUIRE(bake.Initialize(scene, description) == rhi::RasterStatus::Pending);
    REQUIRE(bake.Initialize(scene, description) == rhi::RasterStatus::InvalidState);
    REQUIRE(bake.Step(0) == rhi::RasterStatus::InvalidState);
    static_cast<void>(CompleteBake(bake, 100));
}
TEST_CASE("Planar emitter centroid is independent of quad tessellation and cube projections preserve depth",
          "[renderer][lighting]")
{
    StaticArray<math::Vector3, PATCH_VERTICES> positions{{{-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, 1, 0}}};
    StaticArray<uint32, PATCH_INDICES> indices{0, 1, 2, 0, 2, 3};
    rr::PlanarAreaLightDescription description;
    description.PositionsMesh = {positions.GetData(), positions.GetSize()};
    description.Indices = {indices.GetData(), indices.GetSize()};
    description.Emission = {10, 20, 30};
    description.SourceId = 1;
    rr::DirectLight light;
    REQUIRE(rr::BuildPlanarAreaLight(description, light) == rhi::RasterStatus::Ready);
    REQUIRE(light.Kind == rr::LightKind::AreaOneSided);
    REQUIRE(light.Position == math::Vector3{});
    REQUIRE(light.Direction.Z == 1);
    REQUIRE(light.Intensity == 120);
    REQUIRE(light.Color.Z == 1);
    indices = {0, 1, 3, 1, 2, 3};
    rr::DirectLight same;
    REQUIRE(rr::BuildPlanarAreaLight(description, same) == rhi::RasterStatus::Ready);
    REQUIRE(same.Position == light.Position);
    REQUIRE(same.Intensity == light.Intensity);
    positions[3].Z = 1;
    REQUIRE(rr::BuildPlanarAreaLight(description, same) == rhi::RasterStatus::Unsupported);
    REQUIRE(same.Intensity == light.Intensity);
    constexpr float32 NEAR_METRES = .1F;
    constexpr float32 FAR_METRES = 10;
    StaticArray<math::Vector3, rr::POINT_SHADOW_FACES> axes{
        {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
    for (uint32 face = 0; face < rr::POINT_SHADOW_FACES; ++face)
    {
        math::Matrix4 worldToClip;
        REQUIRE(rr::BuildPointShadowMatrix({}, face, NEAR_METRES, FAR_METRES, worldToClip) == rhi::RasterStatus::Ready);
        const math::Vector3 nearWorld = axes[face] * NEAR_METRES;
        const math::Vector3 farWorld = axes[face] * FAR_METRES;
        const math::Vector4 nearClip = worldToClip * math::Vector4{nearWorld.X, nearWorld.Y, nearWorld.Z, 1};
        const math::Vector4 farClip = worldToClip * math::Vector4{farWorld.X, farWorld.Y, farWorld.Z, 1};
        REQUIRE(math::Abs(nearClip.Z / nearClip.W) < 1e-5F);
        REQUIRE(math::Abs(farClip.Z / farClip.W - 1) < 1e-5F);
    }
}

TEST_CASE("Static transport copies current per-object World material terms", "[renderer][static-lighting]")
{
    resources::SceneDefinition scene = MakeBakeScene(false, true, true);
    Array<rr::StaticLightingSurfaceDescription> surfaces;
    REQUIRE(surfaces.TryResize(scene.Objects.GetSize()) == true);
    for (rr::StaticLightingSurfaceDescription& surface : surfaces)
    {
        surface.Texture = 0;
        surface.Roughness = 1;
    }
    rr::StaticLightingBakeDescription description = MakeBakeDescription();
    description.Surfaces = {surfaces.GetData(), surfaces.GetSize()};
    rr::StaticLightingBake white;
    REQUIRE(white.Initialize(scene, description) == rhi::RasterStatus::Pending);
    surfaces[1].Tint = {0, 0, 0, 1}; // Copied overrides may change after Initialize.
    const math::Vector3 reflected = ReadChart(CompleteBake(white, 31));
    REQUIRE(reflected.X > 1);
    rr::StaticLightingBake dark;
    REQUIRE(dark.Initialize(scene, description) == rhi::RasterStatus::Pending);
    REQUIRE(ReadChart(CompleteBake(dark, 31)).X < reflected.X * .1F);
    description.Surfaces = {surfaces.GetData(), 1};
    rr::StaticLightingBake invalid;
    REQUIRE(invalid.Initialize(scene, description) == rhi::RasterStatus::InvalidDescription);
}
