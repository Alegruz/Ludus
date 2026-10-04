#pragma once

#include <ludus/foundation/base/core.h>
#include <ludus/foundation/base/pointer.hpp>

#include <ludus/platform/base/window.h>

#include <ludus/graphics/rhi/rhi.h>

#include "lifecycle.h"
#include "simulation.h"

namespace ludus::smoke
{
enum class State : foundation::uint8
{
    Stopped,
    Loading,
    Playing,
    Failed,
    DeviceLost
};
// One main-thread application, matching the RHI's single-session contract.
class Application final
{
public:
    Application() noexcept;
    ~Application();
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;
    // Browser backend policy. Native ignores it (Vulkan). Default Auto attempts
    // WebGPU, then one WebGL 2 fallback; forced values diagnose a single path.
    bool Start(graphics::rhi::BackendSelection selection = graphics::rhi::BackendSelection::Auto) noexcept;
    State Tick() noexcept;
    void Shutdown() noexcept;
    [[nodiscard]] State GetState() const noexcept
    {
        return mState;
    }
    [[nodiscard]] graphics::rhi::StartupError GetError() const noexcept
    {
        return mError;
    }
    [[nodiscard]] const Simulation& GetSimulation() const noexcept
    {
        return mSimulation;
    }
    [[nodiscard]] foundation::uint32 GetFrames() const noexcept
    {
        return mFrames;
    }
    [[nodiscard]] const lifecycle::Runner& GetLifecycle() const noexcept
    {
        return mLifecycle;
    }
    // Bounded per-attempt diagnostics for QA, captured before the RHI session is
    // torn down on failure so they survive into the status report.
    [[nodiscard]] graphics::rhi::StartupError GetWebGpuError() const noexcept
    {
        return mWebGpuError;
    }
    [[nodiscard]] graphics::rhi::StartupError GetWebGl2Error() const noexcept
    {
        return mWebGl2Error;
    }

private:
    void Fail(State, graphics::rhi::StartupError, foundation::usize nodeId = 1) noexcept;
    static lifecycle::StartResult StartWindow(void*) noexcept;
    static lifecycle::StartResult StartRhi(void*) noexcept;
    static lifecycle::StartResult PollRhi(void*) noexcept;
    static lifecycle::StartResult PrepareRenderer(void*) noexcept;
    static lifecycle::StopResult StopWindow(void*) noexcept;
    static lifecycle::StopResult StopRhi(void*) noexcept;
    static lifecycle::StopResult StopRenderer(void*) noexcept;
    foundation::UniquePtr<platform::Window> mWindow;
    static constexpr foundation::usize NodeCount = 3;
    lifecycle::Node mNodes[NodeCount];
    lifecycle::NodeRecord mRecords[NodeCount];
    foundation::usize mJournal[NodeCount]{};
    lifecycle::Runner mLifecycle;
    graphics::rhi::BackendSelection mSelection = graphics::rhi::BackendSelection::Auto;
    Simulation mSimulation;
    State mState = State::Stopped;
    graphics::rhi::StartupError mError = graphics::rhi::StartupError::None;
    graphics::rhi::StartupError mWebGpuError = graphics::rhi::StartupError::None;
    graphics::rhi::StartupError mWebGl2Error = graphics::rhi::StartupError::None;
    foundation::uint64 mLastTick = 0;
    foundation::uint32 mFrames = 0;
};
} // namespace ludus::smoke
