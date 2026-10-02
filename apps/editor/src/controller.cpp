#include "internal/controller.h"

#include <ludus/foundation/logging/category.hpp>
#include <ludus/foundation/logging/log.hpp>

#include <QDir>
#include <QFileInfo>

namespace ludus::editor
{
namespace
{
LUDUS_DEFINE_LOG_CATEGORY(LOG_EDITOR_CTRL, "Editor");

Phase PhaseFromStage(const QString& stage)
{
    if (stage == QStringLiteral("configuring"))
        return Phase::Configuring;
    if (stage == QStringLiteral("building"))
        return Phase::Building;
    if (stage == QStringLiteral("launching"))
        return Phase::Launching;
    if (stage == QStringLiteral("running"))
        return Phase::Running;
    if (stage == QStringLiteral("stopping"))
        return Phase::Stopping;
    return Phase::Starting;
}

const char* PhaseName(Phase phase)
{
    switch (phase)
    {
        case Phase::Idle:
            return "Idle";
        case Phase::Starting:
            return "Starting";
        case Phase::Configuring:
            return "Configuring";
        case Phase::Building:
            return "Building";
        case Phase::Launching:
            return "Launching";
        case Phase::Running:
            return "Running";
        case Phase::Stopping:
            return "Stopping";
        case Phase::CleanupUnknown:
            return "CleanupUnknown";
    }
    return "Idle";
}
} // namespace

EditorController::EditorController(ToolingPaths tooling, QObject* parent)
    : QObject(parent), Tooling_(std::move(tooling)), Tool_(this)
{
    // Bind tool events to this controller's lifetime; the queued connection
    // defers delivery to the next event-loop turn so a reentrant mutation from
    // inside a notification cannot corrupt state.
    connect(&Tool_, &ToolProcess::Event, this, &EditorController::OnToolEvent, Qt::QueuedConnection);
}

void EditorController::Publish()
{
    Q_EMIT StateChanged();
}

QString EditorController::ResolveDescriptorPath() const
{
    return State_.DescriptorPath;
}

void EditorController::OpenProject(const QString& descriptorPath)
{
    if (!ComputeCapabilities(State_).CanOpen)
    {
        return; // a failed attempt leaves the existing workspace intact
    }
    const QString absolute = QFileInfo(descriptorPath).absoluteFilePath();
    const LoadOutcome outcome = Store_.Load(absolute);
    if (!outcome.Ok())
    {
        // Invalid/missing file leaves the previous workspace unchanged and
        // records a useful error as the last result.
        LastResult result;
        result.Kind = Outcome::Failed;
        result.Stage = Phase::Idle;
        result.Code = outcome.Parse.Code;
        result.Message = outcome.Parse.Message;
        State_.Result = result;
        LUDUS_LOG_TEXT(LOG_EDITOR_CTRL, Warning, "open project failed");
        Publish();
        return;
    }

    State_.Document = DocumentState::ProjectLoaded;
    State_.DescriptorPath = absolute;
    State_.SavedDigest = outcome.Digest;
    State_.Saved = outcome.Parse.Descriptor;
    State_.Draft = outcome.Parse.Descriptor;
    State_.HasSaved = true;
    State_.ProjectEpoch += 1;
    State_.DiscoveredTargets.clear();
    State_.DiscoveredPreset.clear();
    State_.Result = LastResult{};
    LUDUS_LOG_TEXT(LOG_EDITOR_CTRL, Info, "opened project (no project code executed)");
    Publish();
}

void EditorController::EditDraft(const ProjectDescriptor& draft)
{
    if (!ComputeCapabilities(State_).CanEdit)
    {
        return;
    }
    if (State_.Draft == draft)
    {
        return;
    }
    State_.Draft = draft;
    // A preset change invalidates the target discovery cache.
    if (State_.DiscoveredPreset != draft.Preset)
    {
        State_.DiscoveredTargets.clear();
        State_.DiscoveredPreset.clear();
    }
    Publish();
}

void EditorController::Save()
{
    if (!ComputeCapabilities(State_).CanSave)
    {
        return;
    }
    const SaveOutcome outcome = Store_.Save(State_.DescriptorPath, State_.Draft, State_.SavedDigest);
    if (!outcome.Ok())
    {
        LastResult result;
        result.Kind = Outcome::Failed;
        result.Code = outcome.Code;
        result.Message = outcome.Message;
        State_.Result = result;
        LUDUS_LOG_TEXT(LOG_EDITOR_CTRL, Warning, "save failed; keeping dirty draft and previous saved file");
        Publish();
        return;
    }
    State_.Saved = State_.Draft;
    State_.SavedDigest = outcome.Digest;
    State_.HasSaved = true;
    State_.Result = LastResult{};
    LUDUS_LOG_TEXT(LOG_EDITOR_CTRL, Info, "saved project");
    Publish();
}

void EditorController::Reload()
{
    if (!ComputeCapabilities(State_).CanReload)
    {
        return;
    }
    const LoadOutcome outcome = Store_.Load(State_.DescriptorPath);
    if (!outcome.Ok())
    {
        LastResult result;
        result.Kind = Outcome::Failed;
        result.Code = outcome.Parse.Code;
        result.Message = outcome.Parse.Message;
        State_.Result = result;
        Publish();
        return;
    }
    State_.SavedDigest = outcome.Digest;
    State_.Saved = outcome.Parse.Descriptor;
    State_.Draft = outcome.Parse.Descriptor;
    State_.HasSaved = true;
    State_.ProjectEpoch += 1;
    State_.DiscoveredTargets.clear();
    State_.DiscoveredPreset.clear();
    Publish();
}

void EditorController::StartJob(ActionKind kind, ToolOperation operation)
{
    if (!CanStartJob(State_, kind))
    {
        return; // duplicate/invalid starts cannot create a second job
    }
    State_ = BeginJob(State_, kind);
    Commands_.clear();
    Transitions_.clear();
    LastPid_ = 0;
    Transitions_.append(QString::fromLatin1(PhaseName(State_.OperationPhase)));

    ToolLaunch launch;
    launch.PythonPath = Tooling_.PythonPath;
    launch.AdapterPath = Tooling_.AdapterPath;
    launch.ToolingRoot = Tooling_.ToolingRoot;
    launch.ProjectPath = State_.DescriptorPath;
    launch.ExpectedSha256 = State_.SavedDigest;
    launch.Operation = operation;
    launch.Job = State_.ActiveJob;

    if (!Tool_.Start(launch))
    {
        LastResult result;
        result.Kind = Outcome::Failed;
        result.Code = ResultCode::Busy;
        result.Message = QStringLiteral("a tool process is already active");
        State_ = ApplyResult(State_, State_.ActiveJob, result);
    }
    Publish();
}

void EditorController::Configure()
{
    StartJob(ActionKind::Configure, ToolOperation::Configure);
}
void EditorController::Build()
{
    StartJob(ActionKind::Build, ToolOperation::Build);
}
void EditorController::BuildRun()
{
    StartJob(ActionKind::BuildRun, ToolOperation::BuildRun);
}

void EditorController::Stop()
{
    if (State_.ActiveJob == 0)
    {
        return;
    }
    // Latch stop first so a late success cannot launch; the latched state wins
    // even before the adapter acknowledges.
    State_ = LatchStop(State_);
    Tool_.Cancel();
    Publish();
}

void EditorController::ClearOutput()
{
    Log_.Clear();
    Publish();
}

bool EditorController::RequestClose()
{
    if (ComputeCapabilities(State_).CanCloseImmediately)
    {
        return true;
    }
    // Busy: begin asynchronous cancellation; the window closes only after the
    // controller confirms cleanup (handled when the terminal result arrives).
    Stop();
    return false;
}

void EditorController::RecordCommand(const QString& stage, const QStringList& argv, const QString& cwd)
{
    if (Commands_.size() >= 8)
    {
        return; // bounded: retain at most eight command records per job
    }
    CommandRecord record;
    record.Stage = stage;
    record.Argv = argv;
    record.Cwd = cwd;
    Commands_.append(record);
}

void EditorController::OnToolEvent(const ProtocolEvent& event)
{
    // Ignore events for a retired/non-current job (stale callbacks).
    if (event.Job != State_.ActiveJob && event.Kind != ProtocolEvent::Type::Error)
    {
        return;
    }

    switch (event.Kind)
    {
        case ProtocolEvent::Type::Ready:
            break;
        case ProtocolEvent::Type::Phase: {
            const Phase before = State_.OperationPhase;
            State_ = ApplyPhaseEvent(State_, event.Job, PhaseFromStage(event.Stage), /*runtimeConfirmed=*/false);
            if (State_.OperationPhase != before && Transitions_.size() < 256)
            {
                Transitions_.append(QString::fromLatin1(PhaseName(State_.OperationPhase)));
            }
            break;
        }
        case ProtocolEvent::Type::Command:
            RecordCommand(event.Stage, event.Argv, event.Cwd);
            break;
        case ProtocolEvent::Type::Targets:
            // Target discovery never changes the document silently; it only updates
            // the discovery cache for the current preset.
            State_.DiscoveredTargets = event.Targets;
            State_.DiscoveredPreset = event.Preset;
            break;
        case ProtocolEvent::Type::Output:
            Log_.Append(event.Stream == ProtocolEvent::OutputStreamTag::Stderr ? OutputStream::Stderr
                                                                               : OutputStream::Stdout,
                        event.Text);
            break;
        case ProtocolEvent::Type::RuntimeStarted: {
            LastPid_ = event.Pid;
            const Phase before = State_.OperationPhase;
            State_ = ApplyPhaseEvent(State_, event.Job, Phase::Running, /*runtimeConfirmed=*/true);
            if (State_.OperationPhase != before && Transitions_.size() < 256)
            {
                Transitions_.append(QString::fromLatin1(PhaseName(State_.OperationPhase)));
            }
            break;
        }
        case ProtocolEvent::Type::Result: {
            LastResult result;
            result.Stage = State_.OperationPhase;
            if (event.Outcome == QStringLiteral("success"))
            {
                result.Kind = Outcome::Success;
            }
            else if (event.Outcome == QStringLiteral("cancelled"))
            {
                result.Kind = Outcome::Cancelled;
            }
            else
            {
                result.Kind = Outcome::Failed;
            }
            result.Code = event.Code;
            result.Message = event.Message;
            result.ExitCode = event.ExitCode;
            result.Signal = event.Signal;
            result.CleanupConfirmed = event.CleanupConfirmed;
            if (!event.CleanupConfirmed)
            {
                result.Kind = Outcome::CleanupUnknown;
            }
            State_ = ApplyResult(State_, event.Job, result);
            break;
        }
        case ProtocolEvent::Type::Error: {
            LastResult result;
            result.Stage = State_.OperationPhase;
            result.Kind = event.Code == ResultCode::CleanupUnknown ? Outcome::CleanupUnknown : Outcome::Failed;
            result.Code = event.Code;
            result.Message = event.Message;
            result.CleanupConfirmed = event.Code != ResultCode::CleanupUnknown;
            if (State_.ActiveJob != 0)
            {
                State_ = ApplyResult(State_, State_.ActiveJob, result);
            }
            else
            {
                State_.Result = result;
            }
            break;
        }
    }
    Publish();
}

QString EditorController::JobDetails() const
{
    // Bounded, telemetry-free record: descriptor digest, provider/preset/target,
    // argv/cwd per stage, job id, transitions, last exit/signal/result, cleanup
    // status. No environment dump or upload (design section 10).
    QString out;
    out += QStringLiteral("job: %1\n").arg(State_.ActiveJob, 16, 16, QLatin1Char('0'));
    out += QStringLiteral("descriptor_digest: %1\n").arg(State_.SavedDigest);
    out += QStringLiteral("provider: %1\n")
               .arg(State_.Saved.ProviderKind == Provider::Ludus ? QStringLiteral("ludus") : QStringLiteral("cmake"));
    out += QStringLiteral("preset: %1\n").arg(State_.Saved.Preset);
    out += QStringLiteral("target: %1\n").arg(State_.Saved.Target);
    out += QStringLiteral("transitions: %1\n").arg(Transitions_.join(QStringLiteral(" -> ")));
    for (const CommandRecord& record : Commands_)
    {
        out += QStringLiteral("command[%1] cwd=%2\n").arg(record.Stage, record.Cwd);
        for (const QString& arg : record.Argv)
        {
            out += QStringLiteral("  argv: %1\n").arg(arg);
        }
    }
    if (LastPid_ != 0)
    {
        out += QStringLiteral("runtime_pid: %1\n").arg(LastPid_);
    }
    out += QStringLiteral("last_result: %1 (%2)\n").arg(ResultCodeName(State_.Result.Code), State_.Result.Message);
    if (State_.Result.ExitCode.has_value())
    {
        out += QStringLiteral("exit_code: %1\n").arg(State_.Result.ExitCode.value());
    }
    if (State_.Result.Signal.has_value())
    {
        out += QStringLiteral("signal: %1\n").arg(State_.Result.Signal.value());
    }
    out += QStringLiteral("cleanup_confirmed: %1\n")
               .arg(State_.Result.CleanupConfirmed ? QStringLiteral("true") : QStringLiteral("false"));
    out += QStringLiteral("dropped_output_blocks: %1\n").arg(Log_.DroppedBlocks());
    out += QStringLiteral("dropped_output_bytes: %1\n").arg(Log_.DroppedBytes());
    // Bound the whole record to 1 MiB.
    if (static_cast<usize>(out.toUtf8().size()) > 1u * 1024u * 1024u)
    {
        out.truncate(1024 * 1024);
        out += QStringLiteral("\n[... job details truncated ...]\n");
    }
    return out;
}

} // namespace ludus::editor
