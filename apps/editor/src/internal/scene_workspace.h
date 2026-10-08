#pragma once

#include <ludus/foundation/base/types.h>

#include "internal/scene_document.h"
#include "internal/scene_preview.h"
#include <QByteArray>
#include <QString>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
QT_END_NAMESPACE
namespace ludus::editor
{
class SceneWorkspace final : public QWidget
{
    Q_OBJECT
public:
    explicit SceneWorkspace(QWidget* parent = nullptr);
    void SetProject(const QString& root, foundation::uint64 epoch);
    [[nodiscard]] bool Open(const QString& path);
    [[nodiscard]] bool Save();
    [[nodiscard]] bool ConfirmDiscard();
    [[nodiscard]] bool Dirty() const noexcept;
    [[nodiscard]] bool CanSave() const noexcept
    {
        return Document_.Loaded();
    }
    [[nodiscard]] bool CanUndo() const noexcept
    {
        return !Pending_ && Document_.CanUndo();
    }
    [[nodiscard]] bool CanRedo() const noexcept
    {
        return !Pending_ && Document_.CanRedo();
    }
    void Undo();
    void Redo();
    void StopPreview() noexcept
    {
        Preview_.Stop();
    }
    [[nodiscard]] QString Path() const
    {
        return Path_;
    }
    [[nodiscard]] SceneDocument& Document() noexcept
    {
        return Document_;
    }
    [[nodiscard]] ScenePreview& Preview() noexcept
    {
        return Preview_;
    }
Q_SIGNALS:
    void DocumentChanged();
    void PlayRequested();

private:
    void Render();
    void Changed();
    [[nodiscard]] bool Apply();
    void Place();
    void Recover();
    SceneDocument Document_;
    ScenePreview Preview_;
    QString Root_;
    QString Path_;
    QByteArray Disk_;
    foundation::uint64 Epoch_ = ~foundation::uint64{0};
    world_demo::Name PendingTarget_{};
    SceneStamp PendingStamp_{};
    bool Pending_ = false;
    bool OwnRecovery_ = false;
    foundation::uint64 RecoveredRevision_ = ~foundation::uint64{0};
    bool Rendering_ = false;
    QListWidget* Hierarchy_;
    QLineEdit* Fields_[5]{};
    QLabel* Status_;
    QLabel* PathLabel_;
    QPushButton* PreviewButton_;
    QPushButton* RecoveryButton_;
};
} // namespace ludus::editor
