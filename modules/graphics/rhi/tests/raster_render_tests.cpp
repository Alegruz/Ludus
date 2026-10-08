#include <chrono>
#include <cstring>
#include <ludus/foundation/base/types.h>
#include <ludus/graphics/rhi/lifetime.h>
#include <thread>

#include "internal/readback.h"
#include "raster.h"

#include <span>

#include <catch2/catch_test_macros.hpp>
using namespace ludus::foundation;
namespace rhi = ludus::graphics::rhi;
namespace
{
template <typename T, usize N>
std::span<const uint8> RasterBytes(const T (&data)[N]) noexcept
{
    return {reinterpret_cast<const uint8*>(data), sizeof(data)};
}
} // namespace
TEST_CASE("Portable indexed instances preserve texture origin, depth and detached binding snapshots",
          "[rhi][gpu][raster]")
{
    int32 mode = 0;
    SECTION("Linear RGBA8 sampling and depth") {}
    SECTION("sRGB sampling decodes to linear output")
    {
        mode = 1;
    }
    SECTION("Premultiplied alpha blends over the frame clear")
    {
        mode = 2;
    }
    struct Guard final
    {
        ~Guard() noexcept
        {
            rhi::Shutdown();
        }
    } guard;
    rhi::Shutdown();
    rhi::DeviceHandle device;
    rhi::SurfaceHandle surface;
    rhi::StartupInfo failure;
    rhi::DeviceDescription description;
    description.Required.PortableRaster = true;
    const auto started = rhi::CreateDevice({}, { .Width = 96, .Height = 64 }, description, device, surface, &failure);
#if defined(LUDUS_TEST_METAL)
    if (started != rhi::DeviceStatus::Ready && failure.Error == rhi::StartupError::AdapterUnavailable)
    {
        REQUIRE(rhi::GetStartup().State == rhi::StartupState::Idle);
        SKIP("No Metal adapter on this host");
    }
#endif
    REQUIRE(started == rhi::DeviceStatus::Ready);
    const auto vertex = ludus::shaders::raster::Vertex();
    const auto fragment = ludus::shaders::raster::Fragment();
    rhi::RasterShaderHandle vs, fs;
    REQUIRE(rhi::CreateRasterShader(device, vertex, vs) == rhi::RasterStatus::Ready);
    REQUIRE(rhi::CreateRasterShader(device, fragment, fs) == rhi::RasterStatus::Ready);
    rhi::BindingLayoutHandle layout;
    REQUIRE(rhi::CreateBindingLayout(device, fragment.Bindings, layout) == rhi::RasterStatus::Ready);
    const float32 tint[4]{1, 1, 1, 1};
    const float32 dim[4]{0.5F, 0.5F, 0.5F, 1};
    rhi::BufferHandle uniform, backgroundUniform;
    REQUIRE(rhi::CreateBuffer(device, {rhi::BufferRole::Uniform, sizeof(tint)}, RasterBytes(tint), uniform) ==
            rhi::RasterStatus::Ready);
    REQUIRE(rhi::CreateBuffer(device, {rhi::BufferRole::Uniform, sizeof(dim)}, RasterBytes(dim), backgroundUniform) ==
            rhi::RasterStatus::Ready);
    uint8 image[16]{255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
    if (mode != 0)
    {
        for (usize i = 0; i < sizeof(image); ++i)
        {
            if (i % 4 == 3)
            {
                image[i] = mode == 2 ? 128 : 255;
            }
            else if (image[i] != 0)
            {
                image[i] = 128;
            }
        }
    }
    rhi::TextureHandle texture;
    rhi::TextureViewHandle view;
    rhi::SamplerHandle sampler;
    REQUIRE(rhi::CreateTexture(device,
                               {2, 2, mode == 1 ? rhi::RasterFormat::Rgba8Srgb : rhi::RasterFormat::Rgba8Unorm},
                               {image, 8},
                               texture) == rhi::RasterStatus::Ready);
    REQUIRE(rhi::CreateTextureView(device, texture, view) == rhi::RasterStatus::Ready);
    REQUIRE(rhi::CreateSampler(device, {}, sampler) == rhi::RasterStatus::Ready);
    rhi::RasterBindingResource resources[3];
    resources[0].Binding = 0;
    resources[0].Buffer = uniform;
    resources[0].Size = 16;
    resources[1].Binding = 1;
    resources[1].Texture = view;
    resources[2].Binding = 2;
    resources[2].Sampler = sampler;
    rhi::BindingSetHandle set, backgroundSet;
    REQUIRE(rhi::CreateBindingSet(device, layout, resources, set) == rhi::RasterStatus::Ready);
    resources[0].Buffer = backgroundUniform;
    REQUIRE(rhi::CreateBindingSet(device, layout, resources, backgroundSet) == rhi::RasterStatus::Ready);
    // Public destruction detaches ownership while snapshots still retain records.
    REQUIRE(rhi::Destroy(device, texture) == rhi::RasterStatus::Ready);
    REQUIRE(rhi::Destroy(device, view) == rhi::RasterStatus::Ready);
    REQUIRE(rhi::Destroy(device, sampler) == rhi::RasterStatus::Ready);
    REQUIRE(rhi::Destroy(device, uniform) == rhi::RasterStatus::Ready);
    REQUIRE(rhi::Destroy(device, backgroundUniform) == rhi::RasterStatus::Ready);
    const rhi::RasterVertexStream streams[2]{{20, false}, {8, true}};
    const rhi::RasterVertexAttribute attributes[3]{{0, 0, 0, rhi::RasterVertexFormat::Float3},
                                                   {1, 0, 12, rhi::RasterVertexFormat::Float2},
                                                   {2, 1, 0, rhi::RasterVertexFormat::Float2}};
    rhi::PipelineRequest request;
    REQUIRE(rhi::RequestPipeline(device, {vs, fs, layout, streams, attributes, true, mode == 2}, request) ==
            rhi::RasterStatus::Pending);
    REQUIRE(rhi::PollLifetime(device) == rhi::RasterStatus::Ready);
    rhi::RasterPipelineHandle pipeline;
    REQUIRE(rhi::GetRequestedPipeline(device, request, pipeline) == rhi::RasterStatus::Ready);
    const float32 points[20]{-.4F, -.75F, .25F, 0, 1, -.4F, .75F,  .25F, 0, 0,
                             .4F,  .75F,  .25F, 1, 0, .4F,  -.75F, .25F, 1, 1};
    const float32 back[20]{-.95F, -.95F, .75F, 0, 1, -.95F, .95F,  .75F, 0, 0,
                           .95F,  .95F,  .75F, 1, 0, .95F,  -.95F, .75F, 1, 1};
    const float32 offsets[4]{-.45F, 0, .45F, 0};
    const float32 center[2]{0, 0};
    const uint16 indices[6]{0, 1, 2, 0, 2, 3};
    rhi::BufferHandle vertices, background, instances, centerInstance, index;
    REQUIRE(rhi::CreateBuffer(device, {rhi::BufferRole::Vertex, sizeof(points)}, RasterBytes(points), vertices) ==
            rhi::RasterStatus::Ready);
    REQUIRE(rhi::CreateBuffer(device, {rhi::BufferRole::Vertex, sizeof(back)}, RasterBytes(back), background) ==
            rhi::RasterStatus::Ready);
    REQUIRE(rhi::CreateBuffer(device, {rhi::BufferRole::Vertex, sizeof(offsets)}, RasterBytes(offsets), instances) ==
            rhi::RasterStatus::Ready);
    REQUIRE(rhi::CreateBuffer(device, {rhi::BufferRole::Vertex, sizeof(center)}, RasterBytes(center), centerInstance) ==
            rhi::RasterStatus::Ready);
    REQUIRE(rhi::CreateBuffer(device, {rhi::BufferRole::Index16, sizeof(indices)}, RasterBytes(indices), index) ==
            rhi::RasterStatus::Ready);
    rhi::RasterVertexSlice slices[2]{{vertices, 0, sizeof(points)}, {instances, 0, sizeof(offsets)}};
    const rhi::RasterDraw draw{pipeline, set, slices, index, 0, 6, 4, 2};
    const float64 clear = mode == 2 ? .25 : 0;
    REQUIRE(rhi::SetFrameTarget(device, surface, {96, 64, clear, clear, clear, 1}) == rhi::DeviceStatus::Ready);
    rhi::CommandBatch batch;
    REQUIRE(rhi::BeginCommands(device, batch) == rhi::RasterStatus::Ready);
    REQUIRE(rhi::RecordDraw(device, batch, draw) == rhi::RasterStatus::Ready);
    const rhi::RasterVertexSlice backgroundSlices[2]{{background, 0, sizeof(back)},
                                                     {centerInstance, 0, sizeof(center)}};
    REQUIRE(rhi::RecordDraw(device, batch, {pipeline, backgroundSet, backgroundSlices, index, 0, 6, 4, 1}) ==
            rhi::RasterStatus::Ready);
    REQUIRE(rhi::Release(device, request) == rhi::RasterStatus::Ready);
    REQUIRE(rhi::Destroy(device, set) == rhi::RasterStatus::Ready);
    REQUIRE(rhi::Destroy(device, backgroundSet) == rhi::RasterStatus::Ready);
    REQUIRE(rhi::Destroy(device, vertices) == rhi::RasterStatus::Ready);
    REQUIRE(rhi::Destroy(device, instances) == rhi::RasterStatus::Ready);
    REQUIRE(rhi::Destroy(device, index) == rhi::RasterStatus::Ready);
    REQUIRE(rhi::FinishCommands(device, batch) == rhi::RasterStatus::Ready);
    rhi::SubmissionToken completion;
    REQUIRE(rhi::SubmitCommands(device, surface, batch, completion) == rhi::RasterStatus::Ready);
    uint8 pixels[96 * 64 * 4]{};
    REQUIRE(rhi::backend::ReadHeadlessPixels(pixels));
    CHECK(rhi::GetStatus(device, completion) == rhi::RasterStatus::Ready);
    const uint8 high = mode == 1 ? 55 : mode == 2 ? 160 : 255;
    const uint8 low = mode == 2 ? 32 : 0;
    const uint32 xs[4]{12, 36, 60, 84};
    for (usize i = 0; i < 4; ++i)
    {
        const usize top = (usize{16} * 96 + xs[i]) * 4;
        const usize bottom = (usize{48} * 96 + xs[i]) * 4;
        const bool left = i % 2 == 0;
        CHECK(pixels[top] == (left ? high : low));
        CHECK(pixels[top + 1] == (left ? low : high));
        CHECK(pixels[top + 2] == low);
        CHECK(pixels[top + 3] == 255);
        CHECK(pixels[bottom] == (left ? low : high));
        CHECK(pixels[bottom + 1] == (left ? low : high));
        CHECK(pixels[bottom + 2] == high);
        CHECK(pixels[bottom + 3] == 255);
    }
}

TEST_CASE("Native upload and readback rings preserve bytes across source destruction", "[rhi][gpu][lifetime]")
{
    struct Guard final
    {
        ~Guard() noexcept
        {
            rhi::Shutdown();
        }
    } guard;
    rhi::Shutdown();
    rhi::DeviceHandle device;
    rhi::SurfaceHandle surface;
    rhi::StartupInfo failure;
    rhi::DeviceDescription description;
    description.Required.PortableRaster = true;
    const auto started = rhi::CreateDevice({}, { .Width = 16, .Height = 16 }, description, device, surface, &failure);
#if defined(LUDUS_TEST_METAL)
    if (started != rhi::DeviceStatus::Ready && failure.Error == rhi::StartupError::AdapterUnavailable)
    {
        SKIP("No Metal adapter on this host");
    }
#endif
    REQUIRE(started == rhi::DeviceStatus::Ready);
    const rhi::BufferRole roles[]{rhi::BufferRole::Uniform,
                                  rhi::BufferRole::Vertex,
                                  rhi::BufferRole::Index16,
                                  rhi::BufferRole::Index32};
    for (const auto role : roles)
    {
        uint8 bytes[16]{1, 2, 3, 4, 5, 6, 7, 8};
        rhi::UploadTicket upload;
        REQUIRE(rhi::RequestBufferUpload(device, {role, sizeof(bytes)}, bytes, sizeof(bytes), upload) ==
                rhi::RasterStatus::Pending);
        bytes[0] = 99;
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (rhi::GetStatus(device, upload) == rhi::RasterStatus::Pending && std::chrono::steady_clock::now() < until)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        rhi::BufferHandle buffer;
        REQUIRE(rhi::TakeUploadedBuffer(device, upload, buffer) == rhi::RasterStatus::Ready);
        rhi::ReadbackTicket readback;
        REQUIRE(rhi::RequestBufferReadback(device, buffer, 0, sizeof(bytes), readback) == rhi::RasterStatus::Pending);
        REQUIRE(rhi::Destroy(device, buffer) == rhi::RasterStatus::Ready);
        while (rhi::GetStatus(device, readback) == rhi::RasterStatus::Pending &&
               std::chrono::steady_clock::now() < until)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        uint8 actual[16]{};
        REQUIRE(rhi::CopyReadback(device, readback, actual, sizeof(actual)) == rhi::RasterStatus::Ready);
        bytes[0] = 1;
        CHECK(std::memcmp(actual, bytes, sizeof(bytes)) == 0);
        REQUIRE(rhi::Release(device, readback) == rhi::RasterStatus::Ready);
    }
}
