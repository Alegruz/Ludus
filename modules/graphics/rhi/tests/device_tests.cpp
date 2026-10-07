#include "internal/device.h"
#include "internal/lifecycle.h"
#include "internal/resources.h"

#include <ludus/graphics/rhi/device.h>

#include <catch2/catch_test_macros.hpp>

namespace ludus::graphics::rhi::backend
{
extern foundation::uint32 PendingToken;
extern StartupError ImmediateError;
extern FrameStatus NextFrame;
extern Backend ActiveKind;
extern bool SelectionSupported;
extern ResourceStatus NextResource;
extern foundation::uint32 LastResource;
extern foundation::uint32 Draws;
extern bool LoseDuringCreation;
} // namespace ludus::graphics::rhi::backend
using namespace ludus::graphics::rhi;
namespace
{
void Reset() noexcept
{
    Shutdown();
    backend::ImmediateError = StartupError::None;
    backend::NextFrame = FrameStatus::Ready;
    backend::ActiveKind = Backend::WebGPU;
    backend::SelectionSupported = true;
    backend::NextResource = ResourceStatus::Ready;
    backend::LoseDuringCreation = false;
}
void Ready() noexcept
{
    internal::Complete(backend::PendingToken,
                       StartupError::None,
                       { .MaxFrameDimension2D = 4096, .MaxUniformBufferSize = 16384 });
}
} // namespace

TEST_CASE("Explicit RHI owners cancel pending work and isolate foreign surfaces and resources", "[rhi][device]")
{
    Reset();
    DeviceHandle device;
    SurfaceHandle surface;
    DeviceDescription description;
    description.Limits.MinUniformBufferSize = 48;
    description.Preferred.Compute = true;
    REQUIRE(CreateDevice({}, {}, description, device, surface) == DeviceStatus::Pending);
    const auto cancelledToken = backend::PendingToken;
    const auto oldDevice = device;
    const auto oldSurface = surface;
    description.Limits.MinUniformBufferSize = 99999;
    DeviceInfo info;
    REQUIRE(GetDeviceInfo(device, info) == DeviceStatus::Pending);
    CHECK(info.Description.Limits.MinUniformBufferSize == 48);
    CHECK(info.Description.Preferred.Compute);
    CHECK_FALSE(info.Enabled.FullscreenRaster);
    UniformHandle uniform;
    CHECK(CreateUniform(device, 48, uniform) == ResourceStatus::NotReady);
    CHECK(BeginFrame(device, surface) == DeviceStatus::NotReady);
    CHECK(CreateDevice({}, {}, {}, device, surface) == DeviceStatus::InvalidState);
    REQUIRE(DestroyDevice(device) == DeviceStatus::Ready);
    CHECK(DestroyDevice(device) == DeviceStatus::InvalidHandle);
    DeviceHandle next;
    SurfaceHandle target;
    REQUIRE(CreateDevice({}, {}, {}, next, target) == DeviceStatus::Pending);
    internal::Complete(cancelledToken, StartupError::None, { .MaxFrameDimension2D = 1, .MaxUniformBufferSize = 16 });
    internal::Fail(cancelledToken, StartupError::DeviceLost);
    CHECK(GetDeviceInfo(next, info) == DeviceStatus::Pending);
    Ready();
    REQUIRE(GetDeviceInfo(next, info) == DeviceStatus::Ready);
    CHECK(info.Supported.FullscreenRaster);
    CHECK(info.Enabled.FullscreenRaster);
    CHECK_FALSE(info.Enabled.Compute);
    const auto before = info.Startup.Capabilities.MaxFrameDimension2D;
    CHECK(GetDeviceInfo(oldDevice, info) == DeviceStatus::InvalidHandle);
    CHECK(info.Startup.Capabilities.MaxFrameDimension2D == before);
    auto staleOwner = oldDevice;
    CHECK(DestroyDevice(staleOwner) == DeviceStatus::InvalidHandle);
    CHECK(GetDeviceInfo(next, info) == DeviceStatus::Ready);
    CHECK(SetFrameTarget(next, oldSurface, {}) == DeviceStatus::InvalidHandle);
    CHECK(BeginFrame(oldDevice, target) == DeviceStatus::InvalidHandle);
    CHECK(EndFrame(oldDevice) == DeviceStatus::InvalidHandle);
    CHECK(CreateUniform(oldDevice, 48, uniform) == ResourceStatus::InvalidHandle);
    REQUIRE(CreateUniform(next, 48, uniform) == ResourceStatus::Ready);
    const auto oldUniform = uniform;
    FrameInfo acquired{ .Width = 777 };
    CHECK(GetFrameInfo(next, target, acquired) == DeviceStatus::InvalidState);
    CHECK(acquired.Width == 777);
    backend::NextFrame = FrameStatus::Skipped;
    CHECK(BeginFrame(next, target) == DeviceStatus::Skipped);
    CHECK(EndFrame(next) == DeviceStatus::InvalidState);
    backend::NextFrame = FrameStatus::Ready;
    REQUIRE(BeginFrame(next, target) == DeviceStatus::Ready);
    CHECK(BeginFrame(next, target) == DeviceStatus::InvalidState);
    CHECK(SetFrameTarget(next, target, {}) == DeviceStatus::InvalidState);
    CHECK(GetFrameInfo(next, oldSurface, acquired) == DeviceStatus::InvalidHandle);
    CHECK(acquired.Width == 777);
    REQUIRE(GetFrameInfo(next, target, acquired) == DeviceStatus::Ready);
    CHECK(acquired.Width == 96);
    CHECK(EndFrame(next) == DeviceStatus::Ready);
    REQUIRE(DestroyDevice(next) == DeviceStatus::Ready);
    target = {};
    REQUIRE(CreateDevice({}, {}, {}, next, target) == DeviceStatus::Pending);
    Ready();
    CHECK(GetStatus(next, oldUniform) == ResourceStatus::InvalidHandle);
    CHECK(Destroy(next, oldUniform) == ResourceStatus::InvalidHandle);
    REQUIRE(DestroyDevice(next) == DeviceStatus::Ready);
}

