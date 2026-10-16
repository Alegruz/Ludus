#pragma once

#include <ludus/foundation/base/types.h>

#include "internal/controller.h"
#include "internal/render_graph_document.h"
#include "internal/render_graph_job.h"

#include <QJsonObject>
#include <QString>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QTableWidget;
class QTreeWidget;
QT_END_NAMESPACE

namespace ludus::editor
{
// RG3 Renderer workspace: edits one render-graph package through the typed
// RenderGraphDocument, and runs validation, builds and runtime cooking through
// the shared `ludus render-graph` backend on explicit immutable draft snapshots.
// Saved sources change only on Save. Diagnostics map to authored nodes and source
// lines. The visual node canvas and live in-editor GPU preview are RG3b work.
// See docs/development/render-graph-authoring.md#edit-and-reload-graphs-rg3.
class RendererWorkspace final : public QWidget
{
    Q_OBJECT
public:
    explicit RendererWorkspace(const ToolingPaths& tooling, QWidget* parentOrNull = nullptr);
    ~RendererWorkspace() override;
    RendererWorkspace(const RendererWorkspace&) = delete;
    RendererWorkspace& operator=(const RendererWorkspace&) = delete;

    /// Bind a project root and epoch; a different root closes the document and cancels jobs.
    void SetProject(const QString& root, foundation::uint64 epoch);
    /// Open a root .lurender.json inside the project; its directory is the package root.
    [[nodiscard]] bool OpenGraph(const QString& path);
    [[nodiscard]] bool ConfirmDiscard();
    [[nodiscard]] bool IsDirty() const;
    [[nodiscard]] bool CanSave() const;
    [[nodiscard]] bool Save();
    [[nodiscard]] bool CanUndo() const;
    [[nodiscard]] bool CanRedo() const;
    void Undo();
    void Redo();

    /// Validate and dump the current draft snapshot. False when a job is active or nothing is open.
    [[nodiscard]] bool Validate();
    /// Build the draft snapshot's sibling .lushader.json into the project's render-graph build tree.
    [[nodiscard]] bool Build();
    /// Cook the last published build into a runtime generation for atomic activation.
    [[nodiscard]] bool CookRuntime();
    void CancelJob();
    [[nodiscard]] bool IsJobActive() const;
    void SetBuildTools(const QString& helper, const QString& compiler);
    /// Publish a finished job result. Results for another request, project epoch or a
    /// closed document are ignored, so stale jobs never replace current diagnostics.
    void AcceptResult(const RenderGraphJobResult& result);

    [[nodiscard]] RenderGraphDocument& GetDocument() noexcept
    {
        return mDocument;
    }
    [[nodiscard]] const RenderGraphJobResult& GetLastResult() const noexcept
    {
        return mLastResult;
    }
    [[nodiscard]] bool SelectNode(const QString& file, const QString& configuration, const QString& node);
    /// Selected node as `file#configuration/node`, or empty.
    [[nodiscard]] QString GetSelection() const;
    [[nodiscard]] foundation::int32 GetDiagnosticCount() const;
    /// Navigate to a diagnostic's authored node or source line.
    [[nodiscard]] bool ActivateDiagnostic(foundation::int32 row);
    [[nodiscard]] QString GetSourceFile() const;
    [[nodiscard]] foundation::int32 GetSourceLine() const;
    [[nodiscard]] QString GetRuntimePackagePath() const;
    [[nodiscard]] QString GetStatusText() const;

Q_SIGNALS:
    void DocumentChanged();
    void JobFinished();

private:
    [[nodiscard]] bool startJob(const QString& action, const QStringList& arguments, bool needsSnapshot);
    void renderTree();
    void renderInspector();
    void renderEdges();
    void renderSource();
    void renderStatus();
    void documentEdited(RenderGraphDocumentStatus status);
    void replaceSelectedField(const QString& field, const QJsonValue& value);
    void addNode();
    void openSourceLine(const QString& file, foundation::int32 line);
    [[nodiscard]] QJsonObject selectedConfiguration() const;
    [[nodiscard]] QJsonObject selectedNode() const;
    [[nodiscard]] QString buildDirectory() const;
    [[nodiscard]] QString manifestFile() const;

    ToolingPaths mTooling;
    QString mProjectRoot;
    foundation::uint64 mEpoch = 0;
    foundation::uint64 mNextRequest = 0;
    foundation::uint64 mActiveRequest = 0;
    foundation::uint64 mValidatedRevision = 0;
    bool mIsValidated = false;
    bool mIsRendering = false;
    RenderGraphDocument mDocument;
    RenderGraphJob mJob;
    RenderGraphJobResult mLastResult;
    QString mSelectedFile;
    QString mSelectedConfiguration;
    QString mSelectedNode;
    QLabel* mPath;
    QComboBox* mConfiguration;
    QPushButton* mValidate;
    QPushButton* mBuild;
    QPushButton* mCook;
    QPushButton* mCancel;
    QTreeWidget* mTree;
    QTabWidget* mTabs;
    QWidget* mInspector;
    QFormLayout* mFields;
    QSpinBox* mLayoutX;
    QSpinBox* mLayoutY;
    QComboBox* mNewKind;
    QLineEdit* mNewIdentity;
    QTableWidget* mEdges;
    QLineEdit* mEdgeFrom;
    QLineEdit* mEdgeTo;
    QComboBox* mSourceFile;
    QPlainTextEdit* mSource;
    QPlainTextEdit* mPlan;
    QLineEdit* mHelper;
    QLineEdit* mCompiler;
    QLineEdit* mValidator;
    QLineEdit* mSpirvCross;
    QListWidget* mDiagnostics;
    QLabel* mStatus;
};
} // namespace ludus::editor
