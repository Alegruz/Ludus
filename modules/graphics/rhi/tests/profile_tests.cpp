#include "internal/lifecycle.h"
#include "reference_raster.h"

#include <ludus/graphics/rhi/device.h>
#include <ludus/graphics/rhi/raster.h>

#include <catch2/catch_test_macros.hpp>

namespace ludus::graphics::rhi::backend
{
extern foundation::uint32 PendingToken;
extern StartupError ImmediateError;
extern Backend ActiveKind;
extern bool SelectionSupported;
} // namespace ludus::graphics::rhi::backend
using namespace ludus::graphics::rhi;
namespace
{
struct ProfileFixture final
{
    DeviceHandle Device;
    SurfaceHandle Surface;
    ProfileFixture()
    {
        Shutdown();
        reference::Reset();
        backend::ImmediateError = StartupError::None;
        backend::ActiveKind = Backend::WebGPU;
        backend::SelectionSupported = true;
    }
    ~ProfileFixture()
    {
        Shutdown();
        reference::Reset();
    }
    DeviceStatus Start(const DeviceDescription& request)
    {
        return CreateDevice({}, {}, request, Device, Surface);
    }
    void Complete()
    {
        internal::Complete(backend::PendingToken,
                           StartupError::None,
                           { .MaxFrameDimension2D = 4096, .MaxUniformBufferSize = 16384 });
    }
};
} // namespace
TEST_CASE("Profile requests are copied and enabled snapshots are invalidated by loss", "[rhi][device][profile]")
{
    ProfileFixture fixture;
    DeviceDescription request;
    request.RequiredProfile.Raster.MinBufferSize = 64U * 1024U * 1024U;
    request.RequiredProfile.Raster.MaxUniformOffsetAlignment = 256;
    request.RequiredProfile.Compute.MinWorkgroupSize[2] = 64;
    request.RequiredProfile.Compute.MinWorkgroupInvocations = 256;
    request.RequiredProfile.Compute.MinDispatch[1] = 65535;
    request.RequiredProfile.FormatCount = 1;
    request.RequiredProfile.Formats[0] =
    {
        .Format = RasterFormat::Rg8Unorm,
        .Uses = { .Sampled = true, .Filterable = true },
        .MinDimension2D = 4096,
        .MinMipLevels = 13,
    };
    REQUIRE(fixture.Start(request) == DeviceStatus::Pending);
    request.RequiredProfile.Raster.MinBufferSize = ~ludus::foundation::usize{0};
    request.RequiredProfile.Formats[0].Uses.Storage = true;
    DeviceInfo info;
    REQUIRE(GetDeviceInfo(fixture.Device, info) == DeviceStatus::Pending);
    CHECK(info.Description.RequiredProfile.Raster.MinBufferSize == 64U * 1024U * 1024U);
    CHECK_FALSE(info.Description.RequiredProfile.Formats[0].Uses.Storage);
    CHECK(info.Profile.Raster.MaxBufferSize == 0);
    FormatCapabilities format;
    format.MaxMipLevels = 777;
    CHECK(GetFormatCapabilities(fixture.Device, RasterFormat::Rg8Unorm, format) == RasterStatus::NotReady);
    CHECK(format.MaxMipLevels == 0);
    fixture.Complete();
    REQUIRE(GetDeviceInfo(fixture.Device, info) == DeviceStatus::Ready);
    CHECK(info.Enabled.Compute);
    CHECK(info.Profile.Interface.MaxBindings == 8);
    REQUIRE(GetFormatCapabilities(fixture.Device, RasterFormat::Rg8Unorm, format) == RasterStatus::Ready);
    CHECK(format.Uses.Filterable);
    CHECK_FALSE(format.Uses.ColorAttachment);
    CHECK_FALSE(format.Uses.Storage);
    CHECK_FALSE(format.Uses.CopySource);
    CHECK_FALSE(format.Uses.CopyDestination);
    CHECK(format.MaxMipLevels == 13);
    CHECK(GetFormatCapabilities({}, RasterFormat::Rgba8Unorm, format) == RasterStatus::InvalidHandle);
    CHECK(format.MaxMipLevels == 13);
    CHECK(GetFormatCapabilities(fixture.Device, static_cast<RasterFormat>(255), format) ==
          RasterStatus::InvalidDescription);
    CHECK(format.MaxMipLevels == 13);
    internal::Fail(backend::PendingToken, StartupError::DeviceLost);
    REQUIRE(GetDeviceInfo(fixture.Device, info) == DeviceStatus::DeviceLost);
    CHECK(info.Profile.Raster.MaxBufferSize == 0);
    CHECK(info.Profile.Formats[0].SampleCounts == 0);
    CHECK(GetFormatCapabilities(fixture.Device, RasterFormat::Rg8Unorm, format) == RasterStatus::DeviceLost);
    CHECK(format.MaxMipLevels == 0);
}

