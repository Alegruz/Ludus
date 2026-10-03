#pragma once

// EditorController: validates actions, advances WorkspaceState, executes effects
// (project load/save, tool start/cancel) and publishes exactly one state-change
// notification per step (design.md sections 3, 5, 6, 9).
//
// Private editor header (not installed). The controller is the only owner of
// WorkspaceState, ProjectStore, ToolProcess and LogBuffer; MainWindow renders
// controller state and emits actions. There is no global event bus or singleton.

#include "internal/log_buffer.h"
#include "internal/project_store.h"
#include "internal/tool_process.h"
#include "internal/workspace.h"

#include <ludus/foundation/base/types.h>

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <optional>

namespace ludus::editor
{

// Where to find the trusted tooling adapter/interpreter (resolved by
// scripts/editor and passed to the application). The controller never imports a
// Python adapter named in the descriptor.
struct ToolingPaths
{
    QString PythonPath;  // absolute managed interpreter
    QString AdapterPath; // absolute editor_tool.py
    QString ToolingRoot; // absolute tooling checkout
};

struct ProjectCreationOptions
{
    QString Destination;
    QString Name;
    QString Sdk;
    bool PrepareEngine = false;
};

// One recorded command invocation for Copy Job Details (bounded).
struct CommandRecord
{
    QString Stage;
    QStringList Argv;
    QString Cwd;
};

class EditorController : public QObject
{
    Q_OBJECT
public:
    explicit EditorController(ToolingPaths tooling, QObject* parent = nullptr);

    [[nodiscard]] const WorkspaceState& State() const noexcept
    {
        return State_;
    }
    [[nodiscard]] const LogBuffer& Log() const noexcept
    {
        return Log_;
    }
    [[nodiscard]] Capabilities Caps() const
    {
        Capabilities caps = ComputeCapabilities(State_);
        if (Tool_.Active() && !State_.Busy())
        {
            // Keep the workspace owned until the bridge exits and drains.
            caps.CanOpen = caps.CanEdit = caps.CanSave = caps.CanReload = false;
            caps.CanConfigure = caps.CanBuild = caps.CanBuildRun = false;
            caps.CanProjectCheck = caps.CanProjectSetup = caps.CanProjectCreate = false;
            caps.CanReleaseInit = caps.CanPackage = caps.CanCloseImmediately = false;
        }
        return caps;
    }

    // Expose the store so tests can inject fault hooks.
    [[nodiscard]] ProjectStore& Store() noexcept
    {
        return Store_;
    }

    // Actions (validated centrally). Each applies at most one transition and
    // emits StateChanged exactly once when it changes state.
    void OpenProject(const QString& descriptorPath);
    void EditDraft(const ProjectDescriptor& draft);
    void Save();
    void Reload();
    void Configure();
    void Build();
    void BuildRun();
    void CheckProjectSetup();
    void SetupProject(const QString& sdk, const QString& webSdk, bool prepareEngine);
    void CreateProject(const ProjectCreationOptions& creation);
    void SetupRelease(const QString& platform, const QString& itchTarget);
    void PackageRelease(const QString& profile, const QString& version, const QString& sdk);
    void Stop();
    void ClearOutput();

    // A bounded text record for Copy Job Details (design section 10).
    [[nodiscard]] QString JobDetails() const;

    // Request to close. Returns true if the application may close immediately;
    // false if a job is active (caller must offer Stop and Close).
    [[nodiscard]] bool RequestClose();

Q_SIGNALS:
    // Published after any state/log change. Rendering reads State()/Log(); it
    // must not trigger new edit actions (reentrancy is deferred by the window).
    void StateChanged();

private Q_SLOTS:
    void OnToolEvent(const ProtocolEvent& event);

private:
    void StartJob(ActionKind kind, ToolOperation operation, const ToolLaunch& options = {});
    void ScheduleSetupCheck();
    void Publish();
    void RecordCommand(const QString& stage, const QStringList& argv, const QString& cwd);
    [[nodiscard]] QString ResolveDescriptorPath() const;

    ToolingPaths Tooling_;
    WorkspaceState State_;
    ProjectStore Store_;
    LogBuffer Log_;
    ToolProcess Tool_;

    // Bounded Copy Job Details storage for the current/last job.
    QList<CommandRecord> Commands_;
    QStringList Transitions_;
    qint64 LastPid_ = 0;
    QString PendingCreatedProject_;
};

} // namespace ludus::editor
