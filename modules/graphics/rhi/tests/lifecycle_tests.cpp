#include "internal/backend.h"
#include "internal/lifecycle.h"
#include <catch2/catch_test_macros.hpp>
#include <ludus/graphics/rhi/rhi.h>
namespace ludus::graphics::rhi::backend
{
using ludus::foundation::uint32;
uint32 PendingToken = 0;
uint32 ShutdownCount = 0;
StartupError ImmediateError = StartupError::None;
FrameStatus NextFrame = FrameStatus::Ready;
Backend Kind() noexcept
{
    return Backend::WebGPU;
}
StartupError Start(const ApplicationInfo&, const WindowInfo&, uint32 token) noexcept
{
    PendingToken = token;
    return ImmediateError;
}
void Shutdown() noexcept
{
    ++ShutdownCount;
}
bool Initialize(const ApplicationInfo&) noexcept
{
    return false;
}
bool ConnectWindow(const WindowInfo&) noexcept
{
    return false;
}
bool InitializeRendering() noexcept
{
    return false;
}
void ShutdownRendering() noexcept {}
bool BeginFrame() noexcept
{
    return false;
}
bool EndFrame() noexcept
{
    return false;
}
FrameStatus SetTarget(const FrameTarget&) noexcept
{
    return FrameStatus::Ready;
}
FrameStatus Begin() noexcept
{
    return NextFrame;
}
FrameStatus End() noexcept
{
    return FrameStatus::Ready;
}
} // namespace ludus::graphics::rhi::backend
using namespace ludus::graphics::rhi;
TEST_CASE("RHI pending requests are nonblocking and invalidated across restart", "[rhi][lifecycle]")
{
    Shutdown();
    backend::ImmediateError = StartupError::None;
    REQUIRE(Start({}, {}) == StartStatus::Pending);
    const auto old = backend::PendingToken;
    CHECK(BeginFrameStatus() == FrameStatus::NotReady);
    CHECK(EndFrameStatus() == FrameStatus::NotReady);
    CHECK(Start({}, {}) == StartStatus::Busy);
    Shutdown();
    Shutdown();
    CHECK(GetStartup().State == StartupState::Idle);
    REQUIRE(Start({}, {}) == StartStatus::Pending);
    const auto current = backend::PendingToken;
    CHECK(current != old);
    internal::Complete(old, StartupError::None, 999);
    internal::Fail(old, StartupError::DeviceLost);
    CHECK(GetStartup().State == StartupState::Pending);
    internal::Complete(current, StartupError::None, 4096);
    CHECK(GetStartup().State == StartupState::Ready);
    CHECK(GetStartup().MaxTextureDimension2D == 4096);
    CHECK(Start({}, {}) == StartStatus::Busy);
    CHECK(EndFrameStatus() == FrameStatus::InvalidState);
    CHECK(BeginFrameStatus() == FrameStatus::Ready);
    CHECK(BeginFrameStatus() == FrameStatus::InvalidState);
    CHECK(EndFrameStatus() == FrameStatus::Ready);
    internal::Fail(current, StartupError::DeviceLost);
    CHECK(GetStartup().State == StartupState::DeviceLost);
    CHECK(BeginFrameStatus() == FrameStatus::NotReady);
    internal::Complete(current, StartupError::None, 8192);
    CHECK(GetStartup().State == StartupState::DeviceLost);
    Shutdown();
}
TEST_CASE("RHI request failures remain explicit and require shutdown before retry", "[rhi][lifecycle]")
{
    const StartupError errors[] = {StartupError::InstanceUnavailable,
                                   StartupError::AdapterUnavailable,
                                   StartupError::DeviceUnavailable,
                                   StartupError::SurfaceUnavailable,
                                   StartupError::RenderingUnavailable,
                                   StartupError::Validation};
    for (const auto error : errors)
    {
        Shutdown();
        backend::ImmediateError = StartupError::None;
        REQUIRE(Start({}, {}) == StartStatus::Pending);
        const auto token = backend::PendingToken;
        internal::Complete(token, error, 0);
        CHECK(GetStartup().State == StartupState::Failed);
        CHECK(GetStartup().Error == error);
        CHECK_FALSE(internal::Current(token));
        CHECK(Start({}, {}) == StartStatus::Busy);
        CHECK_FALSE(BeginFrame());
        Shutdown();
        backend::ImmediateError = error;
        CHECK(Start({}, {}) == StartStatus::Failed);
        CHECK(GetStartup().Error == error);
    }
    Shutdown();
    backend::ImmediateError = StartupError::None;
}

TEST_CASE("Skipped RHI frames never open a frame and targets cannot change during encoding", "[rhi][lifecycle]")
{
    Shutdown();
    backend::ImmediateError = StartupError::None;
    CHECK(SetFrameTarget({}) == FrameStatus::NotReady);
    REQUIRE(Start({}, {}) == StartStatus::Pending);
    internal::Complete(backend::PendingToken, StartupError::None, 4096);
    backend::NextFrame = FrameStatus::Skipped;
    CHECK(SetFrameTarget({}) == FrameStatus::Ready);
    CHECK(BeginFrameStatus() == FrameStatus::Skipped);
    CHECK(EndFrameStatus() == FrameStatus::InvalidState);
    CHECK(SetFrameTarget({ .Width = 640, .Height = 360 }) == FrameStatus::Ready);
    backend::NextFrame = FrameStatus::Ready;
    REQUIRE(BeginFrameStatus() == FrameStatus::Ready);
    CHECK(SetFrameTarget({}) == FrameStatus::InvalidState);
    internal::Fail(backend::PendingToken, StartupError::DeviceLost);
    CHECK(EndFrameStatus() == FrameStatus::NotReady);
    Shutdown();
}
