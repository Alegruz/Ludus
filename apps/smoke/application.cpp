#include "internal/application.h"
#include "internal/renderer.h"
#include <ludus/foundation/logging/log_format.hpp>
#include <ludus/foundation/profiling/clock.hpp>
#include <ludus/foundation/profiling/profiling.hpp>
namespace ludus::smoke
{
using namespace foundation;
using namespace graphics;
void Application::Shutdown() noexcept
{
    renderer::Shutdown();
    rhi::Shutdown();
    mWindow.Reset();
    mState = State::Stopped;
    mError = rhi::StartupError::None;
    mLastTick = 0;
}
void Application::Fail(State state, rhi::StartupError error) noexcept
{
    LUDUS_LOG_ERROR(logging::LOG_CORE, "Smoke application stopped: graphics error {}", static_cast<uint32>(error));
    // Capture the bounded per-attempt diagnostics before Shutdown clears the RHI
    // session, so QA metadata (which browser paths were tried, and why) survives.
    const auto startup = rhi::GetStartup();
    mWebGpuError = startup.WebGpu.Error;
    mWebGl2Error = startup.WebGL2.Error;
    Shutdown();
    mState = state;
    mError = error;
}
bool Application::Start(rhi::BackendSelection selection) noexcept
{
    Shutdown();
    mSimulation = {};
    mFrames = 0;
    mWebGpuError = rhi::StartupError::None;
    mWebGl2Error = rhi::StartupError::None;
    platform::WindowManager manager;
    if (!manager.Initialize({}) || !manager.CreateWindow(
                                       {
                                           .Name = "Ludus demo",
                                           .Width = 800,
                                           .Height = 600,
                                       },
                                       mWindow))
    {
        Fail(State::Failed, rhi::StartupError::InvalidWindow);
        return false;
    }
    const auto started = rhi::Start({ .Name = "Smoke App", .Version = 1 }, mWindow->GetNativeWindowInfo(), selection);
    if (started != rhi::StartStatus::Ready && started != rhi::StartStatus::Pending)
    {
        Fail(State::Failed, rhi::GetStartup().Error);
        return false;
    }
    mState = State::Loading;
    mLastTick = profiling::NowTicks();
    return true;
}
State Application::Tick() noexcept
{
    if (mState != State::Loading && mState != State::Playing)
    {
        return mState;
    }
    LUDUS_PROFILE_SCOPE(Frame);
    if (!mWindow->HandleEvent({}))
    {
        Shutdown();
        return mState;
    }
    const auto now = profiling::NowTicks();
    const float64 delta = now >= mLastTick ? static_cast<float64>(now - mLastTick) / 1000000000.0 : 0;
    mLastTick = now;
    const auto startup = rhi::GetStartup();
    if (startup.State == rhi::StartupState::Failed || startup.State == rhi::StartupState::DeviceLost)
    {
        Fail(startup.State == rhi::StartupState::DeviceLost ? State::DeviceLost : State::Failed, startup.Error);
        return mState;
    }
    if (startup.State != rhi::StartupState::Ready)
    {
        return mState;
    }
    const auto prepared = renderer::Prepare();
    if (prepared == renderer::State::Failed)
    {
        Fail(State::Failed, rhi::StartupError::RenderingUnavailable);
        return mState;
    }
    if (prepared != renderer::State::Ready)
    {
        return mState;
    }
    mState = State::Playing;
    platform::browser::WindowState input;
    // Browser input/framebuffer handling applies to any browser backend, not
    // just WebGPU. The window being a browser canvas (not the selected graphics
    // backend) is what determines whether to read browser state.
    const bool browser =
        startup.SelectedBackend == rhi::Backend::WebGPU || startup.SelectedBackend == rhi::Backend::WebGL2;
    if (browser)
    {
        (void)mWindow->SetBrowserFramebufferLimit(startup.MaxTextureDimension2D);
        (void)mWindow->GetBrowserState(input);
    }
    else
    {
        input.Visible = true;
    }
    platform::browser::InputEvent event;
    while (mWindow->PollBrowserInput(event))
    {
        // Held keys/buttons and pointer positions come from the snapshot,
        // including after queue overflow, blur and visibility transitions.
    }
    Advance(mSimulation, input, delta);
    const auto rendered = renderer::Render(input, mSimulation);
    if (rendered == rhi::FrameStatus::Failed || rendered == rhi::FrameStatus::InvalidState)
    {
        const auto failed = rhi::GetStartup();
        Fail(failed.State == rhi::StartupState::DeviceLost ? State::DeviceLost : State::Failed,
             failed.Error == rhi::StartupError::None ? rhi::StartupError::RenderingUnavailable : failed.Error);
        return mState;
    }
    if (rendered == rhi::FrameStatus::Ready)
    {
        ++mFrames;
    }
    LUDUS_PROFILE_FRAME();
    return mState;
}
} // namespace ludus::smoke
