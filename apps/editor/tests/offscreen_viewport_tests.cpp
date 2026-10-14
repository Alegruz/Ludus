#include "internal/offscreen_frame_reader.h"
#include "internal/preview_viewport.h"

#include <ludus/foundation/time/time.hpp>

#include <ludus/graphics/rhi/rhi.h>

#include <QByteArray>
#include <QtGlobal>

#include <catch2/catch_test_macros.hpp>

using namespace ludus::editor;
using namespace ludus::foundation;
namespace rhi = ludus::graphics::rhi;

namespace
{
/// Width wider than one staging row alignment so bands carry row padding.
constexpr uint32 FRAME_WIDTH = 300;
/// Height spanning several 256 KiB transfer slices at FRAME_WIDTH.
constexpr uint32 FRAME_HEIGHT = 700;
constexpr usize BYTES_PER_TEXEL = 4;
/// Upper bound for one asynchronous copy on a software rasterizer.
constexpr uint64 TIMEOUT_SECONDS = 20;
uint8 GetPatternByte(uint32 x, uint32 y, uint32 channel)
{
    return static_cast<uint8>((x * 7U + y * 13U + channel * 61U) & 0xFFU);
}
} // namespace

TEST_CASE("Offscreen presentation is forced by the environment on Linux", "[editor][viewport][offscreen]")
{
#if defined(LUDUS_EDITOR_NATIVE_PREVIEW) && defined(Q_OS_LINUX)
    const QByteArray previous = qgetenv("LUDUS_EDITOR_VIEWPORT");
    qputenv("LUDUS_EDITOR_VIEWPORT", QByteArrayLiteral("offscreen"));
    CHECK(PreviewViewport::GetPresentation() == ViewportPresentation::OffscreenReadback);
    CHECK(PreviewViewport::CanPresent() == true);
    if (previous.isEmpty() == true)
    {
        qunsetenv("LUDUS_EDITOR_VIEWPORT");
    }
    else
    {
        qputenv("LUDUS_EDITOR_VIEWPORT", previous);
    }
#else
    SKIP("Offscreen readback presentation is Linux-only");
#endif
}

TEST_CASE("Offscreen frame reader copies a texture in bands without waiting", "[editor][viewport][offscreen][gpu]")
{
    rhi::Shutdown();
    struct Guard final
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
    const rhi::DeviceStatus started = rhi::CreateDevice({}, { .Width = 8, .Height = 8 }, description, device, surface);
    if (started != rhi::DeviceStatus::Ready)
    {
        SKIP("No Vulkan device (install lavapipe for this smoke test)");
    }
    QByteArray pixels(static_cast<qsizetype>(usize{FRAME_WIDTH} * FRAME_HEIGHT * BYTES_PER_TEXEL), '\0');
    for (uint32 y = 0; y < FRAME_HEIGHT; ++y)
    {
        for (uint32 x = 0; x < FRAME_WIDTH; ++x)
        {
            for (uint32 channel = 0; channel < BYTES_PER_TEXEL; ++channel)
            {
                const usize index = (usize{y} * FRAME_WIDTH + x) * BYTES_PER_TEXEL + channel;
                pixels[static_cast<qsizetype>(index)] = static_cast<char>(GetPatternByte(x, y, channel));
            }
        }
    }
    rhi::TextureHandle texture;
    const rhi::RasterStatus created = rhi::CreateTexture(
        device,
        { .Width = FRAME_WIDTH, .Height = FRAME_HEIGHT, .Format = rhi::RasterFormat::Rgba8Unorm },
        {
            .Bytes = {reinterpret_cast<const uint8*>(pixels.constData()), static_cast<usize>(pixels.size())},
            .RowPitch = usize{FRAME_WIDTH} * BYTES_PER_TEXEL,
        },
        texture);
    REQUIRE((created == rhi::RasterStatus::Ready || created == rhi::RasterStatus::Pending));
    const uint64 start = time::NowTicks();
    while (rhi::GetStatus(device, texture) == rhi::RasterStatus::Pending &&
           time::NowTicks() - start < TIMEOUT_SECONDS * time::NANOSECONDS_PER_SECOND)
    {
        static_cast<void>(rhi::PollLifetime(device));
    }
    REQUIRE(rhi::GetStatus(device, texture) == rhi::RasterStatus::Ready);

    OffscreenFrameReader reader;
    CHECK(reader.Poll() == OffscreenReadStatus::Idle);
    REQUIRE(reader.Begin(device, texture, FRAME_WIDTH, FRAME_HEIGHT) == true);
    CHECK(reader.Begin(device, texture, FRAME_WIDTH, FRAME_HEIGHT) == false);
    OffscreenReadStatus status = OffscreenReadStatus::Pending;
    uint32 polls = 0;
    while (status == OffscreenReadStatus::Pending &&
           time::NowTicks() - start < TIMEOUT_SECONDS * time::NANOSECONDS_PER_SECOND)
    {
        static_cast<void>(rhi::PollLifetime(device));
        status = reader.Poll();
        ++polls;
    }
    REQUIRE(status == OffscreenReadStatus::Complete);
    CHECK(polls > 1); // More than one band: the copy never completes inside Begin.
    const QImage image = reader.TakeImage();
    CHECK(reader.IsBusy() == false);
    REQUIRE(image.width() == static_cast<int32>(FRAME_WIDTH));
    REQUIRE(image.height() == static_cast<int32>(FRAME_HEIGHT));
    bool isEqual = true;
    for (uint32 y = 0; y < FRAME_HEIGHT && isEqual == true; ++y)
    {
        const uint8* row = image.constScanLine(static_cast<int32>(y));
        for (uint32 x = 0; x < FRAME_WIDTH && isEqual == true; ++x)
        {
            for (uint32 channel = 0; channel < BYTES_PER_TEXEL; ++channel)
            {
                isEqual = isEqual && row[usize{x} * BYTES_PER_TEXEL + channel] == GetPatternByte(x, y, channel);
            }
        }
    }
    CHECK(isEqual == true);

    REQUIRE(reader.Begin(device, texture, FRAME_WIDTH, FRAME_HEIGHT) == true);
    reader.Cancel();
    CHECK(reader.IsBusy() == false);
    {
        rhi::TextureHandle released = texture;
        CHECK(rhi::Destroy(device, released) == rhi::RasterStatus::Ready);
    }
    CHECK(rhi::DestroyDevice(device) == rhi::DeviceStatus::Ready);
}
