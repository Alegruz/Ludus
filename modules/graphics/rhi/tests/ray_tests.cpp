#include <ludus/foundation/base/types.h>

#include <ludus/foundation/logging/log_system.hpp>
#include <ludus/foundation/time/time.hpp>

#include <ludus/graphics/rhi/device.h>
#include <ludus/graphics/rhi/ray.h>

#include "ray_query.h"

#include <catch2/catch_test_macros.hpp>

using namespace ludus::foundation;
namespace rhi = ludus::graphics::rhi;
namespace
{
template <typename T>
rhi::RasterStatus Poll(rhi::DeviceHandle device, T handle) noexcept
{
    const auto started = time::NowTicks();
    while (time::NowTicks() - started < 10 * time::NANOSECONDS_PER_SECOND)
    {
        const auto status = rhi::GetStatus(device, handle);
        if (status != rhi::RasterStatus::Pending)
        {
            return status;
        }
    }
    return rhi::RasterStatus::Pending;
}
} // namespace
TEST_CASE("Native RT0 filters opacity, traces offscreen instances and retains detached resources", "[rhi][gpu][ray]")
{
    rhi::Shutdown();
    logging::LogConfig loggingConfig;
    loggingConfig.Mode = logging::LogMode::Synchronous;
    loggingConfig.EnableFile = loggingConfig.EnableDebugger = false;
    static_cast<void>(logging::LogSystem::Initialize(loggingConfig));
    struct Guard final
    {
        ~Guard() noexcept
        {
            rhi::Shutdown();
            logging::LogSystem::Shutdown();
        }
    } guard;
    rhi::DeviceHandle device;
    rhi::SurfaceHandle surface;
    rhi::DeviceDescription description;
    description.Required.PortableRaster = true;
    description.Preferred.RayQueries = true;
    const auto started = rhi::CreateDevice({}, { .Width = 8, .Height = 8 }, description, device, surface);
#if defined(LUDUS_TEST_METAL)
    if (started != rhi::DeviceStatus::Ready && rhi::GetStartup().Error == rhi::StartupError::AdapterUnavailable)
    {
        SKIP("No Metal adapter");
    }
#endif
    REQUIRE(started == rhi::DeviceStatus::Ready);
    rhi::RayCapabilities capabilities;
    if (rhi::GetRayCapabilities(device, capabilities) == rhi::RasterStatus::Unsupported)
    {
        CHECK_FALSE(capabilities.InlineQueries);
        CHECK(rhi::BeginFrame(device, surface) == rhi::DeviceStatus::Ready);
        CHECK(rhi::EndFrame(device) == rhi::DeviceStatus::Ready);
        SKIP("Native adapter does not implement the optional ray profile; raster remains usable");
    }
    REQUIRE(capabilities.InlineQueries);
    CHECK_FALSE(capabilities.RayPipelines);
    CHECK_FALSE(capabilities.Updates);
    CHECK(sizeof(rhi::RayInput) == 48);
    CHECK(sizeof(rhi::RayResult) == 32);
    rhi::RayQueryProgram program;
    REQUIRE(rhi::CreateRayQueryProgram(device, ludus::shaders::ray_query::Query(), program) ==
            rhi::RasterStatus::Ready);
    const float32 positions[]{-1, -1, 0, 1, -1, 0, 0, 1, 0};
    const float32 uv[]{0, 1, 1, 1, 0.5F, 0};
    const uint32 indices[]{0, 1, 2};
    rhi::RayGeometryDescription mesh{positions, 3, indices, 3, uv, usize{16} * 1024 * 1024};
    rhi::RayGeometryHandle geometry;
    auto undersized = mesh;
    undersized.ByteBudget = 1;
    for (usize attempt = 0; attempt < 128; ++attempt)
    {
        CHECK(rhi::CreateRayGeometry(device, undersized, geometry) == rhi::RasterStatus::CapacityExceeded);
        CHECK(rhi::GetStatus(device, geometry) == rhi::RasterStatus::InvalidHandle);
    }
    rhi::RayResourceStats stats;
    REQUIRE(rhi::GetRayResourceStats(device, stats) == rhi::RasterStatus::Ready);
    CHECK(stats.Geometries == 0);
    CHECK(stats.ResidentBytes == 0);
    REQUIRE(rhi::CreateRayGeometry(device, mesh, geometry) == rhi::RasterStatus::Pending);
    REQUIRE(Poll(device, geometry) == rhi::RasterStatus::Ready);
    rhi::RayAllocation bytes;
    REQUIRE(rhi::GetRayAllocation(device, geometry, bytes) == rhi::RasterStatus::Ready);
    CHECK(bytes.ResidentBytes > sizeof(positions) + sizeof(indices));
    const float32 alpha[]{0, 1, 0, 1};
    rhi::RayInstanceDescription instances[3];
    for (auto& instance : instances)
    {
        instance.Geometry = geometry;
    }
    instances[0].Transform[11] = 0.5F;
    instances[0].AlphaMasked = true;
    instances[0].Alpha = alpha;
    instances[0].Width = instances[0].Height = 2;
    instances[2].Transform[0] = -2;
    instances[2].Transform[1] = .3F;
    instances[2].Transform[5] = .5F;
    instances[2].Transform[3] = 3;
    instances[2].Mask = 2;
    rhi::RaySceneHandle scene;
    REQUIRE(rhi::CreateRayScene(device, {instances, 3, usize{16} * 1024 * 1024}, scene) == rhi::RasterStatus::Pending);
    REQUIRE(Poll(device, scene) == rhi::RasterStatus::Ready);
    const auto detachedGeometry = geometry;
    REQUIRE(rhi::Destroy(device, geometry) == rhi::RasterStatus::Ready);
    CHECK(rhi::GetStatus(device, detachedGeometry) == rhi::RasterStatus::InvalidHandle);
    rhi::RayInput rays[7];
    rays[0].Origin[0] = -0.5F;
    rays[0].Origin[1] = -0.5F;
    rays[0].Origin[2] = 1;
    rays[1] = rays[0];
    rays[1].Origin[0] = 0.5F;
    rays[2].Origin[0] = 3;
    rays[2].Origin[2] = 1;
    rays[2].Mask = 2;
    rays[3] = rays[0];
    rays[3].Origin[0] = 8;
    rays[4] = rays[1];
    rays[4].VisibilityOnly = 1;
    rays[5] = rays[2];
    rays[5].Mask = 1;
    rays[6] = rays[1];
    rays[6].MaxDistance = .25F;
    rhi::RayQueryBatch batch;
    const auto saved = rays[0];
    rays[0].Direction[2] = -2;
    CHECK(rhi::SubmitRayQueries(device, program, scene, rays, 7, batch) == rhi::RasterStatus::InvalidDescription);
    CHECK(rhi::GetStatus(device, batch) == rhi::RasterStatus::InvalidHandle);
    rays[0] = saved;
    REQUIRE(rhi::SubmitRayQueries(device, program, scene, rays, 7, batch) == rhi::RasterStatus::Pending);
    const auto detachedScene = scene;
    const auto detachedProgram = program;
    REQUIRE(rhi::Destroy(device, scene) == rhi::RasterStatus::Ready);
    REQUIRE(rhi::Destroy(device, program) == rhi::RasterStatus::Ready);
    CHECK(rhi::GetStatus(device, detachedScene) == rhi::RasterStatus::InvalidHandle);
    CHECK(rhi::GetStatus(device, detachedProgram) == rhi::RasterStatus::InvalidHandle);
    REQUIRE(Poll(device, batch) == rhi::RasterStatus::Ready);
    rhi::RayResult results[7];
    results[0].Hit = 77;
    CHECK(rhi::ReadRayResults(device, batch, results, 6) == rhi::RasterStatus::InvalidDescription);
    CHECK(results[0].Hit == 77);
    REQUIRE(rhi::ReadRayResults(device, batch, results, 7) == rhi::RasterStatus::Ready);
    CHECK(results[0].Hit == 1);
    CHECK(results[0].Instance == 1);
    CHECK(results[0].Distance == 1);
    CHECK(results[1].Hit == 1);
    CHECK(results[1].Instance == 0);
    CHECK(results[1].Distance == 0.5F);
    CHECK(results[2].Hit == 1);
    CHECK(results[2].Instance == 2);
    CHECK(results[2].Distance == 1);
    CHECK(results[3].Hit == 0);
    CHECK(results[4].Hit == 1);
    CHECK(results[5].Hit == 0);
    CHECK(results[6].Hit == 0);
    const auto stale = batch;
    REQUIRE(rhi::Destroy(device, batch) == rhi::RasterStatus::Ready);
    CHECK(rhi::GetStatus(device, stale) == rhi::RasterStatus::InvalidHandle);
    REQUIRE(rhi::GetRayResourceStats(device, stats) == rhi::RasterStatus::Ready);
    CHECK(stats.Geometries == 0);
    CHECK(stats.Scenes == 0);
    CHECK(stats.Batches == 0);
    CHECK(stats.ResidentBytes == 0);
    CHECK(stats.BuildScratchBytes == 0);
    REQUIRE(rhi::DestroyDevice(device) == rhi::DeviceStatus::Ready);
}