TEST_CASE("Required profile limits and formats fail before resource creation with exact diagnostics", "[rhi][profile]")
{
    ProfileFixture fixture;
    DeviceDescription request;
    ProfileRequirement expected = ProfileRequirement::None;
    SECTION("engine capacity")
    {
        request.RequiredProfile.Raster.MinResourcesPerKind = 33;
        expected = ProfileRequirement::ResourceCount;
    }
    SECTION("binding capacity")
    {
        request.RequiredProfile.Raster.MinBindings = 9;
        expected = ProfileRequirement::BindingCount;
    }
    SECTION("attachment capacity")
    {
        request.RequiredProfile.Raster.MinColorAttachments = 2;
        expected = ProfileRequirement::ColorAttachmentCount;
    }
    SECTION("uniform alignment is an upper bound")
    {
        request.RequiredProfile.Raster.MaxUniformOffsetAlignment = 128;
        expected = ProfileRequirement::UniformAlignment;
    }
    SECTION("storage alignment is an upper bound")
    {
        request.RequiredProfile.Compute.MaxStorageOffsetAlignment = 128;
        expected = ProfileRequirement::StorageAlignment;
    }
    SECTION("workgroup invocation capacity")
    {
        request.RequiredProfile.Compute.MinWorkgroupInvocations = 257;
        expected = ProfileRequirement::WorkgroupInvocations;
    }
    SECTION("dispatch axis")
    {
        request.RequiredProfile.Compute.MinDispatch[2] = 65536;
        expected = ProfileRequirement::DispatchCount;
    }
    SECTION("sampled-only format cannot be an attachment")
    {
        request.RequiredProfile.FormatCount = 1;
        request.RequiredProfile.Formats[0] = { .Format = RasterFormat::R8Unorm, .Uses = { .ColorAttachment = true } };
        expected = ProfileRequirement::ColorAttachment;
    }
    SECTION("driver capabilities do not enable storage textures")
    {
        request.RequiredProfile.FormatCount = 1;
        request.RequiredProfile.Formats[0].Uses.Storage = true;
        expected = ProfileRequirement::Storage;
    }
    SECTION("creation uploads do not enable general copies")
    {
        request.RequiredProfile.FormatCount = 1;
        request.RequiredProfile.Formats[0].Uses.CopyDestination = true;
        expected = ProfileRequirement::CopyDestination;
    }
    SECTION("unsupported sample count")
    {
        request.RequiredProfile.FormatCount = 1;
        request.RequiredProfile.Formats[0] = { .Uses = { .ColorAttachment = true }, .SampleCount = 4 };
        expected = ProfileRequirement::SampleCount;
    }
    SECTION("same texture cannot have a mip chain and attachment use")
    {
        request.RequiredProfile.FormatCount = 1;
        request.RequiredProfile.Formats[0] =
        {
            .Uses =
            {
                .Sampled = true,
                .ColorAttachment = true,
            },
            .MinMipLevels = 2,
        };
        expected = ProfileRequirement::MipCount;
    }
    REQUIRE(fixture.Start(request) == DeviceStatus::Pending);
    fixture.Complete();
    DeviceInfo info;
    REQUIRE(GetDeviceInfo(fixture.Device, info) == DeviceStatus::Failed);
    CHECK(info.Startup.Error == StartupError::RequirementsUnsatisfied);
    CHECK(info.Startup.UnmetRequirement == RequirementFailure::Profile);
    CHECK(info.Startup.ProfileFailure.Requirement == expected);
    if (expected == ProfileRequirement::DispatchCount)
    {
        CHECK(info.Startup.ProfileFailure.Axis == 2);
        CHECK(info.Startup.ProfileFailure.Requested == 65536);
        CHECK(info.Startup.ProfileFailure.Available == 65535);
    }
    CHECK_FALSE(info.Enabled.PortableRaster);
    for (const auto created : reference::Creates)
    {
        CHECK(created == 0);
    }
    BufferHandle buffer;
    const ludus::foundation::uint8 bytes[4]{};
    CHECK(CreateBuffer(fixture.Device, { .Size = 4 }, bytes, buffer) == RasterStatus::Failed);
    REQUIRE(DestroyDevice(fixture.Device) == DeviceStatus::Ready);
}

