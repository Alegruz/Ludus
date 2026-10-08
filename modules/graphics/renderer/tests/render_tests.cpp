#include "internal/readback.h"
#include "l0_scene.h"
#include "renderer_flat.h"
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <ludus/graphics/renderer/renderer.hpp>
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
    const usize overlay = (usize{32} * 96 + 48) * 4;
    CHECK(images[0][overlay] == 70);
    CHECK(images[0][overlay + 1] == 134);
    CHECK(images[0][overlay + 2] == 13);
    const usize error = (usize{45} * 96 + 48) * 4;
    CHECK(images[0][error] == 255);
    CHECK(images[0][error + 2] == 255);
}
