#include "internal/workspace.h"

#include "internal/project_descriptor.h"

namespace ludus::editor
{

const char* ResultCodeName(ResultCode code) noexcept
{
    switch (code)
    {
        case ResultCode::Ok:
            return "Ok";
        case ResultCode::InvalidProject:
            return "InvalidProject";
        case ResultCode::UnsupportedVersion:
            return "UnsupportedVersion";
        case ResultCode::Conflict:
            return "Conflict";
        case ResultCode::MissingTools:
            return "MissingTools";
        case ResultCode::BootstrapStale:
            return "BootstrapStale";
        case ResultCode::Busy:
            return "Busy";
        case ResultCode::ConfigureFailed:
            return "ConfigureFailed";
        case ResultCode::TargetInvalid:
            return "TargetInvalid";
        case ResultCode::ReplyInvalid:
            return "ReplyInvalid";
        case ResultCode::BuildFailed:
            return "BuildFailed";
        case ResultCode::ArtifactInvalid:
            return "ArtifactInvalid";
        case ResultCode::SpawnFailed:
            return "SpawnFailed";
        case ResultCode::RuntimeFailed:
            return "RuntimeFailed";
        case ResultCode::RuntimeSignaled:
            return "RuntimeSignaled";
        case ResultCode::ProtocolError:
            return "ProtocolError";
        case ResultCode::Cancelled:
            return "Cancelled";
        case ResultCode::CleanupUnknown:
            return "CleanupUnknown";
    }
    return "Unknown";
}

Capabilities ComputeCapabilities(const WorkspaceState& state)
{
    Capabilities caps;
    const bool loaded = state.Document == DocumentState::ProjectLoaded;
    const bool busy = state.Busy();
    const bool stopping = state.OperationPhase == Phase::Stopping;
    const bool cleanupUnknown = state.OperationPhase == Phase::CleanupUnknown;

    // Open is disabled during a job and while cleanup is unknown: a destructive
    // document switch cannot happen while an operation owns the workspace.
    caps.CanOpen = !busy && !stopping && !cleanupUnknown;
    caps.CanEdit = loaded && !busy && !stopping && !cleanupUnknown;
    caps.CanSave = loaded && state.Dirty() && !busy && !stopping && !cleanupUnknown;
    caps.CanReload = loaded && !busy && !stopping && !cleanupUnknown;

    caps.CanConfigure = CanStartJob(state, ActionKind::Configure);
    caps.CanBuild = CanStartJob(state, ActionKind::Build);
    caps.CanBuildRun = CanStartJob(state, ActionKind::BuildRun);

    // Stop is available whenever a job is active or stopping (idempotent).
    caps.CanStop = busy || stopping;
    // Output selection/copy stay available during a job.
    caps.CanClearOutput = true;
    caps.CanCopyJobDetails = state.ActiveJob != 0 || state.Result.Kind != Outcome::None;
    // While busy, Close requires explicit Stop and Close first.
    caps.CanCloseImmediately = !busy && !stopping;
    return caps;
}

bool CanStartJob(const WorkspaceState& state, ActionKind kind)
{
    // Exactly one owned operation; nothing starts unless Idle. CleanupUnknown is
    // not Idle: it forbids a new operation until operator recovery.
    if (state.OperationPhase != Phase::Idle)
    {
        return false;
    }
    if (state.Document != DocumentState::ProjectLoaded)
    {
        return false;
    }
    switch (kind)
    {
        case ActionKind::Configure:
            return true;
        case ActionKind::Build:
        case ActionKind::BuildRun:
            // Build/BuildRun require a clean saved document; unsaved edits must be
            // saved first (design section 5).
            return !state.Dirty();
        default:
            return false;
    }
}

Phase StartingPhaseFor(ActionKind /*kind*/)
{
    // Every job begins in Starting; the adapter must report ready before any
    // child spawn (design sections 6, 8).
    return Phase::Starting;
}

WorkspaceState BeginJob(const WorkspaceState& state, ActionKind kind)
{
    WorkspaceState next = state;
    next.ActiveJob = next.NextJob;
    // Counter never wraps silently; a wrap would be a programming error long
    // before 2^64 jobs, but we still advance explicitly.
    next.NextJob = next.NextJob + 1;
    next.OperationPhase = StartingPhaseFor(kind);
    next.StopLatched = false;
    next.Result = LastResult{}; // live result cleared while the new job runs
    return next;
}

namespace
{
// Allowed forward phase transitions within one job. Running is reached only via
// RuntimeStarted; this table never allows a bare phase event to assert Running.
bool AllowedForward(const WorkspaceState& state, Phase requested)
{
    switch (state.OperationPhase)
    {
        case Phase::Starting:
            return requested == Phase::Configuring;
        case Phase::Configuring:
            return requested == Phase::Building || requested == Phase::Launching;
        case Phase::Building:
            return requested == Phase::Launching;
        case Phase::Launching:
            // Launching -> Running is handled only by RuntimeStarted, not here.
        default:
            return false;
    }
}
} // namespace

WorkspaceState ApplyPhaseEvent(const WorkspaceState& state, uint64 jobId, Phase requested, bool runtimeConfirmed)
{
    // Ignore events for a retired/non-current job.
    if (jobId == 0 || jobId != state.ActiveJob)
    {
        return state;
    }
    // Once Stop is latched, never move back toward Running or launch a child.
    if (state.StopLatched && requested != Phase::Stopping)
    {
        return state;
    }
    if (state.OperationPhase == Phase::Stopping || state.OperationPhase == Phase::CleanupUnknown)
    {
        // Terminal/cancelling phases ignore further forward progress.
        if (requested != Phase::Stopping)
        {
            return state;
        }
    }

    WorkspaceState next = state;
    if (requested == Phase::Stopping)
    {
        next.OperationPhase = Phase::Stopping;
        return next;
    }

    if (runtimeConfirmed)
    {
        // Only a runtime-started confirmation moves Launching -> Running.
        if (state.OperationPhase != Phase::Launching && state.OperationPhase != Phase::Building &&
            state.OperationPhase != Phase::Configuring && state.OperationPhase != Phase::Starting)
        {
            return state;
        }
        if (state.StopLatched)
        {
            // A late runtime confirmation after Stop must not launch.
            return state;
        }
        next.OperationPhase = Phase::Running;
        return next;
    }

    if (!AllowedForward(state, requested))
    {
        // Reject unexpected/duplicate/backward phase for the current job.
        return state;
    }
    next.OperationPhase = requested;
    return next;
}

WorkspaceState LatchStop(const WorkspaceState& state)
{
    if (state.ActiveJob == 0)
    {
        return state;
    }
    WorkspaceState next = state;
    next.StopLatched = true;
    if (next.OperationPhase != Phase::CleanupUnknown)
    {
        next.OperationPhase = Phase::Stopping;
    }
    return next;
}

WorkspaceState ApplyResult(const WorkspaceState& state, uint64 jobId, const LastResult& result)
{
    // A terminal result for a non-current job, or a duplicate for a job that
    // already produced its terminal result, is ignored.
    if (jobId == 0 || jobId != state.ActiveJob)
    {
        return state;
    }
    WorkspaceState next = state;
    next.ActiveJob = 0;
    next.StopLatched = false;
    next.Result = result;
    // Cleanup uncertainty outranks everything: it is not success and forbids a
    // new operation until operator recovery.
    if (!result.CleanupConfirmed || result.Kind == Outcome::CleanupUnknown)
    {
        next.OperationPhase = Phase::CleanupUnknown;
    }
    else
    {
        next.OperationPhase = Phase::Idle;
    }
    return next;
}

} // namespace ludus::editor
