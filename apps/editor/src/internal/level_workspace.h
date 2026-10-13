#pragma once

#include "internal/controller.h"
#include "internal/render_preview.h"
#include "internal/world_document.h"

#include <QByteArray>
#include <QJsonObject>
#include <QThread>
#include <QWidget>

#include <atomic>

QT_BEGIN_NAMESPACE
class QLabel;
class QListWidget;
class QLineEdit;
class QDoubleSpinBox;
class QPushButton;
QT_END_NAMESPACE
namespace ludus::editor
{
// Project-owned 3D level surface. Ludus::SceneCook owns cooking, immutable
// catalogs and save conflicts; it runs in process on a worker thread and no
// project game code is executed.
class PreviewViewport;
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
    void ResumePreview()
    {
        Q_EMIT PreviewStarting();
        (void)Preview_.Start();
    }
    [[nodiscard]] QJsonObject Navigation() const
    {
        return Preview_.Navigation();
    }
    void RestoreNavigation(const QJsonObject& state)
    {
        Preview_.RestoreNavigation(state);
    }
    [[nodiscard]] bool CanCancel() const noexcept
    {
        return Busy_ && Operation_ != QStringLiteral("save") && !PublishingCatalog_;
    }
    void CancelOperation()
    {
        Cancel();
    }
    [[nodiscard]] bool Busy() const noexcept
    {
        return Busy_;
    }
    [[nodiscard]] QWidget* TakeViewport(QWidget* parent);
    void SetWorldSnapshot(const WorldDocument& world)
    {
        WorldSnapshot_ = world;
    }
    [[nodiscard]] bool VerifyAssets(const QJsonObject& level);
    [[nodiscard]] bool Assemble(const QJsonObject& level);
    [[nodiscard]] bool ImportInto(const QJsonObject& level, const QString& source);
    [[nodiscard]] QString StatusText() const;
    [[nodiscard]] bool PreviewRunning() const noexcept
    {
        return Preview_.Running();
    }
    [[nodiscard]] const RenderScene& PreviewScene() const noexcept
    {
        return Preview_.Scene();
    }

    [[nodiscard]] QJsonObject Document() const
    {
        return Level_;
    }
Q_SIGNALS:
    void DocumentChanged();
    void PreviewStarting();
    void OperationFinished(bool accepted);
    void AssetsVerified(bool accepted);

private:
    bool Start(const QString& operation, const QString& path = {}, const QString& source = {});
    void Completed();
    bool Run(const QString& operation, const QString& path, const QString& source, const QJsonValue& expected);
    void WaitForOperation();
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
    QByteArray Response_, SceneBytes_, CandidateCatalog_;
    QString OperationError_;
    QThread* Operate_ = nullptr;
    std::atomic<bool> CancelRequested_{false};
    QThread Decode_;
    WorldDocument WorldSnapshot_;
    RenderScene Scene_;
    RenderScene PreviousScene_;
    bool AwaitingViewport_ = false;
    QString Error_;
    RenderPreview Preview_;
    bool WorldOwned_ = false;
    bool VerificationOnly_ = false;
    bool Busy_ = false, Dirty_ = false, Cancelling_ = false, Success_ = false, Rendering_ = false;
    QLabel *Status_, *File_, *Warnings_;
    PreviewViewport* Viewport_;
    QListWidget* Objects_;
    QLineEdit* Name_;
    QDoubleSpinBox* Translation_[3];
    QPushButton *New_, *Open_, *Import_, *Save_, *Cancel_, *Remove_;
};
} // namespace ludus::editor