TEST_CASE("Required workloads reject before startup and synchronous failures preserve outputs", "[rhi][device]")
{
    Reset();
    DeviceHandle device;
    SurfaceHandle surface;
    DeviceDescription description;
    const auto token = backend::PendingToken;
    StartupInfo diagnostic;
    diagnostic.MaxTextureDimension2D = 777;
    description.Required.PortableRaster = true;
    CHECK(ValidateDeviceDescription(description) == DeviceStatus::Unsupported);
    CHECK(CreateDevice({}, {}, description, device, surface, &diagnostic) == DeviceStatus::Unsupported);
    CHECK(diagnostic.MaxTextureDimension2D == 777);
    CHECK(backend::PendingToken == token);
    CHECK(GetStartup().State == StartupState::Idle);
    description.Required.PortableRaster = false;
    description.Required.Compute = true;
    CHECK(CreateDevice({}, {}, description, device, surface) == DeviceStatus::Unsupported);
    description.Required.Compute = false;
    description.Required.IndirectRendering = true;
    CHECK(CreateDevice({}, {}, description, device, surface) == DeviceStatus::Unsupported);
    description.Required.IndirectRendering = false;
    description.Selection = static_cast<BackendSelection>(255);
    CHECK(CreateDevice({}, {}, description, device, surface) == DeviceStatus::InvalidDescription);
    description.Selection = BackendSelection::Auto;
    backend::ImmediateError = StartupError::DeviceUnavailable;
    CHECK(CreateDevice({}, {}, description, device, surface, &diagnostic) == DeviceStatus::Failed);
    CHECK(diagnostic.State == StartupState::Failed);
    CHECK(diagnostic.Error == StartupError::DeviceUnavailable);
    CHECK(GetStartup().State == StartupState::Idle);
    backend::ImmediateError = StartupError::None;
    REQUIRE(CreateDevice({}, {}, description, device, surface) == DeviceStatus::Pending);
    Ready();
    REQUIRE(DestroyDevice(device) == DeviceStatus::Ready);
    // A borrowed stale target must be cleared explicitly before it becomes an output.
    CHECK(CreateDevice({}, {}, description, device, surface) == DeviceStatus::InvalidState);
    surface = {};
    REQUIRE(Start({}, {}) == StartStatus::Pending);
    const auto facadeToken = backend::PendingToken;
    CHECK(CreateDevice({}, {}, description, device, surface) == DeviceStatus::InvalidState);
    CHECK(backend::PendingToken == facadeToken);
    description.Required.Compute = true;
    CHECK(CreateDevice({}, {}, description, device, surface) == DeviceStatus::InvalidState);
    CHECK(backend::PendingToken == facadeToken);
    CHECK(GetStartup().State == StartupState::Pending);
    Shutdown();
}

