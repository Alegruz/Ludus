#pragma once

// Private host session: owns the loaded gameplay module, its instance, the host
// services, the control-channel framing and the per-frame command dispatch
// (project-live-reload design 3/4/7/10). One session per host process. Gameplay
// callbacks run on the host thread; the control fd is polled non-blocking each
// frame and validated commands are dispatched at the frame boundary.
//
// The reload transaction (Quiesce/Snapshot/Stage/Commit/Retire) and the live
// property/asset commands are implemented here so one owner serializes reload
// against property/pause/step commands and Stop supersedes queued work.

#include "internal/host_services.h"
#include "internal/module_loader.h"
#include "internal/protocol_codec.h"

#include <ludus/runtime/game_api/api.h>
#include <ludus/runtime/game_host/host.h>
#include <ludus/runtime/game_host/protocol.h>

#include <ludus/foundation/base/types.h>

#include <string>
#include <string_view>

namespace ludus::runtime::game_host
{
using ludus::foundation::int32;
using ludus::foundation::uint64;
using ludus::foundation::usize;

// PlayState mirrored from design section 4. The host owns transitions.
enum class PlayState : ludus::foundation::uint8
{
    Stopped = 0,
    Starting = 1,
    Running = 2,
    Paused = 3,
    Reloading = 4,
    Stopping = 5,
    Failed = 6,
    CleanupUnknown = 7
};

[[nodiscard]] std::string_view PlayStateName(PlayState state) noexcept;

// Run a statically linked gameplay implementation (shipping build, no dlopen,
// no reload). Builds the table from the in-process entry, runs the bounded
// frame loop, and retires the instance.
[[nodiscard]] RunResult RunStaticEntry(const HostConfig& config, StaticEntryFn entry) noexcept;

class HostSession final
{
public:
    // projectId/gameId/controlFd are a fixed construction signature.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    HostSession(uint64 projectId, uint64 gameId, int32 controlFd) noexcept;
    ~HostSession() noexcept;

    HostSession(const HostSession&) = delete;
    HostSession& operator=(const HostSession&) = delete;

    // Load the initial generation from an absolute leased path. Emits
    // ModuleReady on success. Returns false and sets Failed on rejection.
    [[nodiscard]] bool LoadInitial(std::string_view modulePath) noexcept;

    // Run the frame loop until Stop (or maxFrames, 0 = unbounded). Polls the
    // control channel each frame, dispatches validated commands at the boundary,
    // advances the active instance, and emits events. Returns the terminal
    // RunResult.
    [[nodiscard]] RunResult RunLoop(uint64 maxFrames) noexcept;

    [[nodiscard]] PlayState State() const noexcept
    {
        return State_;
    }

    [[nodiscard]] uint64 ActiveGeneration() const noexcept
    {
        return ActiveGeneration_;
    }

    [[nodiscard]] uint64 OutstandingHostAllocations() const noexcept
    {
        return Services_.OutstandingAllocations();
    }

    // Advance the active instance one frame (deterministic test driver). No I/O.
    void AdvanceOneFrame() noexcept
    {
        StepFrame();
    }

    // Read the active instance's live property values into a caller buffer for
    // state-equality checks. Returns bytes written, or 0 on failure.
    [[nodiscard]] usize ReadActiveProperties(ludus::foundation::uint8* buffer, usize capacity) noexcept;

    // Capture the active instance's full checkpoint body into a caller buffer
    // for EXACT supported-state comparison (position/velocity/RNG/time/etc).
    // Returns bytes written, or 0 on failure. Test driver only.
    [[nodiscard]] usize CaptureCheckpoint(ludus::foundation::uint8* buffer, usize capacity) noexcept;

    // Simulation ticks advanced so far (advances only when not paused).
    [[nodiscard]] uint64 SimTicks() const noexcept
    {
        return SimTicks_;
    }

    // Reload the active module to a new generation at a frame boundary. Keeps A
    // alive until B validates; recoverable rejection resumes A unchanged
    // (design 7). Returns the command status.
    [[nodiscard]] protocol::CommandStatus ReloadTo(std::string_view newModulePath, uint64 newGeneration) noexcept;

private:
    // Emit an event frame on the control channel (no-op if no control fd).
    void Emit(const protocol::Message& message) noexcept;
    void EmitReloadPhase(protocol::ReloadPhase phase) noexcept;
    void EmitCommandResult(uint64 requestId, protocol::CommandStatus status, std::string_view message) noexcept;

    // Poll + dispatch any pending control commands. Returns false to stop.
    [[nodiscard]] bool PumpControl() noexcept;
    void Dispatch(const protocol::Message& command) noexcept;

    // One Update tick of the active instance (unless paused/stepping gate it).
    void StepFrame() noexcept;

    // Whether the active module advertises the Reload capability (gates calls to
    // the optional Quiesce/Resume/checkpoint/candidate callbacks).
    [[nodiscard]] bool ModuleHasReload() const noexcept;

    uint64 ProjectId_ = 0;
    uint64 GameId_ = 0;
    int32 ControlFd_ = -1;

    PlayState State_ = PlayState::Stopped;
    bool StopLatched_ = false;
    bool StepRequested_ = false;
    uint64 FrameIndex_ = 0;
    uint64 SimTicks_ = 0;                    // Simulation ticks; advance only when not paused (no catch-up).
    game_api::RenderParams LastRender_ = {}; // Last render params from Update (asset/visible-change checks).
    uint64 SessionId_ = 1;
    uint64 ActiveGeneration_ = 0;
    uint64 SchemaEpoch_ = 0;

    LoadedModule Active_;
    HostServiceProvider Services_;
    game_api::GameInstance* Instance_ = nullptr;

    protocol::FrameReader Reader_;
};
} // namespace ludus::runtime::game_host
