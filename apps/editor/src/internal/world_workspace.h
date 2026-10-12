#pragma once

#include <ludus/foundation/base/types.h>

#include "internal/world_document.h"

#include <QMap>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QVBoxLayout;
QT_END_NAMESPACE
namespace ludus::editor
{
// Desktop, project-owned schema UI. No runtime pools or project code are loaded.
class RenderPanel;
class LevelWorkspace;
struct ToolingPaths;
class WorldWorkspace final : public QWidget
{
    Q_OBJECT
public:
    explicit WorldWorkspace(QWidget* parent = nullptr);
    WorldWorkspace(const ToolingPaths& tooling, QWidget* parent);
    [[nodiscard]] bool ImportScene(const QString& source);
    void SetProject(const QString& root, foundation::uint64 epoch);
    [[nodiscard]] bool Open(const QString& path);
    [[nodiscard]] bool Create(const QString& path);
    [[nodiscard]] bool Save();
    [[nodiscard]] bool ConfirmDiscard();
    [[nodiscard]] bool Dirty() const noexcept
    {
        return Pending_ || Unsaved_ || Document_.Dirty();
    }
    [[nodiscard]] bool CanSave() const noexcept
    {
        return Document_.Loaded() && !SceneBusy();
    }
    [[nodiscard]] bool CanUndo() const noexcept
    {
        return !Pending_ && !SceneBusy() && Document_.CanUndo();
    }
    [[nodiscard]] bool CanRedo() const noexcept
    {
        return !Pending_ && !SceneBusy() && Document_.CanRedo();
    }
    void StopPreview() noexcept;
    void Undo();
    void Redo();
    [[nodiscard]] bool Apply();
    [[nodiscard]] bool Recover();
    [[nodiscard]] const WorldDocument& Document() const noexcept
    {
        return Document_;
    }
Q_SIGNALS:
    void DocumentChanged();
    void PreviewStarting();

private:
    [[nodiscard]] bool LoadProjectSchema(WorldDocument& candidate, QByteArray& schema);
    [[nodiscard]] bool Inside(const QString& path, bool existing) const;
    [[nodiscard]] bool SceneBusy() const noexcept;
    void RefreshScene();
    void SceneCompleted(bool accepted);
    void SceneVerified(bool accepted);
    void Render();
    void RenderInspector();
    void Changed(const QString& message = {});
    void Pending();
    void PickTint(const QString& id, const QJsonObject& binding);
    void Snapshot();
    [[nodiscard]] QByteArray RecoveryHeader() const;
    WorldDocument Document_;
    RenderPanel* Render_ = nullptr;
    LevelWorkspace* Scene_ = nullptr;
    QWidget* Viewport_ = nullptr;
    QPushButton* Import_ = nullptr;
    QPushButton* CancelScene_ = nullptr;
    bool Importing_ = false;
    bool SavePending_ = false;
    bool SaveValidated_ = false;
    QJsonObject ValidatedScene_;
    QByteArray ValidatedWorld_;
    QString SceneError_;
    QString ConversionPath_;
    QByteArray ConversionSchema_;
    foundation::uint64 SceneGeneration_ = 0;

    QString Root_;
    QString Path_;
    QString Selected_;
    QByteArray Disk_;
    QByteArray SchemaDisk_;
    foundation::uint64 Epoch_ = ~foundation::uint64{0};
    foundation::uint64 DocumentGeneration_ = 0;
    bool Pending_ = false;
    bool Unsaved_ = false;
    bool Rendering_ = false;
    bool RecoveryFailed_ = false;
    QMap<QString, QWidget*> Fields_;
    QListWidget* Entities_;
    QLabel* Status_;
    QLabel* PathLabel_;
    QLineEdit* Name_ = nullptr;
    QComboBox* Components_;
    QPushButton* Attach_;
    QPushButton* Add_;
    QPushButton* Duplicate_;
    QPushButton* Remove_;
    QPushButton* Apply_;
    QPushButton* Reset_;
    QPushButton* Recovery_;
    QVBoxLayout* Inspector_;
};
} // namespace ludus::editor
