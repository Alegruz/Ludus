#include "l6_scene.h"
#include "renderer_composite.h"
#include "renderer_flat.h"
#include "renderer_gpu_preview.h"
#include "renderer_gpu_preview_image.h"
#include "renderer_lit.h"
#include "renderer_lit_shadow.h"
#include "renderer_output.h"
#include "renderer_shadow.h"

#include <ludus/foundation/math/scalar.hpp>
#include <ludus/foundation/time/time.hpp>
#include <ludus/graphics/renderer/renderer.hpp>
#include <ludus/graphics/rhi/device.h>
#include <ludus/graphics/rhi/lifetime.h>

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstring>

namespace
{
using namespace ludus::foundation;
namespace rr = ludus::graphics::renderer;
namespace rhi = ludus::graphics::rhi;
namespace shaders = ludus::shaders;
using S = rhi::RasterStatus;
void WaitPreview(rr::Renderer& renderer, rr::GpuPreview preview, uint32 samples)
{
    rr::GpuPreviewInfo info;
    const auto start = time::NowTicks();
    S status = S::Pending;
    do
    {
        status = renderer.GetGpuPreviewInfo(preview, info);
        REQUIRE((status == S::Ready || status == S::Pending));
    } while (info.CompletedSamples < samples && time::NowTicks() - start < 10 * time::NANOSECONDS_PER_SECOND);
    REQUIRE(info.CompletedSamples == samples);
}
void ReadImage(rhi::DeviceHandle device, rr::GpuPreviewImage image, uint8* output, usize size)
{
    // Acceptance only: production preview never calls either readback service.
    rhi::ReadbackTicket ticket;
    auto status = rhi::RequestTextureReadback(device,
                                              image.Texture,
                                              {0, 0, 0, image.Width, image.Height},
                                              usize(image.Width) * 4,
                                              ticket);
    const auto start = time::NowTicks();
    while (status == S::Pending && time::NowTicks() - start < 10 * time::NANOSECONDS_PER_SECOND)
    {
        status = rhi::GetStatus(device, ticket);
    }
    REQUIRE(status == S::Ready);
    REQUIRE(rhi::CopyReadback(device, ticket, output, size) == S::Ready);
    REQUIRE(rhi::Release(device, ticket) == S::Ready);
}
uint8 Display(float32 value)
{
    value /= 1 + value;
    const auto encoded = value <= .0031308F ? value * 12.92F : 1.055F * std::pow(value, 1.0F / 2.4F) - .055F;
    return static_cast<uint8>(std::lround(encoded * 255));
}
} // namespace
TEST_CASE("GPU preview transports emissive RGB on GPU and preserves leased output", "[renderer][gpu][gpu-preview]")
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
    rhi::DeviceDescription description;
    description.Required.PortableRaster = true;
    description.Required.Compute = true;
    const auto started = rhi::CreateDevice({}, { .Width = 8, .Height = 8 }, description, device, surface);
#if defined(LUDUS_TEST_METAL)
    if (started != rhi::DeviceStatus::Ready && rhi::GetStartup().Error == rhi::StartupError::AdapterUnavailable)
    {
        SKIP("No Metal adapter");
    }
