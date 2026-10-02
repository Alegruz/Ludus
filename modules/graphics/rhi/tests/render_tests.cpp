#include <ludus/foundation/base/types.h>
#include <ludus/foundation/logging/log_system.hpp>
#include <ludus/graphics/rhi/render.h>
#include <ludus/graphics/rhi/rhi.h>

#include "diagnostic.h"
#include "internal/vulkan_readback.h"

#include <cmath>
#include <span>

#include <catch2/catch_test_macros.hpp>

using namespace ludus::foundation;
namespace rhi = ludus::graphics::rhi;
TEST_CASE("Public Vulkan resources render changing uniforms across resize and restart", "[rhi][gpu]")
{
    struct Guard final
    {
        ~Guard() noexcept
        {
            rhi::Shutdown();
            ludus::foundation::logging::LogSystem::Shutdown();
        }
    } guard;
    ludus::foundation::logging::LogConfig log;
    log.EnableConsole = true;
    log.EnableFile = false;
    log.EnableDebugger = false;
    (void)ludus::foundation::logging::LogSystem::Initialize(log);

    for (usize session = 0; session < 2; ++session)
    {
        rhi::Shutdown();
        REQUIRE(rhi::Start({}, { .Width = 96, .Height = 64 }) == rhi::StartStatus::Ready);
        rhi::ShaderHandle vertex, fragment;
        rhi::UniformHandle uniform;
        rhi::PipelineHandle pipeline;
        auto invalid = ludus::shaders::diagnostic::Vertex();
        invalid.SpirvEntry = "missing";
        rhi::ShaderHandle failed;
        REQUIRE(rhi::CreateShader(invalid, failed) == rhi::ResourceStatus::Failed);
        REQUIRE(rhi::Destroy(failed) == rhi::ResourceStatus::Ready);
        REQUIRE(rhi::CreateShader(ludus::shaders::diagnostic::Vertex(), vertex) == rhi::ResourceStatus::Ready);
        REQUIRE(rhi::CreateShader(ludus::shaders::diagnostic::Fragment(), fragment) == rhi::ResourceStatus::Ready);
        REQUIRE(rhi::CreateUniform(48, uniform) == rhi::ResourceStatus::Ready);
        REQUIRE(rhi::CreatePipeline({vertex, fragment, uniform}, pipeline) == rhi::ResourceStatus::Ready);
        uint8 pixels[96 * 64 * 4]{};
        for (usize frame = 0; frame < 40; ++frame)
        {
            const uint32 width = frame < 4 ? 96 : 64, height = frame < 4 ? 64 : 96;
            const float32 elapsed = frame % 2 == 0 ? 0.0F : 2.0F;
            // Independently padded scalar payload; neither backend handle enters this test.
            const float32 values[12] = {static_cast<float32>(width),
                                        static_cast<float32>(height),
                                        elapsed,
                                        0,
                                        0.2F,
                                        0.4F,
                                        0.6F,
                                        0,
                                        0.6F,
                                        0.4F,
                                        0.2F,
                                        1};
            REQUIRE(rhi::UpdateUniform(uniform, {reinterpret_cast<const uint8*>(values), sizeof(values)}) ==
                    rhi::ResourceStatus::Ready);
            REQUIRE(rhi::SetFrameTarget({ .Width = width, .Height = height }) == rhi::FrameStatus::Ready);
            REQUIRE(rhi::BeginFrameStatus() == rhi::FrameStatus::Ready);
            REQUIRE(rhi::DrawFullscreen(pipeline) == rhi::ResourceStatus::Ready);
            REQUIRE(rhi::EndFrameStatus() == rhi::FrameStatus::Ready);
            // Burst frames without CPU/GPU idle readback exercise both in-flight
            // slots; inspect the final submitted image after the burst.
            if (frame >= 8 && frame != 39)
            {
                continue;
            }
            REQUIRE(rhi::backend::ReadHeadlessPixels(pixels));
            usize errors = 0;
            for (usize y = 0; y < height; ++y)
            {
                for (usize x = 0; x < width; ++x)
                {
                    const float64 cx = (static_cast<float64>(x) + 0.5 - width * 0.5) / height;
                    const float64 cy = (static_cast<float64>(y) + 0.5 - height * 0.5) / height;
                    const float64 expected[4] = {
                        0.6 * (static_cast<float64>(x) + 0.5) / width + 0.01 * static_cast<float64>(elapsed),
                        0.4 * (static_cast<float64>(y) + 0.5) / height + 0.02 * static_cast<float64>(elapsed),
                        0.2 * (cx * cx + cy * cy < 0.04 ? 1 : 0) + 0.03 * static_cast<float64>(elapsed),
                        1};
                    for (usize channel = 0; channel < 4; ++channel)
                    {
                        const int32 actual = pixels[(y * width + x) * 4 + channel];
                        if (std::abs(actual - static_cast<int32>(std::round(expected[channel] * 255))) > 2)
                        {
                            ++errors;
                        }
                    }
                }
            }
            INFO("session=" << session << " frame=" << frame);
            REQUIRE(errors == 0);
        }
        REQUIRE(rhi::Destroy(pipeline) == rhi::ResourceStatus::Ready);
        REQUIRE(rhi::Destroy(uniform) == rhi::ResourceStatus::Ready);
        REQUIRE(rhi::GetStatus(uniform) == rhi::ResourceStatus::InvalidHandle);
        rhi::UniformHandle replacement;
        pipeline = {};
        REQUIRE(rhi::CreateUniform(48, replacement) == rhi::ResourceStatus::Ready);
        REQUIRE(rhi::CreatePipeline({vertex, fragment, replacement}, pipeline) == rhi::ResourceStatus::Ready);
        const uint8 zeros[48]{};
        REQUIRE(rhi::UpdateUniform(replacement, zeros) == rhi::ResourceStatus::Ready);
        REQUIRE(rhi::SetFrameTarget({}) == rhi::FrameStatus::Ready);
        REQUIRE(rhi::BeginFrameStatus() == rhi::FrameStatus::Skipped);
        REQUIRE(rhi::GetFrameInfo().Width == 0);
        REQUIRE(rhi::SetFrameTarget({ .Width = 64, .Height = 96 }) == rhi::FrameStatus::Ready);
        REQUIRE(rhi::BeginFrameStatus() == rhi::FrameStatus::Ready);
        REQUIRE(rhi::DrawFullscreen(pipeline) == rhi::ResourceStatus::Ready);
        // Abort a recorded draw before submission; restart must not retain it.
        rhi::Shutdown();
        REQUIRE(rhi::GetStatus(pipeline) == rhi::ResourceStatus::InvalidHandle);
        REQUIRE(rhi::GetStatus(replacement) == rhi::ResourceStatus::InvalidHandle);
    }
}
