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
        return ComputeCapabilities(State_);
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
};

} // namespace ludus::editor
