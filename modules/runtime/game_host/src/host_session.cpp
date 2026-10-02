#include "internal/host_session.h"

#include "internal/identity.h"

#include <ludus/foundation/base/core.h>
#include <ludus/foundation/logging/log_format.hpp>

#include <array>
#include <cerrno>
#include <string>
#include <vector>

#include <poll.h>
#include <unistd.h>

namespace ludus::runtime::game_host
{
namespace
{
LUDUS_DEFINE_LOG_CATEGORY(LOG_SESSION, "GameHost");

using game_api::ByteSpan;
using game_api::ByteView;
using game_api::CheckpointHeader;
using game_api::CreateInfo;
using game_api::FrameInput;
using game_api::GameCandidate;
using game_api::GameInstance;
using game_api::RenderParams;
using game_api::Status;
using protocol::CommandKind;
using protocol::CommandStatus;
using protocol::EventKind;
using protocol::Message;
using protocol::ReloadPhase;

// Checkpoint body cap (design 8): 16 MiB.
constexpr usize kMaxCheckpointBody = static_cast<usize>(16) * 1024 * 1024;
} // namespace

std::string_view PlayStateName(PlayState state) noexcept
{
    switch (state)
    {
        case PlayState::Stopped:
            return "Stopped";
        case PlayState::Starting:
            return "Starting";
        case PlayState::Running:
            return "Running";
        case PlayState::Paused:
            return "Paused";
        case PlayState::Reloading:
            return "Reloading";
        case PlayState::Stopping:
            return "Stopping";
        case PlayState::Failed:
            return "Failed";
        case PlayState::CleanupUnknown:
            return "CleanupUnknown";
    }
    return "Unknown";
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
HostSession::HostSession(uint64 projectId, uint64 gameId, int32 controlFd) noexcept
    : ProjectId_(projectId), GameId_(gameId), ControlFd_(controlFd)
{
}

HostSession::~HostSession() noexcept
{
    if (Instance_ != nullptr && Active_.IsLoaded())
    {
        Active_.Table().Destroy(Instance_);
        Instance_ = nullptr;
    }
    Active_.Close();
}

void HostSession::Emit(const Message& message) noexcept
{
    if (ControlFd_ < 0)
    {
        return;
    }
    const std::string payload = message.Serialize();
    std::vector<ludus::foundation::uint8> frame;
    if (!protocol::EncodeFrame(payload, frame))
    {
        return;
    }
    usize written = 0;
    while (written < frame.size())
    {
        const ssize_t n = ::write(ControlFd_, frame.data() + written, frame.size() - written);
        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return; // broken pipe / closed reader: stop emitting
        }
        written += static_cast<usize>(n);
    }
}

void HostSession::EmitReloadPhase(ReloadPhase phase) noexcept
{
    Message m;
    m.SetString("event", protocol::EventKindName(EventKind::ReloadPhase));
    m.SetUint("protocol", protocol::kProtocolVersion);
    m.SetHexId("session", SessionId_);
    m.SetString("phase", protocol::ReloadPhaseName(phase));
    m.SetHexId("generation", ActiveGeneration_);
    Emit(m);
}

void HostSession::EmitCommandResult(uint64 requestId, CommandStatus status, std::string_view message) noexcept
{
    Message m;
    m.SetString("event", protocol::EventKindName(EventKind::CommandResult));
    m.SetUint("protocol", protocol::kProtocolVersion);
    m.SetHexId("session", SessionId_);
    m.SetHexId("request", requestId);
    m.SetString("status", protocol::CommandStatusName(status));
    m.SetHexId("generation", ActiveGeneration_);
    m.SetUint("schema_epoch", SchemaEpoch_);
    if (!message.empty())
    {
        m.SetString("message", message);
    }
    Emit(m);
}

bool HostSession::LoadInitial(std::string_view modulePath) noexcept
{
    State_ = PlayState::Starting;
    ActiveGeneration_ = 1;
    const LoadStatus loadStatus =
        LoadModule(modulePath, ActiveGeneration_, CurrentHostIdentity(), CurrentAbiMajor(), CurrentAbiMinor(), Active_);
    if (loadStatus != LoadStatus::Ok)
    {
        LUDUS_LOG_ERROR(LOG_SESSION, "initial load rejected: {}", LoadStatusName(loadStatus));
        State_ = PlayState::Failed;
        return false;
    }

    CreateInfo info = {};
    info.StructSize = static_cast<uint32>(sizeof(CreateInfo));
    info.Services = &Services_.Services();
    info.ProjectId = ProjectId_;
    info.GameId = GameId_;
    info.ModuleGeneration = ActiveGeneration_;
    const Status createStatus = Active_.Table().Create(&info, &Instance_);
    if (createStatus != Status::Ok || Instance_ == nullptr)
    {
        LUDUS_LOG_ERROR(LOG_SESSION, "initial Create failed: {}", static_cast<uint32>(createStatus));
        Active_.Close();
        State_ = PlayState::Failed;
        return false;
    }

    State_ = PlayState::Running;

    // SessionReady then ModuleReady declare actual capabilities/build identity.
    {
        Message ready;
        ready.SetString("event", protocol::EventKindName(EventKind::SessionReady));
        ready.SetUint("protocol", protocol::kProtocolVersion);
        ready.SetHexId("session", SessionId_);
        ready.SetString("identity", CurrentHostIdentity());
        ready.SetUint("abi_major", CurrentAbiMajor());
        ready.SetUint("abi_minor", CurrentAbiMinor());
        Emit(ready);
    }
    {
        Message mod;
        mod.SetString("event", protocol::EventKindName(EventKind::ModuleReady));
        mod.SetUint("protocol", protocol::kProtocolVersion);
        mod.SetHexId("session", SessionId_);
        mod.SetHexId("generation", ActiveGeneration_);
        mod.SetUint("capabilities", Active_.Metadata().Capabilities);
        mod.SetUint("property_schema", Active_.Metadata().PropertySchemaVersion);
        mod.SetUint("checkpoint_schema", Active_.Metadata().CheckpointSchemaVersion);
        Emit(mod);
    }
    return true;
}

void HostSession::StepFrame() noexcept
{
    if (Instance_ == nullptr || !Active_.IsLoaded())
    {
        return;
    }
    const bool advancing = (State_ == PlayState::Running) || (State_ == PlayState::Paused && StepRequested_);
    FrameInput input = {};
    input.FrameIndex = FrameIndex_;
    input.DeltaSeconds = 1.0 / 60.0;
    input.ElapsedSeconds = static_cast<ludus::foundation::float64>(FrameIndex_) / 60.0;
    input.Width = 800;
    input.Height = 600;
    input.Paused = (State_ == PlayState::Paused && !StepRequested_) ? 1 : 0;

    RenderParams render = {};
    (void)Active_.Table().Update(Instance_, &input, &render);
    if (advancing)
    {
        ++FrameIndex_;
    }
    StepRequested_ = false;
}

bool HostSession::PumpControl() noexcept
{
    if (ControlFd_ < 0)
    {
        return true;
    }
    // Non-blocking poll so the frame loop is never starved.
    pollfd pfd = {};
    pfd.fd = ControlFd_;
    pfd.events = POLLIN;
    const int ready = ::poll(&pfd, 1, 0);
    if (ready > 0 && (pfd.revents & (POLLIN | POLLHUP)) != 0)
    {
        std::array<ludus::foundation::uint8, 4096> buffer = {};
        const ssize_t n = ::read(ControlFd_, buffer.data(), buffer.size());
        if (n == 0)
        {
            // EOF cancels the session (design 10); it does not reconnect.
            LUDUS_LOG_INFO(LOG_SESSION, "control EOF: ending session");
            return false;
        }
        if (n > 0)
        {
            Reader_.Append(buffer.data(), static_cast<usize>(n));
        }
    }
    std::string payload;
    while (Reader_.Next(payload))
    {
        Message command;
        if (!Message::Parse(payload, command))
        {
            EmitCommandResult(0, CommandStatus::ProtocolError, "malformed frame");
            continue;
        }
        Dispatch(command);
        if (StopLatched_)
        {
            return false;
        }
    }
    if (Reader_.Failed())
    {
        LUDUS_LOG_ERROR(LOG_SESSION, "control framing failure: ending session");
        return false;
    }
    return true;
}

void HostSession::Dispatch(const Message& command) noexcept
{
    std::string type;
    if (!command.GetString("command", type))
    {
        EmitCommandResult(0, CommandStatus::ProtocolError, "missing command");
        return;
    }
    uint64 requestId = 0;
    (void)command.GetHexId("request", requestId);

    if (type == protocol::CommandKindName(CommandKind::Stop))
    {
        StopLatched_ = true;
        State_ = PlayState::Stopping;
        EmitCommandResult(requestId, CommandStatus::Ok, "stopping");
        return;
    }
    if (type == protocol::CommandKindName(CommandKind::Status))
    {
        EmitCommandResult(requestId, CommandStatus::Ok, PlayStateName(State_));
        return;
    }
    if (type == protocol::CommandKindName(CommandKind::Pause))
    {
        if (State_ == PlayState::Running)
        {
            (void)Active_.Table().Quiesce(Instance_);
            State_ = PlayState::Paused;
        }
        EmitCommandResult(requestId, CommandStatus::Ok, PlayStateName(State_));
        return;
    }
    if (type == protocol::CommandKindName(CommandKind::Resume))
    {
        if (State_ == PlayState::Paused)
        {
            (void)Active_.Table().Resume(Instance_);
            State_ = PlayState::Running;
        }
        EmitCommandResult(requestId, CommandStatus::Ok, PlayStateName(State_));
        return;
    }
    if (type == protocol::CommandKindName(CommandKind::Step))
    {
        if (State_ == PlayState::Paused)
        {
            StepRequested_ = true;
            EmitCommandResult(requestId, CommandStatus::Ok, "stepped");
        }
        else
        {
            EmitCommandResult(requestId, CommandStatus::InvalidRequest, "step only while paused");
        }
        return;
    }
    if (type == protocol::CommandKindName(CommandKind::Reload))
    {
        std::string path;
        uint64 generation = 0;
        if (!command.GetString("module_path", path) || !command.GetHexId("generation", generation))
        {
            EmitCommandResult(requestId, CommandStatus::InvalidRequest, "reload needs module_path+generation");
            return;
        }
        const CommandStatus status = ReloadTo(path, generation);
        EmitCommandResult(requestId, status, protocol::CommandStatusName(status));
        return;
    }

    EmitCommandResult(requestId, CommandStatus::InvalidRequest, "unsupported command");
}

CommandStatus HostSession::ReloadTo(std::string_view newModulePath, uint64 newGeneration) noexcept
{
    if (Instance_ == nullptr || !Active_.IsLoaded())
    {
        return CommandStatus::InvalidRequest;
    }
    if (!game_api::HasCapability(Active_.Metadata().Capabilities, game_api::Capability::Reload))
    {
        return CommandStatus::RestartRequired;
    }

    const PlayState priorState = State_;
    State_ = PlayState::Reloading;

    // Phase 1 Validate: load B from a distinct immutable path and copy its
    // Query table. A keeps running on load/query failure (design 7).
    EmitReloadPhase(ReloadPhase::Validate);
    LoadedModule candidateModule;
    const LoadStatus loadStatus = LoadModule(newModulePath,
                                             newGeneration,
                                             CurrentHostIdentity(),
                                             CurrentAbiMajor(),
                                             CurrentAbiMinor(),
                                             candidateModule);
    if (loadStatus != LoadStatus::Ok)
    {
        State_ = priorState;
        EmitReloadPhase(ReloadPhase::Rejected);
        return loadStatus == LoadStatus::IdentityRejected ? CommandStatus::IncompatibleModule
                                                          : CommandStatus::ReloadRejected;
    }
    if (!game_api::HasCapability(candidateModule.Metadata().Capabilities, game_api::Capability::Reload))
    {
        State_ = priorState;
        EmitReloadPhase(ReloadPhase::Rejected);
        return CommandStatus::RestartRequired;
    }

    // Phase 2 Quiesce: gate dispatch and pause A. Record prior pause state.
    EmitReloadPhase(ReloadPhase::Quiesce);
    const bool wasPaused = (priorState == PlayState::Paused);
    if (Active_.Table().Quiesce(Instance_) != Status::Ok)
    {
        State_ = priorState;
        EmitReloadPhase(ReloadPhase::Rejected);
        return CommandStatus::ReloadRejected;
    }

    // Phase 3 Snapshot: write A's read-only checkpoint into bounded host storage.
    EmitReloadPhase(ReloadPhase::Snapshot);
    usize bodySize = 0;
    if (Active_.Table().CheckpointSize == nullptr ||
        Active_.Table().CheckpointSize(Instance_, &bodySize) != Status::Ok || bodySize > kMaxCheckpointBody)
    {
        (void)Active_.Table().Resume(Instance_);
        State_ = priorState;
        EmitReloadPhase(ReloadPhase::Rejected);
        return CommandStatus::ReloadRejected;
    }
    std::vector<ludus::foundation::uint8> body(bodySize);
    CheckpointHeader header = {};
    ByteSpan span = {body.data(), body.size()};
    usize written = 0;
    if (Active_.Table().WriteCheckpoint(Instance_, &header, span, &written) != Status::Ok || written != bodySize)
    {
        (void)Active_.Table().Resume(Instance_);
        State_ = priorState;
        EmitReloadPhase(ReloadPhase::Rejected);
        return CommandStatus::ReloadRejected;
    }

    // Phase 4 Stage: create and validate B from the checkpoint without mutating
    // active state. B is created paused.
    EmitReloadPhase(ReloadPhase::Stage);
    CreateInfo info = {};
    info.StructSize = static_cast<uint32>(sizeof(CreateInfo));
    info.Services = &Services_.Services();
    info.ProjectId = ProjectId_;
    info.GameId = GameId_;
    info.ModuleGeneration = newGeneration;
    GameCandidate* candidate = nullptr;
    ByteView bodyView = {body.data(), body.size()};
    const Status createStatus = candidateModule.Table().CreateCandidate(&info, &header, bodyView, &candidate);
    if (createStatus != Status::Ok || candidate == nullptr)
    {
        (void)Active_.Table().Resume(Instance_);
        State_ = priorState;
        EmitReloadPhase(ReloadPhase::Rejected);
        return createStatus == Status::MigrationUnsupported ? CommandStatus::RestartRequired
                                                            : CommandStatus::ReloadRejected;
    }
    if (candidateModule.Table().ValidateCandidate(candidate) != Status::Ok)
    {
        candidateModule.Table().DiscardCandidate(candidate);
        (void)Active_.Table().Resume(Instance_);
        State_ = priorState;
        EmitReloadPhase(ReloadPhase::Rejected);
        return CommandStatus::ReloadRejected;
    }

    // Phase 5 Commit: promote the candidate (non-failing swap). The old instance
    // A is retained until retire below.
    EmitReloadPhase(ReloadPhase::Commit);
    GameInstance* newInstance = nullptr;
    if (candidateModule.Table().CommitCandidate(candidate, &newInstance) != Status::Ok || newInstance == nullptr)
    {
        candidateModule.Table().DiscardCandidate(candidate);
        (void)Active_.Table().Resume(Instance_);
        State_ = priorState;
        EmitReloadPhase(ReloadPhase::Rejected);
        return CommandStatus::ReloadRejected;
    }

    // Phase 6 Retire: destroy A with its own code, release A's loader reference,
    // and swap in B. Resume B in the prior pause state.
    EmitReloadPhase(ReloadPhase::Retire);
    GameInstance* oldInstance = Instance_;
    LoadedModule oldModule = std::move(Active_);

    Instance_ = newInstance;
    Active_ = std::move(candidateModule);
    ActiveGeneration_ = newGeneration;
    ++SchemaEpoch_;

    oldModule.Table().Destroy(oldInstance);
    oldModule.Close();

    if (wasPaused)
    {
        State_ = PlayState::Paused;
    }
    else
    {
        (void)Active_.Table().Resume(Instance_);
        State_ = PlayState::Running;
    }

    EmitReloadPhase(ReloadPhase::Done);
    return CommandStatus::Ok;
}

RunResult RunStaticEntry(const HostConfig& config, StaticEntryFn entry) noexcept
{
    // Fill the table in-process (static dispatch replaces only the module
    // lookup; design 2). No identity check is needed — the implementation is
    // compiled into this binary against this SDK.
    game_api::GameApiTable table = {};
    table.StructSize = static_cast<uint32>(sizeof(game_api::GameApiTable));
    table.AbiMajor = CurrentAbiMajor();
    table.AbiMinor = CurrentAbiMinor();
    const int fillStatus = entry(CurrentAbiMajor(), CurrentAbiMinor(), &table);
    if (fillStatus != 0 || table.Create == nullptr || table.Update == nullptr || table.Destroy == nullptr)
    {
        return RunResult::IncompatibleModule;
    }

    HostServiceProvider services;
    CreateInfo info = {};
    info.StructSize = static_cast<uint32>(sizeof(CreateInfo));
    info.Services = &services.Services();
    info.ProjectId = config.ProjectId;
    info.GameId = config.GameId;
    info.ModuleGeneration = 1;
    GameInstance* instance = nullptr;
    if (table.Create(&info, &instance) != Status::Ok || instance == nullptr)
    {
        return RunResult::Internal;
    }

    const uint64 frames = config.MaxFrames == 0 ? 1 : config.MaxFrames;
    for (uint64 i = 0; i < frames; ++i)
    {
        FrameInput input = {};
        input.FrameIndex = i;
        input.DeltaSeconds = 1.0 / 60.0;
        input.ElapsedSeconds = static_cast<ludus::foundation::float64>(i) / 60.0;
        input.Width = 800;
        input.Height = 600;
        RenderParams render = {};
        if (table.Update(instance, &input, &render) != Status::Ok)
        {
            table.Destroy(instance);
            return RunResult::Internal;
        }
    }
    table.Destroy(instance);
    return services.OutstandingAllocations() == 0 ? RunResult::Ok : RunResult::Internal;
}

usize HostSession::ReadActiveProperties(ludus::foundation::uint8* buffer, usize capacity) noexcept
{
    if (Instance_ == nullptr || !Active_.IsLoaded() || Active_.Table().ReadProperties == nullptr || buffer == nullptr)
    {
        return 0;
    }
    ByteSpan span = {buffer, capacity};
    usize written = 0;
    if (Active_.Table().ReadProperties(Instance_, span, &written) != Status::Ok)
    {
        return 0;
    }
    return written;
}

RunResult HostSession::RunLoop(uint64 maxFrames) noexcept
{
    if (State_ != PlayState::Running && State_ != PlayState::Paused)
    {
        return RunResult::Internal;
    }
    for (;;)
    {
        if (!PumpControl())
        {
            break;
        }
        StepFrame();
        if (maxFrames != 0 && FrameIndex_ >= maxFrames)
        {
            break;
        }
    }

    // Stop: retire the instance before releasing the loader reference.
    if (Instance_ != nullptr && Active_.IsLoaded())
    {
        Active_.Table().Destroy(Instance_);
        Instance_ = nullptr;
    }
    if (Services_.OutstandingAllocations() != 0)
    {
        LUDUS_LOG_ERROR(LOG_SESSION, "module leaked {} host allocations", Services_.OutstandingAllocations());
        State_ = PlayState::CleanupUnknown;
    }
    Active_.Close();

    {
        Message ended;
        ended.SetString("event", protocol::EventKindName(EventKind::SessionEnded));
        ended.SetUint("protocol", protocol::kProtocolVersion);
        ended.SetHexId("session", SessionId_);
        ended.SetString("state", PlayStateName(State_));
        Emit(ended);
    }

    if (State_ == PlayState::CleanupUnknown)
    {
        return RunResult::Internal;
    }
    State_ = PlayState::Stopped;
    return RunResult::Ok;
}
} // namespace ludus::runtime::game_host