TEST_CASE("Malformed profile requests never launch a backend and preserve outputs", "[rhi][profile]")
{
    ProfileFixture fixture;
    DeviceDescription request;
    request.RequiredProfile.FormatCount = 1;
    request.RequiredProfile.Formats[0].Uses.Sampled = true;
    SECTION("too many formats")
    {
        request.RequiredProfile.FormatCount = 5;
    }
    SECTION("invalid format")
    {
        request.RequiredProfile.Formats[0].Format = static_cast<RasterFormat>(255);
    }
    SECTION("no use")
    {
        request.RequiredProfile.Formats[0].Uses = {};
    }
    SECTION("duplicate format")
    {
        request.RequiredProfile.FormatCount = 2;
        request.RequiredProfile.Formats[1] = request.RequiredProfile.Formats[0];
    }
    SECTION("non-power-of-two samples")
    {
        request.RequiredProfile.Formats[0].SampleCount = 3;
    }
    SECTION("samples without attachment")
    {
        request.RequiredProfile.Formats[0].SampleCount = 4;
    }
    SECTION("malformed preference")
    {
        request.PreferredProfile.FormatCount = 5;
    }
    const auto token = backend::PendingToken;
    StartupInfo failure;
    failure.MaxTextureDimension2D = 777;
    CHECK(ValidateDeviceDescription(request) == DeviceStatus::InvalidDescription);
    CHECK(CreateDevice({}, {}, request, fixture.Device, fixture.Surface, &failure) == DeviceStatus::InvalidDescription);
    CHECK(backend::PendingToken == token);
    CHECK(failure.MaxTextureDimension2D == 777);
    CHECK(GetStartup().State == StartupState::Idle);
}

TEST_CASE("Preferred constraints report misses without failing or changing enabled limits", "[rhi][profile]")
{
    ProfileFixture fixture;
    backend::ActiveKind = Backend::WebGL2;
    DeviceDescription request;
    request.Selection = BackendSelection::WebGL2;
    request.Preferred.Compute = true;
    request.Preferred.IndirectRendering = true;
    request.PreferredProfile.Compute.MinStorageRange = 4;
    REQUIRE(fixture.Start(request) == DeviceStatus::Pending);
    fixture.Complete();
    DeviceInfo info;
    REQUIRE(GetDeviceInfo(fixture.Device, info) == DeviceStatus::Ready);
    CHECK(info.UnmetPreferences.Compute);
    CHECK(info.UnmetPreferences.IndirectRendering);
    CHECK_FALSE(info.Enabled.Compute);
    CHECK(info.UnmetProfilePreference.Requirement == ProfileRequirement::StorageRange);
    CHECK(info.UnmetProfilePreference.Available == 0);
    CHECK(info.Profile.Raster.MaxBufferSize == 64U * 1024U * 1024U);
    REQUIRE(DestroyDevice(fixture.Device) == DeviceStatus::Ready);
    fixture.Surface = {};
    request.RequiredProfile.Compute.MinStorageRange = 4;
    const auto token = backend::PendingToken;
    CHECK(fixture.Start(request) == DeviceStatus::Unsupported);
    CHECK(backend::PendingToken == token);
}

TEST_CASE("Profile admission rechecks fallback and isolates old callbacks", "[rhi][profile]")
{
    ProfileFixture fixture;
    DeviceDescription request;
    request.RequiredProfile.FormatCount = 1;
    request.RequiredProfile.Formats[0] = { .Uses = { .Sampled = true, .Blendable = true }, .MinDimension2D = 1024 };
    REQUIRE(fixture.Start(request) == DeviceStatus::Pending);
    const auto oldToken = backend::PendingToken;
    const auto owner = internal::DeviceOwner();
    reference::FormatBlendUnavailable = true;
    internal::SetFallback([](ludus::foundation::uint32 token, StartupError error) noexcept {
        internal::RecordAttempt(token, Backend::WebGPU, error);
        const auto next = internal::Reissue(token);
        backend::ActiveKind = Backend::WebGL2;
        internal::SelectBackend(next, Backend::WebGL2);
        reference::FormatBlendUnavailable = false;
        internal::Complete(next, StartupError::None, { .MaxFrameDimension2D = 4096, .MaxUniformBufferSize = 16384 });
        return true;
    });
    fixture.Complete();
    DeviceInfo info;
    REQUIRE(GetDeviceInfo(fixture.Device, info) == DeviceStatus::Ready);
    CHECK(internal::DeviceOwner() == owner);
    CHECK(info.Startup.WebGpu.ProfileFailure.Requirement == ProfileRequirement::Blendable);
    CHECK(info.Startup.WebGL2.ProfileFailure.Requirement == ProfileRequirement::None);
    CHECK(info.Startup.ProfileFailure.Requirement == ProfileRequirement::None);
    CHECK(info.Profile.Formats[0].Uses.Blendable);
    CHECK_FALSE(info.Enabled.Compute);
    internal::Fail(oldToken, StartupError::DeviceLost);
    internal::Complete(oldToken, StartupError::None, { .MaxFrameDimension2D = 1, .MaxUniformBufferSize = 16 });
    CHECK(GetDeviceInfo(fixture.Device, info) == DeviceStatus::Ready);
}

