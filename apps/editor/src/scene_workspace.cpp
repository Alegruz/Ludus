#include "internal/scene_workspace.h"
#include "internal/scene_store.h"

#include <ludus/foundation/base/parse_number.hpp>

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace ludus::editor
{
namespace
{
QString Text(const world_demo::Name& name)
{
    return QString::fromLatin1(name.Text);
}
world_demo::Name Name(const QString& text)
{
    world_demo::Name name;
    const auto bytes = text.toLatin1();
    if (bytes.size() < 32)
    {
        for (foundation::int64 i = 0; i < bytes.size(); ++i)
        {
            name.Text[i] = bytes[i];
        }
    }
    return name;
}
} // namespace
SceneWorkspace::SceneWorkspace(QWidget* parent) : QWidget(parent), Preview_(&Document_, this)
{
    setObjectName(QStringLiteral("sceneWorkspace"));
    auto* layout = new QVBoxLayout(this);
    auto* actions = new QHBoxLayout;
    struct SceneButton
    {
        QString Text;
        QString Name;
    };
    auto button = [this, actions](const SceneButton& description, auto callback) {
        auto* item = new QPushButton(description.Text, this);
        item->setObjectName(description.Name);
        actions->addWidget(item);
        connect(item, &QPushButton::clicked, this, callback);
        return item;
    };
    button({QStringLiteral("Open scene…"), QStringLiteral("scene.open")}, [this]() {
        const auto path = QFileDialog::getOpenFileName(this,
                                                       QStringLiteral("Open world-demo scene"),
                                                       Root_,
                                                       QStringLiteral("Level JSON (*.json)"));
        if (!path.isEmpty() && ConfirmDiscard())
        {
            (void)Open(path);
        }
    });
    button({QStringLiteral("Save scene"), QStringLiteral("scene.save")}, [this]() { (void)Save(); });
    button({QStringLiteral("Undo"), QStringLiteral("scene.undo")}, [this]() { Undo(); });
    button({QStringLiteral("Redo"), QStringLiteral("scene.redo")}, [this]() { Redo(); });
    PreviewButton_ = button({QStringLiteral("Open rendered preview"), QStringLiteral("scene.preview")}, [this]() {
        if (Apply())
        {
            (void)Preview_.Start();
        }
    });
    button({QStringLiteral("Play saved scene"), QStringLiteral("scene.play")}, [this]() {
        if (Save())
        {
            Preview_.Stop();
            Q_EMIT PlayRequested();
        }
    });
    layout->addLayout(actions);
    PathLabel_ = new QLabel(this);
    PathLabel_->setWordWrap(true);
    layout->addWidget(PathLabel_);
    auto* body = new QHBoxLayout;
    Hierarchy_ = new QListWidget(this);
    Hierarchy_->setObjectName(QStringLiteral("sceneHierarchy"));
    Hierarchy_->setAccessibleName(QStringLiteral("Scene objects"));
    body->addWidget(Hierarchy_);
    auto* inspector = new QFormLayout;
    const QString labels[] = {QStringLiteral("Position X (metres)"),
                              QStringLiteral("Position Y (metres)"),
                              QStringLiteral("Rotation (radians)"),
                              QStringLiteral("Scale X"),
                              QStringLiteral("Scale Y")};
    for (foundation::usize i = 0; i < 5; ++i)
    {
        Fields_[i] = new QLineEdit(this);
        Fields_[i]->setObjectName(QStringLiteral("sceneTransform%1").arg(i));
        Fields_[i]->setAccessibleName(labels[i]);
        inspector->addRow(labels[i], Fields_[i]);
        connect(Fields_[i], &QLineEdit::textEdited, this, [this]() {
            if (!Rendering_)
            {
                if (!Pending_)
                {
                    PendingTarget_ = Preview_.Selected();
                    PendingStamp_ = Document_.Stamp(0, 0);
                }
                Pending_ = true;
                Q_EMIT DocumentChanged();
            }
        });
    }
    auto* apply = new QPushButton(QStringLiteral("Apply transform"), this);
    apply->setObjectName(QStringLiteral("scene.apply"));
    inspector->addRow(apply);
    connect(apply, &QPushButton::clicked, this, [this]() { (void)Apply(); });
    auto* reset = new QPushButton(QStringLiteral("Reset pending fields"), this);
    reset->setObjectName(QStringLiteral("scene.resetBuffer"));
    inspector->addRow(reset);
    connect(reset, &QPushButton::clicked, this, [this]() {
        Pending_ = false;
        Render();
        Q_EMIT DocumentChanged();
    });
    auto* place = new QPushButton(QStringLiteral("Duplicate selected object"), this);
    place->setObjectName(QStringLiteral("scene.place"));
    inspector->addRow(place);
    connect(place, &QPushButton::clicked, this, &SceneWorkspace::Place);
    RecoveryButton_ = new QPushButton(QStringLiteral("Restore recovery snapshot"), this);
    RecoveryButton_->setObjectName(QStringLiteral("scene.recover"));
    inspector->addRow(RecoveryButton_);
    connect(RecoveryButton_, &QPushButton::clicked, this, &SceneWorkspace::Recover);
    body->addLayout(inspector);
    layout->addLayout(body);
    Status_ = new QLabel(
        QStringLiteral(
            "Open a world-demo level. The game-owned v1 adapter supports at most 32 objects and 16 collision boxes."),
        this);
    Status_->setObjectName(QStringLiteral("sceneStatus"));
    Status_->setWordWrap(true);
    layout->addWidget(Status_);
    connect(&Preview_, &ScenePreview::Changed, this, [this]() {
        Status_->setText(Preview_.Status());
        Changed();
    });
    connect(Hierarchy_, &QListWidget::currentRowChanged, this, [this]() {
        if (Rendering_)
        {
            return;
        }
        const auto requested = Hierarchy_->currentItem() != nullptr
                                   ? Name(Hierarchy_->currentItem()->data(Qt::UserRole).toString())
                                   : world_demo::Name{};
        // A selection change may not discard a focused/incomplete numeric buffer.
        if (Pending_ && !Apply())
        {
            Render();
            return;
        }
        Preview_.Select(requested);
        Render();
    });
    Render();
}
void SceneWorkspace::SetProject(const QString& root, foundation::uint64 epoch)
{
    const auto canonicalRoot = root.isEmpty() ? QString() : QFileInfo(root).canonicalFilePath();
    if (canonicalRoot == Root_ && epoch == Epoch_)
    {
        return;
    }
    Preview_.Stop();
    Document_.Clear();
    Root_ = canonicalRoot;
    Epoch_ = epoch;
    OwnRecovery_ = false;
    RecoveredRevision_ = ~foundation::uint64{0};
    Path_.clear();
    Disk_.clear();
    Pending_ = false;
    Render();
    setEnabled(!Root_.isEmpty());
#if defined(Q_OS_WASM)
    setEnabled(false);
    setToolTip(QStringLiteral("Scene authoring currently requires the desktop editor."));
#endif
}
bool SceneWorkspace::Open(const QString& path)
{
    const QFileInfo info(path);
    const auto canonical = info.canonicalFilePath();
    QByteArray bytes;
    if (Pending_ || Document_.Dirty() || Root_.isEmpty() || !canonical.startsWith(Root_ + QLatin1Char('/')) ||
        canonical != info.absoluteFilePath() || !ReadScene(canonical, bytes))
    {
        Status_->setText(QStringLiteral(
            "Scene open refused; preserve the current draft, or choose a regular file in this project."));
        return false;
    }
    const auto result = Document_.Load(bytes.constData(), static_cast<foundation::usize>(bytes.size()));
    if (result.Error != world_demo::LevelError::None)
    {
        Status_->setText(QStringLiteral("Invalid scene (code %1, byte %2); the current draft was preserved.")
                             .arg(static_cast<foundation::uint8>(result.Error))
                             .arg(result.Offset));
        return false;
    }
    Preview_.Stop();
    Preview_.Select({});
    OwnRecovery_ = false;
    RecoveredRevision_ = ~foundation::uint64{0};
    Path_ = canonical;
    Disk_ = bytes;
    Pending_ = false;
    Status_->setText(QStringLiteral("Scene loaded. Source, authoring draft and gameplay remain separate."));
    Render();
    Q_EMIT DocumentChanged();
    return true;
}
bool SceneWorkspace::Dirty() const noexcept
{
    return Pending_ || Document_.Dirty() || Document_.Previewing();
}
bool SceneWorkspace::Apply()
{
    if (!Pending_)
    {
        return true;
    }
    foundation::float32 values[5]{};
    for (foundation::usize i = 0; i < 5; ++i)
    {
        const auto bytes = Fields_[i]->text().toLatin1();
        if (foundation::ParseFloat32(bytes.constData(), static_cast<foundation::usize>(bytes.size()), values[i]) !=
            foundation::NumberParseStatus::Success)
        {
            Status_->setText(
                QStringLiteral("Enter a complete finite number in every transform field. Pending text was preserved."));
            return false;
        }
    }
    const auto id = PendingTarget_;
    if (!Document_.BeginTransform(&id, 1, PendingStamp_))
    {
        Status_->setText(QStringLiteral("Scene changed while these fields were pending. Reset the pending fields to "
                                        "accept current values; the buffer was preserved."));
        return false;
    }
    const SceneTransform value{{values[0], values[1]}, values[2], {values[3], values[4]}};
    if (!Document_.PreviewTransform(&value, 1))
    {
        Document_.CancelTransform();
        Status_->setText(QStringLiteral("Transform is outside the level schema bounds; pending text was preserved."));
        return false;
    }
    (void)Document_.AcceptTransform();
    Pending_ = false;
    Changed();
    return true;
}
void SceneWorkspace::Changed()
{
    if (Document_.Dirty() && !Path_.isEmpty() &&
        (!OwnRecovery_ || RecoveredRevision_ != Document_.Stamp(0, 0).Revision))
    {
        if (WriteSceneRecovery(Path_, Disk_, Document_))
        {
            OwnRecovery_ = true;
            RecoveredRevision_ = Document_.Stamp(0, 0).Revision;
        }
        else
        {
            Status_->setText(QStringLiteral("Scene edited, but recovery could not be written. Save explicitly."));
        }
    }
    else if (!Document_.Dirty() && OwnRecovery_ && !Path_.isEmpty())
    {
        (void)QFile::remove(Path_ + QStringLiteral(".ludus-recovery"));
        OwnRecovery_ = false;
    }
    Render();
    Q_EMIT DocumentChanged();
}
bool SceneWorkspace::Save()
{
    if (!Apply())
    {
        return false;
    }
    QByteArray saved;
    if (!SaveScene(Path_, Disk_, Document_, saved))
    {
        Status_->setText(QStringLiteral(
            "Scene save failed or source changed on disk. Reopen/merge explicitly; the draft was preserved."));
        return false;
    }
    Disk_ = saved;
    OwnRecovery_ = false;
    Status_->setText(QStringLiteral("Scene saved."));
    Render();
    Q_EMIT DocumentChanged();
    return true;
}
void SceneWorkspace::Undo()
{
    if (!Pending_ && Document_.Undo())
    {
        Changed();
    }
}
void SceneWorkspace::Redo()
{
    if (!Pending_ && Document_.Redo())
    {
        Changed();
    }
}
bool SceneWorkspace::ConfirmDiscard()
{
    if (!Dirty())
    {
        return true;
    }
    const auto answer = QMessageBox::question(this,
                                              QStringLiteral("Unsaved scene"),
                                              QStringLiteral("Save this scene before continuing?"),
                                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                                              QMessageBox::Cancel);
    if (answer == QMessageBox::Save)
    {
        Document_.CancelTransform();
        return Save();
    }
    if (answer != QMessageBox::Discard)
    {
        return false;
    }
    Preview_.Stop();
    Pending_ = false;
    (void)Document_.Load(Disk_.constData(), static_cast<foundation::usize>(Disk_.size()));
    (void)QFile::remove(Path_ + QStringLiteral(".ludus-recovery"));
    Render();
    Q_EMIT DocumentChanged();
    return true;
}
void SceneWorkspace::Place()
{
    if (!Apply())
    {
        return;
    }
    auto candidate = Document_.Draft();
    const auto selected = SceneDocument::Find(candidate, Preview_.Selected());
    if (selected == candidate.Level.EntityCount || candidate.Level.EntityCount == 32 ||
        candidate.Level.Entities[selected].Type == world_demo::Kind::Player)
    {
        Status_->setText(
            QStringLiteral("Select an enemy or exit to duplicate (the schema requires exactly one player)."));
        return;
    }
    auto entity = candidate.Level.Entities[selected];
    for (foundation::uint32 suffix = 1; suffix < 1000; ++suffix)
    {
        entity.Id = Name(QStringLiteral("object-%1").arg(suffix));
        bool duplicate = SceneDocument::Find(candidate, entity.Id) != candidate.Level.EntityCount;
        for (foundation::usize i = 0; i < candidate.Level.CollisionCount; ++i)
        {
            duplicate |= candidate.Level.Collision[i].Id == entity.Id;
        }
        if (duplicate)
        {
            continue;
        }
        candidate.Level.Entities[candidate.Level.EntityCount++] = entity;
        if (Document_.Commit(candidate, Document_.Stamp(0, 0)))
        {
            Preview_.Select(entity.Id);
            Changed();
        }
        return;
    }
}
void SceneWorkspace::Recover()
{
    if (Dirty())
    {
        return;
    }
    SceneSnapshot snapshot;
    if (!ReadSceneRecovery(Path_, Disk_, snapshot) || !Document_.Restore(snapshot))
    {
        Status_->setText(QStringLiteral("Recovery is stale, truncated, invalid, or already matches this scene."));
        return;
    }
    Pending_ = false;
    Changed();
}
void SceneWorkspace::Render()
{
    Rendering_ = true;
    const QSignalBlocker block(Hierarchy_);
    Hierarchy_->clear();
    const auto& scene = Document_.Visible();
    const auto selected = Preview_.Selected();
    for (foundation::usize i = 0; Document_.Loaded() && i < scene.Level.EntityCount; ++i)
    {
        auto* item = new QListWidgetItem(Text(scene.Level.Entities[i].Id), Hierarchy_);
        item->setData(Qt::UserRole, Text(scene.Level.Entities[i].Id));
        if (scene.Level.Entities[i].Id == selected)
        {
            Hierarchy_->setCurrentItem(item);
        }
    }
    const auto index = SceneDocument::Find(scene, selected);
    if (!Pending_ && index < scene.Level.EntityCount)
    {
        const auto& e = scene.Level.Entities[index];
        const foundation::float32 values[] = {e.Position.X, e.Position.Y, e.Rotation, e.Scale.X, e.Scale.Y};
        for (foundation::usize i = 0; i < 5; ++i)
        {
            Fields_[i]->setText(QString::number(static_cast<foundation::float64>(values[i]), 'g', 9));
        }
    }
    for (auto* field : Fields_)
    {
        field->setEnabled(Document_.Loaded() && (Pending_ || index < scene.Level.EntityCount) &&
                          !Document_.Previewing());
    }
    PreviewButton_->setEnabled(Document_.Loaded());
    SceneSnapshot recovery;
    RecoveryButton_->setEnabled(Document_.Loaded() && !Dirty() && ReadSceneRecovery(Path_, Disk_, recovery));
    PathLabel_->setText(Path_.isEmpty() ? QStringLiteral("No scene open")
                                        : Path_ + (Dirty() ? QStringLiteral(" — unsaved") : QString()));
    if (Pending_)
    {
        PathLabel_->setText(PathLabel_->text() + QStringLiteral(" — pending transform for ") + Text(PendingTarget_));
    }
    Rendering_ = false;
}
} // namespace ludus::editor
