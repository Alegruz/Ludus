#include "internal/backend.h"
#include "internal/lifecycle.h"
#include <ludus/graphics/rhi/rhi.h>
namespace ludus::graphics::rhi
{
namespace
{
using ludus::foundation::uint32;
StartupInfo gStartup;
uint32 gToken = 0;
uint32 gNextToken = 1;
bool gLegacy = false;
bool gRendering = false;
bool gFrame = false;
} // namespace
namespace internal
{
bool Current(uint32 token) noexcept
{
    return token != 0 && token == gToken &&
           (gStartup.State == StartupState::Pending || gStartup.State == StartupState::Ready);
}
void Fail(uint32 token, StartupError error) noexcept
{
    if (!Current(token))
    {
        return;
    }
    gToken = 0;
    gStartup.State = error == StartupError::DeviceLost ? StartupState::DeviceLost : StartupState::Failed;
    gStartup.Error = error;
    gFrame = false;
    backend::Shutdown();
}
void Complete(uint32 token, StartupError error, uint32 maxTextureDimension) noexcept
{
    if (!Current(token) || gStartup.State != StartupState::Pending)
    {
        return;
    }
    if (error != StartupError::None)
    {
        Fail(token, error);
        return;
    }
    gStartup.MaxTextureDimension2D = maxTextureDimension;
    gStartup.State = StartupState::Ready;
}
} // namespace internal
StartStatus Start(const ApplicationInfo& app, const WindowInfo& window) noexcept
{
    if (gLegacy || gStartup.State != StartupState::Idle)
    {
        return StartStatus::Busy;
    }
    gStartup.SelectedBackend = backend::Kind();
    if (gNextToken == 0)
    {
        gStartup.State = StartupState::Failed;
        gStartup.Error = StartupError::GenerationExhausted;
        return StartStatus::Failed;
    }
    gToken = gNextToken++;
    gStartup.State = StartupState::Pending;
    const auto error = backend::Start(app, window, gToken);
    if (error != StartupError::None)
    {
        internal::Fail(gToken, error);
    }
    if (gStartup.State == StartupState::Ready)
    {
        return StartStatus::Ready;
    }
    return gStartup.State == StartupState::Pending ? StartStatus::Pending : StartStatus::Failed;
}
StartupInfo GetStartup() noexcept
{
    auto result = gStartup;
    result.SelectedBackend = backend::Kind();
    return result;
}
FrameStatus SetFrameTarget(const FrameTarget& target) noexcept
{
    if (gStartup.State != StartupState::Ready)
    {
        return FrameStatus::NotReady;
    }
    if (gFrame)
    {
        return FrameStatus::InvalidState;
    }
    return backend::SetTarget(target);
}
FrameStatus BeginFrameStatus() noexcept
{
    if (gStartup.State != StartupState::Ready)
    {
        return FrameStatus::NotReady;
    }
    if (gFrame)
    {
        return FrameStatus::InvalidState;
    }
    const auto result = backend::Begin();
    gFrame = result == FrameStatus::Ready;
    return result;
}
FrameStatus EndFrameStatus() noexcept
{
    if (gStartup.State != StartupState::Ready)
    {
        return FrameStatus::NotReady;
    }
    if (!gFrame)
    {
        return FrameStatus::InvalidState;
    }
    gFrame = false;
    return backend::End();
}
bool Initialize(const ApplicationInfo& app) noexcept
{
    if (gStartup.State != StartupState::Idle)
    {
        return false;
    }
    gLegacy = backend::Initialize(app);
    return gLegacy;
}
bool ConnectWindow(const WindowInfo& window) noexcept
{
    return gLegacy && backend::ConnectWindow(window);
}
bool InitializeRendering() noexcept
{
    if (!gLegacy)
    {
        return false;
    }
    if (!gRendering)
    {
        gRendering = backend::InitializeRendering();
    }
    return gRendering;
}
void ShutdownRendering() noexcept
{
    if (gLegacy && gRendering)
    {
        backend::ShutdownRendering();
    }
    gRendering = false;
}
void Shutdown() noexcept
{
    gToken = 0;
    backend::Shutdown();
    gStartup = {};
    gLegacy = false;
    gRendering = false;
    gFrame = false;
}
bool BeginFrame() noexcept
{
    return gLegacy ? (gRendering && backend::BeginFrame()) : BeginFrameStatus() == FrameStatus::Ready;
}
bool EndFrame() noexcept
{
    return gLegacy ? (gRendering && backend::EndFrame()) : EndFrameStatus() == FrameStatus::Ready;
}
} // namespace ludus::graphics::rhi