#endif
    REQUIRE(started == rhi::DeviceStatus::Ready);
    rr::Renderer renderer;
    REQUIRE(renderer.Initialize(device, shaders::renderer_flat::Vertex(), shaders::renderer_flat::Fragment()) ==
            S::Ready);
    REQUIRE(renderer.EnableGpuPreview(shaders::renderer_gpu_preview::Compute(),
                                      shaders::renderer_gpu_preview_image::Vertex(),
                                      shaders::renderer_gpu_preview_image::Fragment(),
                                      {usize{32} * 1024 * 1024, usize{32} * 1024 * 1024, 1}) == S::Ready);
    CHECK(renderer.DisableReferenceRendering() == S::InvalidState);
    REQUIRE(renderer.InitializeViews(shaders::renderer_composite::Vertex(), shaders::renderer_composite::Fragment()) ==
            S::Ready);
    REQUIRE(renderer.InitializeLighting(shaders::renderer_output::Vertex(),
                                        shaders::renderer_output::Fragment(),
                                        shaders::renderer_shadow::Fragment()) == S::Ready);
    rr::MaterialProgram program;
    auto status = renderer.PrewarmLitProgram(shaders::renderer_lit::Vertex(),
                                             shaders::renderer_lit::Fragment(),
                                             shaders::renderer_lit_shadow::Fragment(),
                                             program);
    const auto setupStart = time::NowTicks();
    while (status == S::Pending && time::NowTicks() - setupStart < 10 * time::NANOSECONDS_PER_SECOND)
    {
        status = renderer.GetStatus(program);
    }
    REQUIRE(status == S::Ready);
    rr::Snapshot snapshot;
    REQUIRE(ludus::qa::BuildL6Snapshot(renderer, program, snapshot, -.7F) == S::Ready);
    rr::ReferenceCaptureDescription capture;
    capture.OrthographicCamera = true;
    capture.WorldToClip.Columns[3].Z = .7F;
    capture.CameraPosition.Z -= .7F;
    capture.Width = capture.Height = 2;
    capture.TargetSamples = 128;
    capture.MaxDepth = 4;
    capture.Seed = 871;
    // Authored constant environment is permitted; the renderer invents no lights.
    capture.Environment = {.3F, .4F, .5F};
    rr::ReferenceCapture oracle;
    REQUIRE(renderer.BeginReferenceCapture(snapshot, capture, oracle) == S::Ready);
    status = S::Pending;
    const auto oracleStart = time::NowTicks();
    while (status == S::Pending && time::NowTicks() - oracleStart < 10 * time::NANOSECONDS_PER_SECOND)
    {
        status = renderer.AdvanceReferenceCapture(oracle);
    }
    REQUIRE(status == S::Ready);
    rr::ReferencePixel reference[4];
    REQUIRE(renderer.CopyReferencePixels(oracle, reference, 4) == S::Ready);
    float32 indirect = 0;
    for (const auto& pixel : reference)
    {
        indirect += pixel.Indirect.X;
    }
    REQUIRE(indirect > .01F);
    REQUIRE(renderer.Release(oracle) == S::Ready);
    rr::GpuPreview preview;
    auto rejected = capture;
    rr::DirectLight light;
    rejected.Lights = &light;
    rejected.LightCount = 1;
    CHECK(renderer.BeginGpuPreview(snapshot, rejected, preview) == S::Unsupported);
    rejected = capture;
    rejected.Width = rejected.Height = 4096;
    CHECK(renderer.BeginGpuPreview(snapshot, rejected, preview) == S::CapacityExceeded);
    REQUIRE(renderer.BeginGpuPreview(snapshot, capture, preview) == S::Ready);
    REQUIRE(renderer.Release(snapshot) == S::Ready);
    REQUIRE(renderer.Release(program) == S::Ready);
    rr::GpuPreviewFrame frames[2];
    rr::GpuPreviewImage images[2];
    uint8 early[16], retained[16], final[16];
    for (uint32 sample = 1; sample <= 2; ++sample)
    {
        rhi::SubmissionToken token;
        REQUIRE(renderer.AdvanceGpuPreview(preview, surface, token) == S::Ready);
        WaitPreview(renderer, preview, sample);
        REQUIRE(renderer.AcquireGpuPreviewFrame(preview, frames[sample - 1], images[sample - 1]) == S::Ready);
    }
    ReadImage(device, images[0], early, sizeof(early));
    rhi::SubmissionToken blocked;
    CHECK(renderer.AdvanceGpuPreview(preview, surface, blocked) == S::NotReady);
    auto staleFrame = frames[1];
    REQUIRE(renderer.Release(frames[1]) == S::Ready);
    CHECK(renderer.Release(staleFrame) == S::InvalidHandle);
    for (uint32 sample = 3; sample <= capture.TargetSamples; ++sample)
    {
        rhi::SubmissionToken token;
        REQUIRE(renderer.AdvanceGpuPreview(preview, surface, token) == S::Ready);
        WaitPreview(renderer, preview, sample);
    }
    REQUIRE(renderer.AcquireGpuPreviewFrame(preview, frames[1], images[1]) == S::Ready);
    ReadImage(device, images[0], retained, sizeof(retained));
    CHECK(std::memcmp(early, retained, sizeof(early)) == 0);
    ReadImage(device, images[1], final, sizeof(final));
    for (usize pixel = 0; pixel < 4; ++pixel)
    {
        const float32 expected[]{reference[pixel].Radiance.X, reference[pixel].Radiance.Y, reference[pixel].Radiance.Z};
        for (usize channel = 0; channel < 3; ++channel)
        {
            CAPTURE(pixel, channel, final[pixel * 4 + channel], Display(expected[channel]));
            CHECK(math::Abs(float32(final[pixel * 4 + channel]) - float32(Display(expected[channel]))) <= 2);
        }
        CHECK(final[pixel * 4 + 3] == 255);
        CHECK_FALSE((final[pixel * 4] == 255 && final[pixel * 4 + 1] == 0 && final[pixel * 4 + 2] == 255));
    }
    auto staleJob = preview;
    REQUIRE(renderer.Release(preview) == S::Ready);
    rr::GpuPreviewInfo info;
    CHECK(renderer.GetGpuPreviewInfo(staleJob, info) == S::InvalidHandle);
    // Parent cancellation retains acquired texture until the consumer releases it.
    ReadImage(device, images[1], retained, sizeof(retained));
    CHECK(std::memcmp(final, retained, sizeof(final)) == 0);
    REQUIRE(renderer.Release(frames[0]) == S::Ready);
    REQUIRE(renderer.Release(frames[1]) == S::Ready);
    CHECK(rhi::GetStatus(device, images[1].Texture) == S::InvalidHandle);
}