TEST_CASE("Explicit owner survives fallback attempts but loss stops all work", "[rhi][device]")
{
    Reset();
    DeviceHandle device;
    SurfaceHandle surface;
    DeviceDescription description;
    description.Limits.MinUniformBufferSize = 48;
    REQUIRE(CreateDevice({}, {}, description, device, surface) == DeviceStatus::Pending);
    const auto first = backend::PendingToken;
    const auto owner = internal::DeviceOwner();
    internal::SetFallback([](ludus::foundation::uint32 token, StartupError error) noexcept {
        internal::RecordAttempt(token, Backend::WebGPU, error);
        const auto retry = internal::Reissue(token);
        internal::SelectBackend(retry, Backend::WebGL2);
        internal::Complete(retry, StartupError::None, { .MaxFrameDimension2D = 2048, .MaxUniformBufferSize = 16384 });
        return true;
    });
    internal::Complete(first, StartupError::None, { .MaxFrameDimension2D = 4096, .MaxUniformBufferSize = 16 });
    DeviceInfo info;
    REQUIRE(GetDeviceInfo(device, info) == DeviceStatus::Ready);
    CHECK(internal::DeviceOwner() == owner);
    CHECK(info.Startup.SelectedBackend == Backend::WebGL2);
    CHECK(info.Startup.WebGpu.Error == StartupError::RequirementsUnsatisfied);
    CHECK(info.Startup.WebGL2.Error == StartupError::None);
    CHECK(info.Enabled.FullscreenRaster);
    CHECK_FALSE(info.Enabled.Compute);
    const auto current = internal::Reissue(first);
    CHECK(current == 0);
    // The current fallback token is not the cancelled first request.
    internal::Fail(first, StartupError::DeviceLost);
    CHECK(GetDeviceInfo(device, info) == DeviceStatus::Ready);
    // Resource creation can synchronously notify loss before returning.
    Shutdown();
    CHECK(GetDeviceInfo(device, info) == DeviceStatus::InvalidHandle);
    device = {};
    surface = {};
    REQUIRE(CreateDevice({}, {}, {}, device, surface) == DeviceStatus::Pending);
    Ready();
    UniformHandle uniform;
    REQUIRE(CreateUniform(device, 16, uniform) == ResourceStatus::Ready);
    const auto lostToken = backend::PendingToken;
    internal::Fail(lostToken, StartupError::DeviceLost);
    CHECK(GetDeviceInfo(device, info) == DeviceStatus::DeviceLost);
    CHECK_FALSE(info.Supported.FullscreenRaster);
    CHECK_FALSE(info.Enabled.FullscreenRaster);
    CHECK(GetStatus(device, uniform) == ResourceStatus::DeviceLost);
    CHECK(CreateUniform(device, 16, uniform) == ResourceStatus::DeviceLost);
    CHECK(BeginFrame(device, surface) == DeviceStatus::DeviceLost);
    CHECK(EndFrame(device) == DeviceStatus::DeviceLost);
    internal::Complete(lostToken, StartupError::None, { .MaxFrameDimension2D = 4096, .MaxUniformBufferSize = 16384 });
    CHECK(GetDeviceInfo(device, info) == DeviceStatus::DeviceLost);
    REQUIRE(DestroyDevice(device) == DeviceStatus::Ready);
}

TEST_CASE("Published startup failure remains owned until explicit destruction", "[rhi][device]")
{
    Reset();
    DeviceHandle device;
    SurfaceHandle surface;
    DeviceDescription description;
    description.Limits.MinUniformBufferSize = 16400;
    REQUIRE(CreateDevice({}, {}, description, device, surface) == DeviceStatus::Pending);
    Ready();
    DeviceInfo info;
    CHECK(GetDeviceInfo(device, info) == DeviceStatus::Failed);
    CHECK(info.Startup.Error == StartupError::RequirementsUnsatisfied);
    CHECK(info.Startup.UnmetRequirement == RequirementFailure::UniformBufferSize);
    CHECK_FALSE(info.Enabled.FullscreenRaster);
    CHECK(SetFrameTarget(device, surface, {}) == DeviceStatus::Failed);
    CHECK(BeginFrame(device, surface) == DeviceStatus::Failed);
    CHECK(EndFrame(device) == DeviceStatus::Failed);
    FrameInfo frame{ .Width = 777 };
    CHECK(GetFrameInfo(device, surface, frame) == DeviceStatus::Failed);
    CHECK(frame.Width == 777);
    DeviceHandle other;
    SurfaceHandle otherTarget;
    CHECK(CreateDevice({}, {}, {}, other, otherTarget) == DeviceStatus::InvalidState);
    REQUIRE(DestroyDevice(device) == DeviceStatus::Ready);
}

TEST_CASE("Synchronous resource loss reports the explicit device loss result", "[rhi][device]")
{
    Reset();
    DeviceHandle device;
    SurfaceHandle surface;
    REQUIRE(CreateDevice({}, {}, {}, device, surface) == DeviceStatus::Pending);
    Ready();
    backend::LoseDuringCreation = true;
    UniformHandle interrupted;
    CHECK(CreateUniform(device, 16, interrupted) == ResourceStatus::DeviceLost);
    DeviceInfo info;
    CHECK(GetDeviceInfo(device, info) == DeviceStatus::DeviceLost);
    CHECK(GetStatus(device, interrupted) == ResourceStatus::DeviceLost);
    REQUIRE(DestroyDevice(device) == DeviceStatus::Ready);
}

TEST_CASE("Device identity exhaustion never wraps to an old owner", "[rhi][device]")
{
    const auto maximum = ~ludus::foundation::uint64{0};
    internal::DeviceOwnerSequence sequence{maximum - 1};
    CHECK(sequence.Take() == maximum - 1);
    CHECK(sequence.Take() == maximum);
    CHECK(sequence.Take() == 0);
    CHECK(sequence.Take() == 0);
}
