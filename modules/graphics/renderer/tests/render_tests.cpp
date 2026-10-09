#include "internal/readback.h"
#include "l0_scene.h"
#include "l1_scene.h"
#include "l2_scene.h"
#include "renderer_composite.h"
#include "renderer_flat.h"
#include "renderer_overlay.h"
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <ludus/foundation/containers/static_array.hpp>
#include <ludus/graphics/renderer/renderer.hpp>
#include <ludus/graphics/renderer/views.hpp>
#include <ludus/graphics/rhi/device.h>
#include <ludus/graphics/rhi/lifetime.h>
#include <ludus/graphics/rhi/raster.h>
namespace rhi = ludus::graphics::rhi;
namespace rr = ludus::graphics::renderer;
using namespace ludus::foundation;
TEST_CASE("L0 optimized packets match unsorted direct pixels with depth, errors and painter overlays",
          "[renderer][gpu]")
{
    rhi::Shutdown();
    struct Guard
    {
        ~Guard()
        {
            rhi::Shutdown();
        }
    } guard;
    rhi::DeviceHandle device;
    rhi::SurfaceHandle surface;
    rhi::StartupInfo failure;
    rhi::DeviceDescription description;
    description.Required.PortableRaster = true;
    const auto started = rhi::CreateDevice({}, { .Width = 96, .Height = 64 }, description, device, surface, &failure);
#if defined(LUDUS_TEST_METAL)
    if (started != rhi::DeviceStatus::Ready && failure.Error == rhi::StartupError::AdapterUnavailable)
    {
        SKIP("No Metal adapter on this host");
    }
#endif
    REQUIRE(started == rhi::DeviceStatus::Ready);
    rr::Renderer renderer;
    REQUIRE(renderer.Initialize(device,
                                ludus::shaders::renderer_flat::Vertex(),
                                ludus::shaders::renderer_flat::Fragment()) == rhi::RasterStatus::Ready);
    rr::Mesh quad, triangle;
    REQUIRE(renderer.CreateMesh({ludus::qa::L0_QUAD, 4, ludus::qa::L0_INDICES, 6}, quad) == rhi::RasterStatus::Ready);
    REQUIRE(renderer.CreateMesh({ludus::qa::L0_TRIANGLE, 3, ludus::qa::L0_INDICES, 3}, triangle) ==
            rhi::RasterStatus::Ready);
    uint8 images[2][96 * 64 * 4]{};
    rr::ViewReport reports[2];
    for (usize variant = 0; variant < 2; ++variant)
    {
        rr::Snapshot snapshot;
        rr::PreparedView view;
        REQUIRE(ludus::qa::BuildL0Scene(renderer, quad, triangle, variant == 1, snapshot, view, reports[variant]) ==
                rhi::RasterStatus::Ready);
        rhi::SubmissionToken completion;
        REQUIRE(renderer.Submit(view, surface, completion) == rhi::RasterStatus::Ready);
        REQUIRE(rhi::backend::ReadHeadlessPixels(images[variant]));
        REQUIRE(renderer.Release(view) == rhi::RasterStatus::Ready);
        REQUIRE(renderer.Release(snapshot) == rhi::RasterStatus::Ready);
    }
    CHECK(reports[0].Culled == 1);
    CHECK(reports[0].Draws < reports[1].Draws);
    CHECK(std::memcmp(images[0], images[1], sizeof(images[0])) == 0);
    // Independent interior samples prevent a blank/equally broken image from passing.
    const usize red = (usize{20} * 96 + 24) * 4;
    CHECK(images[0][red] == 255);
    CHECK(images[0][red + 1] == 0);
    const usize green = (usize{20} * 96 + 72) * 4;
    CHECK(images[0][green + 1] == 255);
    const usize background = (usize{32} * 96 + 12) * 4;
    CHECK(images[0][background] == 25);
    CHECK(images[0][background + 1] == 26);
    CHECK(images[0][background + 2] == 52);
    const usize overlay = (usize{32} * 96 + 48) * 4;
    CHECK(images[0][overlay] == 70);
    CHECK(images[0][overlay + 1] == 134);
    CHECK(images[0][overlay + 2] == 13);
    const usize error = (usize{45} * 96 + 48) * 4;
    CHECK(images[0][error] == 255);
    CHECK(images[0][error + 2] == 255);
}
TEST_CASE("L1 offscreen split views preserve neighbors, reverse-Z order and top-left UVs", "[renderer][views][gpu]")
{
    rhi::Shutdown();
    struct Guard
    {
        ~Guard()
        {
            rhi::Shutdown();
        }
    } guard;
    rhi::DeviceHandle device;
    rhi::SurfaceHandle surface;
    rhi::StartupInfo failure;
    rhi::DeviceDescription description;
    description.Required.PortableRaster = true;
    const auto started = rhi::CreateDevice({}, { .Width = 96, .Height = 64 }, description, device, surface, &failure);
#if defined(LUDUS_TEST_METAL)
    if (started != rhi::DeviceStatus::Ready && failure.Error == rhi::StartupError::AdapterUnavailable)
    {
        SKIP("No Metal adapter on this host");
    }
#endif
    REQUIRE(started == rhi::DeviceStatus::Ready);
    rr::Renderer renderer;
    REQUIRE(renderer.Initialize(device,
                                ludus::shaders::renderer_flat::Vertex(),
                                ludus::shaders::renderer_flat::Fragment()) == rhi::RasterStatus::Ready);
    REQUIRE(renderer.InitializeViews(ludus::shaders::renderer_composite::Vertex(),
                                     ludus::shaders::renderer_composite::Fragment()) == rhi::RasterStatus::Ready);
    rr::Mesh mesh;
    REQUIRE(renderer.CreateMesh({ludus::qa::L1_QUAD, 4, ludus::qa::L1_INDICES, 6}, mesh) == rhi::RasterStatus::Ready);
    rr::Snapshot snapshot;
    REQUIRE(ludus::qa::BuildL1Snapshot(renderer, mesh, snapshot) == rhi::RasterStatus::Ready);
    rhi::TextureHandle targets[2];
    for (auto& target : targets)
    {
        REQUIRE(rhi::CreateTexture(device, {48, 48, rhi::RasterFormat::Rgba8Unorm, true}, {}, target) ==
                rhi::RasterStatus::Ready);
    }
    rr::Presentation presentations[2];
    REQUIRE(ludus::qa::PrepareL1Presentations(renderer, targets, 96, 64, presentations) == rhi::RasterStatus::Ready);
    uint8 images[2][96 * 64 * 4]{};
    for (usize variant = 0; variant < 2; ++variant)
    {
        rr::PreparedView views[2];
        rr::ViewReport report;
        for (usize i = 0; i < 2; ++i)
        {
            REQUIRE(ludus::qa::BuildL1View(renderer, snapshot, i == 1, variant == 1, variant == 1, views[i], report) ==
                    rhi::RasterStatus::Ready);
        }
        REQUIRE(rhi::BeginFrame(device, surface) == rhi::DeviceStatus::Ready);
        REQUIRE(ludus::qa::DrawL1Frame(renderer, views, targets, presentations) == rhi::RasterStatus::Ready);
        rhi::SubmissionToken completion;
        REQUIRE(rhi::EndFrame(device, completion) == rhi::RasterStatus::Ready);
        for (auto& view : views)
        {
            REQUIRE(renderer.Release(view) == rhi::RasterStatus::Ready);
        }
        REQUIRE(rhi::backend::ReadHeadlessPixels(images[variant]));
    }
    CHECK(std::memcmp(images[0], images[1], sizeof(images[0])) == 0);
    const auto color = [&](uint32 x, uint32 y, const core::StaticArray<uint8, 3>& rgb) {
        const auto offset = (static_cast<usize>(y) * 96 + static_cast<usize>(x)) * 4;
        CHECK(images[0][offset] == rgb[0]);
        CHECK(images[0][offset + 1] == rgb[1]);
        CHECK(images[0][offset + 2] == rgb[2]);
        CHECK(images[0][offset + 3] == 255);
    };
    color(24, 32, {255, 0, 0});
    color(72, 32, {255, 0, 0});
    color(4, 12, {0, 255, 0});
    color(52, 12, {0, 0, 255});
    color(42, 16, {255, 0, 255});
    color(90, 16, {255, 0, 255});
    color(42, 48, {0, 255, 0});
    color(90, 48, {0, 0, 255});
    color(24, 2, {0, 0, 0});
    color(72, 2, {0, 0, 0});
}
TEST_CASE("L1 explicit viewport/scissor preserves the outside surface and empty scissors draw nothing",
          "[renderer][views][gpu]")
{
    rhi::Shutdown();
    struct Guard
    {
        ~Guard()
        {
            rhi::Shutdown();
        }
    } guard;
    rhi::DeviceHandle device;
    rhi::SurfaceHandle surface;
    rhi::StartupInfo failure;
    rhi::DeviceDescription description;
    description.Required.PortableRaster = true;
    const auto started = rhi::CreateDevice({}, { .Width = 96, .Height = 64 }, description, device, surface, &failure);
#if defined(LUDUS_TEST_METAL)
    if (started != rhi::DeviceStatus::Ready && failure.Error == rhi::StartupError::AdapterUnavailable)
    {
        SKIP("No Metal adapter on this host");
    }
#endif
    REQUIRE(started == rhi::DeviceStatus::Ready);
    rr::Renderer renderer;
    REQUIRE(renderer.Initialize(device,
                                ludus::shaders::renderer_flat::Vertex(),
                                ludus::shaders::renderer_flat::Fragment()) == rhi::RasterStatus::Ready);
    REQUIRE(renderer.InitializeViews(ludus::shaders::renderer_composite::Vertex(),
                                     ludus::shaders::renderer_composite::Fragment()) == rhi::RasterStatus::Ready);
    rr::Mesh mesh;
    REQUIRE(renderer.CreateMesh({ludus::qa::L1_QUAD, 4, ludus::qa::L1_INDICES, 6}, mesh) == rhi::RasterStatus::Ready);
    rr::SceneItem item;
    item.Geometry = mesh;
    item.SourceId = 1;
    item.Transform.Translation.Z = .5F;
    rr::Snapshot snapshot;
    REQUIRE(renderer.CreateSnapshot(&item, 1, snapshot) == rhi::RasterStatus::Ready);
    rr::ViewDescription viewDescription;
    viewDescription.Depth = rr::DepthConvention::ReverseZ;
    rr::PreparedView view;
    rr::ViewReport report;
    REQUIRE(renderer.PrepareView(snapshot, viewDescription, view, report) == rhi::RasterStatus::Ready);
    for (const bool empty : {false, true})
    {
        REQUIRE(rhi::SetFrameTarget(device, surface, {96, 64, 1, 1, 0, 1}) == rhi::DeviceStatus::Ready);
        REQUIRE(rhi::BeginFrame(device, surface) == rhi::DeviceStatus::Ready);
        rhi::RasterPassDescription pass;
        pass.ColorLoad = rhi::RasterLoad::Load;
        pass.UseViewport = pass.UseScissor = true;
        pass.Viewport = {0, 0, 24, 24};
        pass.Scissor = {3, 3, empty ? 0U : 6U, 6};
        REQUIRE(renderer.DrawView(view, pass) == rhi::RasterStatus::Ready);
        rhi::SubmissionToken completion;
        REQUIRE(rhi::EndFrame(device, completion) == rhi::RasterStatus::Ready);
        uint8 pixels[96 * 64 * 4];
        REQUIRE(rhi::backend::ReadHeadlessPixels(pixels));
        const auto inside = (usize{4} * 96 + 4) * 4;
        CHECK(pixels[inside] == 255);
        CHECK(pixels[inside + 1] == (empty ? 255 : 0));
        CHECK(pixels[inside + 2] == (empty ? 0 : 255));
        for (const auto offset : {(usize{12} * 96 + 12) * 4, (usize{50} * 96 + 50) * 4})
        {
            CHECK(pixels[offset] == 255);
            CHECK(pixels[offset + 1] == 255);
            CHECK(pixels[offset + 2] == 0);
        }
    }
}
TEST_CASE("L1 Fill crops logical offscreen pixels once and matches direct output sampling", "[renderer][views][gpu]")
{
    rhi::Shutdown();
    struct Guard
    {
        ~Guard()
        {
            rhi::Shutdown();
        }
    } guard;
    rhi::DeviceHandle device;
    rhi::SurfaceHandle surface;
    rhi::StartupInfo failure;
    rhi::DeviceDescription deviceDescription;
    deviceDescription.Required.PortableRaster = true;
    const auto started =
        rhi::CreateDevice({}, { .Width = 96, .Height = 64 }, deviceDescription, device, surface, &failure);
#if defined(LUDUS_TEST_METAL)
    if (started != rhi::DeviceStatus::Ready && failure.Error == rhi::StartupError::AdapterUnavailable)
    {
        SKIP("No Metal adapter on this host");
    }
#endif
    REQUIRE(started == rhi::DeviceStatus::Ready);
    rr::Renderer renderer;
    REQUIRE(renderer.Initialize(device,
                                ludus::shaders::renderer_flat::Vertex(),
                                ludus::shaders::renderer_flat::Fragment()) == rhi::RasterStatus::Ready);
    REQUIRE(renderer.InitializeViews(ludus::shaders::renderer_composite::Vertex(),
                                     ludus::shaders::renderer_composite::Fragment()) == rhi::RasterStatus::Ready);
    rr::Mesh mesh;
    REQUIRE(renderer.CreateMesh({ludus::qa::L1_QUAD, 4, ludus::qa::L1_INDICES, 6}, mesh) == rhi::RasterStatus::Ready);
    rr::Snapshot snapshot;
    REQUIRE(ludus::qa::BuildL1Snapshot(renderer, mesh, snapshot) == rhi::RasterStatus::Ready);
    rhi::TextureHandle target;
    REQUIRE(rhi::CreateTexture(device, {96, 48, rhi::RasterFormat::Rgba8Unorm, true}, {}, target) ==
            rhi::RasterStatus::Ready);
    rr::ViewMapping mapping;
    REQUIRE(rr::ResolveViewMapping({96, 48, rr::ScreenFit::Fill}, {0, 0, 96, 64}, 96, 64, mapping) ==
            rhi::RasterStatus::Ready);
    rr::Presentation presentation;
    REQUIRE(renderer.PreparePresentation(target, mapping, presentation) == rhi::RasterStatus::Ready);
    rr::ProjectionDescription lens;
    lens.Kind = rr::ProjectionKind::Orthographic;
    lens.OrthographicHeight = 4;
    lens.Near = 1;
    lens.Far = 10;
    for (const auto space : {rr::ProjectionSpace::LogicalScreen, rr::ProjectionSpace::OutputRegion})
    {
        rr::ViewDescription description;
        REQUIRE(rr::BuildViewDescription(lens, mapping, description, space) == rhi::RasterStatus::Ready);
        rr::PreparedView view;
        rr::ViewReport report;
        REQUIRE(renderer.PrepareView(snapshot, description, view, report) == rhi::RasterStatus::Ready);
        REQUIRE(rhi::BeginFrame(device, surface) == rhi::DeviceStatus::Ready);
        rhi::RasterPassDescription pass;
        if (space == rr::ProjectionSpace::LogicalScreen)
        {
            pass.Color = target;
        }
        else
        {
            pass.UseViewport = pass.UseScissor = true;
            pass.Viewport = mapping.Viewport;
            pass.Scissor = mapping.Region;
        }
        REQUIRE(renderer.DrawView(view, pass) == rhi::RasterStatus::Ready);
        if (space == rr::ProjectionSpace::LogicalScreen)
        {
            REQUIRE(renderer.DrawPresentation(presentation) == rhi::RasterStatus::Ready);
        }
        rhi::SubmissionToken completion;
        REQUIRE(rhi::EndFrame(device, completion) == rhi::RasterStatus::Ready);
        REQUIRE(renderer.Release(view) == rhi::RasterStatus::Ready);
        uint8 pixels[96 * 64 * 4]{};
        REQUIRE(rhi::backend::ReadHeadlessPixels(pixels));
        const auto sample = [&](uint32 x, uint32 y, const core::StaticArray<uint8, 3>& rgb) {
            const auto offset = (static_cast<usize>(y) * 96 + x) * 4;
            CHECK(pixels[offset] == rgb[0]);
            CHECK(pixels[offset + 1] == rgb[1]);
            CHECK(pixels[offset + 2] == rgb[2]);
        };
        sample(48, 32, {255, 0, 0});
        sample(28, 32, {0, 255, 0}); // A second crop would incorrectly widen red into this sample.
        sample(8, 32, {0, 0, 255});
        sample(89, 11, {255, 0, 255});
    }
}
TEST_CASE("L2 R8 coverage orientation, painter scissors and clipped thick lines have independent pixel oracles",
          "[renderer][l2][gpu]")
{
    rhi::Shutdown();
    struct Guard
    {
        ~Guard()
        {
            rhi::Shutdown();
        }
    } guard;
    rhi::DeviceHandle device;
    rhi::SurfaceHandle surface;
    rhi::StartupInfo failure;
    rhi::DeviceDescription description;
    description.Required.PortableRaster = true;
    const auto started = rhi::CreateDevice({}, { .Width = 96, .Height = 64 }, description, device, surface, &failure);
#if defined(LUDUS_TEST_METAL)
    if (started != rhi::DeviceStatus::Ready && failure.Error == rhi::StartupError::AdapterUnavailable)
    {
        SKIP("No Metal adapter on this host");
    }
#endif
    REQUIRE(started == rhi::DeviceStatus::Ready);
    rr::OverlayRenderer overlays;
    const auto initialized = overlays.Initialize(device,
                                                 ludus::shaders::renderer_overlay::Vertex(),
                                                 ludus::shaders::renderer_overlay::Fragment());
    REQUIRE((initialized == rhi::RasterStatus::Ready || initialized == rhi::RasterStatus::Pending));
    auto setup = initialized;
    for (usize i = 0; i < 100000 && setup == rhi::RasterStatus::Pending; ++i)
    {
        setup = overlays.Poll();
    }
    REQUIRE(setup == rhi::RasterStatus::Ready);
    rr::OverlayList list;
    ludus::graphics::text::CoverageAtlas atlas;
    REQUIRE(ludus::qa::BuildL2List(list, atlas) == rhi::RasterStatus::Ready);
    rhi::TextureHandle texture;
    const auto published = atlas.Publish(device, texture);
    REQUIRE((published == rhi::RasterStatus::Ready || published == rhi::RasterStatus::Pending));
    bool textureReady = false;
    for (usize i = 0; i < 100000 && !textureReady; ++i)
    {
        REQUIRE(overlays.Poll() == rhi::RasterStatus::Ready);
        const auto status = rhi::GetStatus(device, texture);
        REQUIRE((status == rhi::RasterStatus::Ready || status == rhi::RasterStatus::Pending));
        textureReady = status == rhi::RasterStatus::Ready;
    }
    REQUIRE(textureReady);
    rr::PreparedOverlay prepared;
    REQUIRE(overlays.Prepare(list, texture, prepared) == rhi::RasterStatus::Pending);
    // Bounded nonblocking poll; native transfer completion may lag submission.
    bool ready = false;
    for (usize i = 0; i < 100000 && !ready; ++i)
    {
        REQUIRE(overlays.Poll() == rhi::RasterStatus::Ready);
        const auto status = overlays.GetStatus(prepared);
        REQUIRE((status == rhi::RasterStatus::Ready || status == rhi::RasterStatus::Pending));
        ready = status == rhi::RasterStatus::Ready;
    }
    REQUIRE(ready);
    // Logical atlas/CPU release cannot invalidate prepared version.
    REQUIRE(rhi::Destroy(device, texture) == rhi::RasterStatus::Ready);
    atlas.Reset();
    list.Clear();
    REQUIRE(rhi::BeginFrame(device, surface) == rhi::DeviceStatus::Ready);
    rhi::RasterPassDescription pass;
    pass.DepthStore = rhi::RasterStore::Store;
    REQUIRE(rhi::BeginRasterPass(device, pass) == rhi::RasterStatus::Ready);
    REQUIRE(overlays.Draw(prepared, pass) == rhi::RasterStatus::Ready);
    rhi::SubmissionToken completion;
    REQUIRE(rhi::EndFrame(device, completion) == rhi::RasterStatus::Ready);
    uint8 image[96 * 64 * 4];
    REQUIRE(rhi::backend::ReadHeadlessPixels(image));
    const auto color = [&](uint32 x, uint32 y, uint8 red, uint8 green, uint8 blue) {
        const usize offset = (static_cast<usize>(y) * 96 + x) * 4;
        CHECK(image[offset] == red);
        CHECK(image[offset + 1] == green);
        CHECK(image[offset + 2] == blue);
    };
    color(8, 48, 255, 0, 0);
    // UNorm destination-alpha blend precision differs: llvmpipe gives red 127,
    // Metal/Chromium give 128. Bound only that observed one-code-value rounding.
    const usize blend = (usize{48} * 96 + 24) * 4;
    CHECK(image[blend] >= 127);
    CHECK(image[blend] <= 128);
    CHECK(image[blend + 1] == 128);
    CHECK(image[blend + 2] == 0);
    color(40, 48, 0, 0, 0);
    color(80, 4, 255, 255, 255);
    color(80, 28, 0, 0, 0);
    color(60, 16, 0, 0, 255);
    color(32, 16, 0, 0, 0);
    REQUIRE(overlays.Release(prepared) == rhi::RasterStatus::Ready);
}
#include "l3_color.h"
#include "l3_scene.h"
#include "renderer_material.h"
TEST_CASE("L3 native material reload, color mips, mask and prewarmed targets preserve independent pixels",
          "[renderer][gpu][l3]")
{
    rhi::Shutdown();
    struct Guard
    {
        ~Guard()
        {
            rhi::Shutdown();
        }
    } guard;
    rhi::DeviceHandle device;
    rhi::SurfaceHandle surface;
    rhi::StartupInfo failure;
    rhi::DeviceDescription required;
    required.Required.PortableRaster = true;
    const auto started = rhi::CreateDevice({}, { .Width = 96, .Height = 64 }, required, device, surface, &failure);
#if defined(LUDUS_TEST_METAL)
    if (started != rhi::DeviceStatus::Ready && failure.Error == rhi::StartupError::AdapterUnavailable)
        SKIP("No Metal adapter");
#endif
    REQUIRE(started == rhi::DeviceStatus::Ready);
    rr::Renderer renderer;
    REQUIRE(renderer.Initialize(device,
                                ludus::shaders::renderer_flat::Vertex(),
                                ludus::shaders::renderer_flat::Fragment()) == rhi::RasterStatus::Ready);
    REQUIRE(renderer.InitializeViews(ludus::shaders::renderer_composite::Vertex(),
                                     ludus::shaders::renderer_composite::Fragment()) == rhi::RasterStatus::Ready);
    const auto wait = [&](auto handle) {
        auto status = renderer.GetStatus(handle);
        for (usize i = 0; i < 100000 && status == rhi::RasterStatus::Pending; ++i)
            status = renderer.GetStatus(handle);
        REQUIRE(status == rhi::RasterStatus::Ready);
    };
    rr::MaterialProgram program;
    const auto warmed = renderer.PrewarmMaterialProgram(ludus::shaders::renderer_material::Vertex(),
                                                        ludus::shaders::renderer_material::Fragment(),
                                                        program);
    REQUIRE((warmed == rhi::RasterStatus::Pending || warmed == rhi::RasterStatus::Ready));
    wait(program);
    rhi::TextureHandle texture, mask;
    const auto uploaded = rhi::CreateTexture(device,
                                             ludus::textures::l3_color::Description(),
                                             ludus::textures::l3_color::Upload(),
                                             texture);
    REQUIRE((uploaded == rhi::RasterStatus::Pending || uploaded == rhi::RasterStatus::Ready));
    auto ready = rhi::GetStatus(device, texture);
    for (usize i = 0; i < 100000 && ready == rhi::RasterStatus::Pending; ++i)
    {
        REQUIRE(rhi::PollLifetime(device) == rhi::RasterStatus::Ready);
        ready = rhi::GetStatus(device, texture);
    }
    REQUIRE(ready == rhi::RasterStatus::Ready);
    const uint8 maskBytes[]{255, 255, 255, 0, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255, 0};
    REQUIRE(rhi::CreateTexture(device, {2, 2, rhi::RasterFormat::Rgba8Unorm}, {maskBytes, 8}, mask) ==
            rhi::RasterStatus::Ready);
    rr::MaterialVersion materials[ludus::qa::L3_MATERIALS];
    for (usize i = 0; i < ludus::qa::L3_MATERIALS; ++i)
    {
        const auto status =
            renderer.CreateMaterial(program, ludus::qa::L3Material(i, i == 4 ? mask : texture), materials[i]);
        REQUIRE((status == rhi::RasterStatus::Ready || status == rhi::RasterStatus::Pending));
        wait(materials[i]);
    }
    REQUIRE(rhi::Destroy(device, texture) == rhi::RasterStatus::Ready);
    REQUIRE(rhi::Destroy(device, mask) == rhi::RasterStatus::Ready);
    rr::Mesh mesh;
    REQUIRE(ludus::qa::L3Mesh(renderer, mesh) == rhi::RasterStatus::Ready);
    rr::SceneItem items[8];
    ludus::qa::L3Items(mesh, materials, items);
    rr::Snapshot snapshot;
    REQUIRE(renderer.CreateSnapshot(items, 8, snapshot) == rhi::RasterStatus::Ready);
    rr::PreparedView prior;
    rr::ViewReport report;
    REQUIRE(renderer.PrepareView(snapshot, {}, prior, report) == rhi::RasterStatus::Ready);
    auto changed = ludus::qa::L3Material(2, {});
    changed.Tint = {0, 1, 0, 1};
    rr::MaterialVersion candidate;
    REQUIRE(renderer.CreateMaterial(program, changed, candidate) == rhi::RasterStatus::Ready);
    REQUIRE(renderer.PublishMaterial(candidate, materials[2]) == rhi::RasterStatus::Ready);
    changed.Tint.X = 2;
    CHECK(renderer.CreateMaterial(program, changed, candidate) == rhi::RasterStatus::InvalidDescription);
    // Old view still resolves red despite publication and an invalid subsequent reload.
    REQUIRE(rhi::BeginFrame(device, surface) == rhi::DeviceStatus::Ready);
    REQUIRE(renderer.DrawView(prior, {}) == rhi::RasterStatus::Ready);
    rhi::SubmissionToken completion;
    REQUIRE(rhi::EndFrame(device, completion) == rhi::RasterStatus::Ready);
    uint8 oldImage[96 * 64 * 4];
    REQUIRE(rhi::backend::ReadHeadlessPixels(oldImage));
    CHECK(oldImage[(48 * 96 + 16) * 4] == 255);
    CHECK(oldImage[(48 * 96 + 16) * 4 + 1] == 0);
    REQUIRE(renderer.Release(prior) == rhi::RasterStatus::Ready);
    REQUIRE(renderer.Release(snapshot) == rhi::RasterStatus::Ready);
    // Four prewarmed opaque/depth/target states, with reference ordering equivalence.
    uint8 oracle[96 * 64 * 4]{};
    for (usize variant = 0; variant < 4; ++variant)
    {
        ludus::qa::L3Items(mesh, materials, items);
        const bool reverse = variant >= 2, offscreen = (variant % 2) != 0;
        if (reverse)
            items[5].Transform.Translation.Z = .1F;
        REQUIRE(renderer.CreateSnapshot(items, 8, snapshot) == rhi::RasterStatus::Ready);
        rr::PreparedView view;
        rr::ViewDescription description;
        description.Reference = offscreen;
        description.Depth = reverse ? rr::DepthConvention::ReverseZ : rr::DepthConvention::Conventional;
        REQUIRE(renderer.PrepareView(snapshot, description, view, report) == rhi::RasterStatus::Ready);
        rhi::TextureHandle target;
        rr::Presentation presentation;
        rhi::RasterPassDescription pass;
        if (offscreen)
        {
            REQUIRE(rhi::CreateTexture(device, {96, 64, rhi::RasterFormat::Rgba8Unorm, true}, {}, target) ==
                    rhi::RasterStatus::Ready);
            rr::ViewMapping mapping;
            REQUIRE(rr::ResolveViewMapping({96, 64, rr::ScreenFit::Stretch}, {0, 0, 96, 64}, 96, 64, mapping) ==
                    rhi::RasterStatus::Ready);
            const auto prepared = renderer.PreparePresentation(target, mapping, presentation);
            REQUIRE((prepared == rhi::RasterStatus::Ready || prepared == rhi::RasterStatus::Pending));
            wait(presentation);
            pass.Color = target;
        }
        REQUIRE(rhi::BeginFrame(device, surface) == rhi::DeviceStatus::Ready);
        REQUIRE(renderer.DrawView(view, pass) == rhi::RasterStatus::Ready);
        if (offscreen)
            REQUIRE(renderer.DrawPresentation(presentation) == rhi::RasterStatus::Ready);
        completion = {};
        REQUIRE(rhi::EndFrame(device, completion) == rhi::RasterStatus::Ready);
        uint8 image[96 * 64 * 4];
        REQUIRE(rhi::backend::ReadHeadlessPixels(image));
        if (variant == 0)
            std::memcpy(oracle, image, sizeof(image));
        else
            CHECK(std::memcmp(oracle, image, sizeof(image)) == 0);
        const auto color = [&](usize x, usize y, uint8 red, uint8 green, uint8 blue) {
            const auto offset = (y * 96 + x) * 4;
            CHECK(image[offset] == red);
            CHECK(image[offset + 1] == green);
            CHECK(image[offset + 2] == blue);
        };
        const auto mip = (usize{8} * 96 + 16) * 4;
        CHECK(image[mip] >= 127);
        CHECK(image[mip] <= 129);
        CHECK(image[mip + 1] == image[mip]);
        CHECK(image[mip + 2] == image[mip]);
        color(40, 8, 255, 255, 255);
        color(56, 8, 0, 0, 0);
        color(16, 48, 0, 255, 0);
        color(72, 8, 0, 0, 255);
        color(88, 8, 0, 0, 0);
        color(80, 48, 255, 0, 255);
        const auto blend = (usize{48} * 96 + 48) * 4;
        CHECK(image[blend] == 0);
        CHECK(image[blend + 1] >= 127);
        CHECK(image[blend + 1] <= 128);
        CHECK(image[blend + 2] == 128);
        REQUIRE(renderer.Release(view) == rhi::RasterStatus::Ready);
        REQUIRE(renderer.Release(snapshot) == rhi::RasterStatus::Ready);
        if (offscreen)
        {
            REQUIRE(renderer.Release(presentation) == rhi::RasterStatus::Ready);
            REQUIRE(rhi::Destroy(device, target) == rhi::RasterStatus::Ready);
        }
    }
}
