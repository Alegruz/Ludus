#pragma once

#include <ludus/foundation/base/core.h>
#include <ludus/foundation/base/pointer.hpp>

#include <ludus/platform/base/window.h>

#include <ludus/graphics/rhi/rhi.h>

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
    bool Start() noexcept;
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

private:
    void Fail(State, graphics::rhi::StartupError) noexcept;
    foundation::UniquePtr<platform::Window> mWindow;
    Simulation mSimulation;
    State mState = State::Stopped;
    graphics::rhi::StartupError mError = graphics::rhi::StartupError::None;
    foundation::uint64 mLastTick = 0;
    foundation::uint32 mFrames = 0;
};
} // namespace ludus::smoke
