#pragma once

#include "internal/controller.h"
#include "internal/render_preview.h"

#include <QJsonObject>
#include <QTemporaryDir>
#include <QThread>
#include <QWidget>
#if !defined(Q_OS_WASM)
#    include <QProcess>
#endif

QT_BEGIN_NAMESPACE
class QLabel;
class QListWidget;
class QLineEdit;
class QDoubleSpinBox;
class QPushButton;
QT_END_NAMESPACE
namespace ludus::editor
{
// Project-owned 3D level surface. The installed host backend owns cooking,
// immutable catalogs and save conflicts; no project game code is executed.
class LevelWorkspace final : public QWidget
{
    Q_OBJECT
public:
    explicit LevelWorkspace(const ToolingPaths& tooling, QWidget* parent = nullptr);
    ~LevelWorkspace() override;
    void SetProject(const QString& root, foundation::uint64 epoch);
    [[nodiscard]] bool Create(const QString& path);
    [[nodiscard]] bool Open(const QString& path);
    [[nodiscard]] bool Import(const QString& source);
    [[nodiscard]] bool Save();
    [[nodiscard]] bool ConfirmDiscard();
    [[nodiscard]] bool Dirty() const noexcept
    {
        return Dirty_;
    }
    [[nodiscard]] bool CanSave() const noexcept
    {
        return !Busy_ && !Level_.isEmpty();
    }
    void StopPreview() noexcept;
    [[nodiscard]] QJsonObject Document() const
    {
        return Level_;
    }
Q_SIGNALS:
    void DocumentChanged();
    void PreviewStarting();

private:
    bool Start(const QString& operation, const QString& path = {}, const QString& source = {});
    void Completed();
    void Publish();
    void CommitCandidate();
    void CommitImport();
    void Rollback(const QString& error);
    void Render();
    void Inspect();
    void Edit();
    void Cancel();
    void ApplyPendingProject();
    QString PendingRoot_;
    foundation::uint64 PendingEpoch_ = 0;
    bool ProjectPending_ = false;
    bool Relative(const QString& path, QString& relative) const;
    ToolingPaths Tooling_;
    QString Root_, Path_, Operation_, CandidatePath_, Disk_, CandidateDisk_, PendingWarnings_;
    foundation::uint64 Epoch_ = ~foundation::uint64{0};
    QJsonObject Level_, Candidate_, CandidateRevisions_;
    QJsonValue CatalogExpected_;
    QString Source_;
    QImage CandidateFrame_;
    bool PublishingCatalog_ = false;
    QByteArray Output_;
    QTemporaryDir Scratch_;
    QThread Decode_;
#if !defined(Q_OS_WASM)
    QProcess Process_;
#endif
    RenderScene Scene_;
    RenderScene PreviousScene_;
    bool AwaitingViewport_ = false;
    QString Error_;
    RenderPreview Preview_;
    bool Busy_ = false, Dirty_ = false, Cancelling_ = false, Success_ = false, Rendering_ = false;
    QLabel *Status_, *Viewport_, *File_, *Warnings_;
    QListWidget* Objects_;
    QLineEdit* Name_;
    QDoubleSpinBox* Translation_[3];
    QPushButton *New_, *Open_, *Import_, *Save_, *Cancel_, *Remove_;
};
} // namespace ludus::editor