TEST_CASE("Format creation uses the admitted snapshot before publishing a resource", "[rhi][profile]")
{
    ProfileFixture fixture;
    reference::FormatUnavailable[static_cast<ludus::foundation::usize>(RasterFormat::Rg8Unorm)] = true;
    REQUIRE(fixture.Start({}) == DeviceStatus::Pending);
    fixture.Complete();
    FormatCapabilities format;
    CHECK(GetFormatCapabilities(fixture.Device, RasterFormat::Rg8Unorm, format) == RasterStatus::Unsupported);
    TextureHandle texture;
    const ludus::foundation::uint8 bytes[4]{};
    CHECK(CreateTexture(fixture.Device,
                        { .Width = 1, .Height = 1, .Format = RasterFormat::Rg8Unorm },
                        { .Bytes = bytes, .RowPitch = 4 },
                        texture) == RasterStatus::Unsupported);
    CHECK(reference::Creates[static_cast<ludus::foundation::usize>(internal::RasterKind::Texture)] == 0);
}

TEST_CASE("Fallback profile failure preserves both diagnostics and clears enabled capabilities", "[rhi][profile]")
{
    ProfileFixture fixture;
    DeviceDescription request;
    request.RequiredProfile.Raster.MinTextureDimension2D = 2048;
    REQUIRE(fixture.Start(request) == DeviceStatus::Pending);
    reference::OverrideRasterLimits = true;
    reference::RasterLimitOverride = {64U * 1024U * 1024U, 1024, 256, 16384, 32, 128};
    internal::SetFallback([](ludus::foundation::uint32 token, StartupError error) noexcept {
        if (backend::ActiveKind != Backend::WebGPU)
        {
            return false;
        }
        internal::RecordAttempt(token, Backend::WebGPU, error);
        const auto next = internal::Reissue(token);
        backend::ActiveKind = Backend::WebGL2;
        internal::SelectBackend(next, Backend::WebGL2);
        reference::RasterLimitOverride.MaxTextureDimension2D = 512;
        internal::Complete(next, StartupError::None, { .MaxFrameDimension2D = 4096, .MaxUniformBufferSize = 16384 });
        return true;
    });
    fixture.Complete();
    DeviceInfo info;
    REQUIRE(GetDeviceInfo(fixture.Device, info) == DeviceStatus::Failed);
    CHECK(info.Startup.Error == StartupError::RequirementsUnsatisfied);
    CHECK(info.Startup.WebGpu.ProfileFailure.Requirement == ProfileRequirement::TextureDimension);
    CHECK(info.Startup.WebGpu.ProfileFailure.Available == 1024);
    CHECK(info.Startup.WebGL2.ProfileFailure.Requirement == ProfileRequirement::TextureDimension);
    CHECK(info.Startup.WebGL2.ProfileFailure.Available == 512);
    CHECK(info.Startup.ProfileFailure.Requested == 2048);
    CHECK(info.Startup.ProfileFailure.Available == 512);
    CHECK(info.Profile.Raster.MaxTextureDimension2D == 0);
    CHECK_FALSE(info.Enabled.PortableRaster);
}

TEST_CASE("Unavailable preferred format uses remain optional", "[rhi][profile]")
{
    ProfileFixture fixture;
    DeviceDescription request;
    request.PreferredProfile.FormatCount = 1;
    request.PreferredProfile.Formats[0] = { .Uses = { .Sampled = true, .Storage = true } };
    REQUIRE(fixture.Start(request) == DeviceStatus::Pending);
    fixture.Complete();
    DeviceInfo info;
    REQUIRE(GetDeviceInfo(fixture.Device, info) == DeviceStatus::Ready);
    CHECK(info.UnmetProfilePreference.Requirement == ProfileRequirement::Storage);
    CHECK(info.UnmetProfilePreference.Requested == 1);
    CHECK(info.UnmetProfilePreference.Available == 0);
    CHECK(info.Profile.Formats[0].Uses.Sampled);
    CHECK_FALSE(info.Profile.Formats[0].Uses.Storage);
}
