#include "internal/world_workspace.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLockFile>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSplitter>
#include <QVBoxLayout>

namespace ludus::editor
{
namespace
{
bool Read(const QString& path, QByteArray& output, foundation::int64 limit)
{
    const QFileInfo info(path);
    if (!info.isFile() || info.isSymLink() || info.canonicalFilePath() != info.absoluteFilePath())
    {
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > limit)
    {
        return false;
    }
    const auto bytes = file.read(limit + 1);
    if (file.error() != QFileDevice::NoError || bytes.size() != file.size() || bytes.size() > limit)
    {
        return false;
    }
    output = bytes;
    return true;
}
bool Replace(const QString& path, const QByteArray& bytes)
{
    if (QFileInfo(path).isSymLink())
    {
        return false;
    }
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
QString FieldKey(const QString& component, const QString& property)
{
    return component + QLatin1Char('/') + property;
}
QString Display(const QJsonValue& value)
{
    if (value.isDouble())
    {
        return QString::number(value.toDouble(), 'g', 17);
    }
    return value.toString();
}
} // namespace
WorldWorkspace::WorldWorkspace(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("worldWorkspace"));
    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(QStringLiteral("World — entities and project-defined components"), this);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto* help = new QLabel(
        QStringLiteral(
            "Create or open a world, then select an entity to edit its components. Your project's "
            "authoring/world.schema.json supplies types, defaults and help. Runtime binding belongs to the game."),
        this);
    help->setWordWrap(true);
    layout->addWidget(help);
    auto* files = new QHBoxLayout;
    auto* create = new QPushButton(QStringLiteral("New World…"), this);
    create->setObjectName(QStringLiteral("worldNew"));
    auto* open = new QPushButton(QStringLiteral("Open World…"), this);
    open->setObjectName(QStringLiteral("worldOpen"));
    Recovery_ = new QPushButton(QStringLiteral("Restore Recovery"), this);
    Recovery_->setObjectName(QStringLiteral("worldRecover"));
    files->addWidget(create);
    files->addWidget(open);
    files->addWidget(Recovery_);
    files->addStretch();
    layout->addLayout(files);
    PathLabel_ = new QLabel(this);
    PathLabel_->setTextFormat(Qt::PlainText);
    PathLabel_->setWordWrap(true);
    PathLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(PathLabel_);
    auto* split = new QSplitter(this);
    layout->addWidget(split, 1);
    auto* left = new QWidget(split);
    auto* hierarchy = new QVBoxLayout(left);
    Entities_ = new QListWidget(left);
    Entities_->setObjectName(QStringLiteral("worldEntities"));
    Entities_->setAccessibleName(QStringLiteral("World entities"));
    hierarchy->addWidget(Entities_, 1);
    auto* commands = new QHBoxLayout;
    Add_ = new QPushButton(QStringLiteral("Add Entity"), left);
    Add_->setObjectName(QStringLiteral("worldAdd"));
    Duplicate_ = new QPushButton(QStringLiteral("Duplicate"), left);
    Duplicate_->setObjectName(QStringLiteral("worldDuplicate"));
    Remove_ = new QPushButton(QStringLiteral("Delete"), left);
    Remove_->setObjectName(QStringLiteral("worldDelete"));
    commands->addWidget(Add_);
    commands->addWidget(Duplicate_);
    commands->addWidget(Remove_);
    hierarchy->addLayout(commands);
    auto* scroll = new QScrollArea(split);
    scroll->setWidgetResizable(true);
    auto* inspector = new QWidget(scroll);
    Inspector_ = new QVBoxLayout(inspector);
    Inspector_->setSizeConstraint(QLayout::SetMinimumSize);
    scroll->setWidget(inspector);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 2);
    auto* edits = new QHBoxLayout;
    Components_ = new QComboBox(this);
    Components_->setObjectName(QStringLiteral("worldComponentChoice"));
    Components_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    Components_->setMinimumContentsLength(12);
    Components_->setAccessibleName(QStringLiteral("Available components"));
    Attach_ = new QPushButton(QStringLiteral("Add Component"), this);
    Attach_->setObjectName(QStringLiteral("worldAttach"));
    Apply_ = new QPushButton(QStringLiteral("Apply Changes"), this);
    Apply_->setObjectName(QStringLiteral("worldApply"));
    Reset_ = new QPushButton(QStringLiteral("Reset Fields"), this);
    Reset_->setObjectName(QStringLiteral("worldReset"));
    edits->addWidget(Components_, 1);
    edits->addWidget(Attach_);
    edits->addWidget(Apply_);
    edits->addWidget(Reset_);
    layout->addLayout(edits);
    Status_ = new QLabel(this);
    Status_->setObjectName(QStringLiteral("worldStatus"));
    Status_->setTextFormat(Qt::PlainText);
    Status_->setWordWrap(true);
    Status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(Status_);
    const auto pick = [this](bool creating) {
        if (Root_.isEmpty())
        {
            Changed(QStringLiteral("Open a project first."));
            return;
        }
        auto* picker = new QFileDialog(this,
                                       creating ? QStringLiteral("New World") : QStringLiteral("Open World"),
                                       Root_,
                                       QStringLiteral("World documents (*.world.json)"));
        picker->setAttribute(Qt::WA_DeleteOnClose);
        picker->setFileMode(creating ? QFileDialog::AnyFile : QFileDialog::ExistingFile);
        picker->setAcceptMode(creating ? QFileDialog::AcceptSave : QFileDialog::AcceptOpen);
        picker->setDefaultSuffix(QStringLiteral("world.json"));
        const auto epoch = Epoch_;
        connect(picker, &QFileDialog::fileSelected, this, [this, creating, epoch](const QString& path) {
            if (Epoch_ != epoch)
            {
                Changed(QStringLiteral("Project changed while choosing a file. Choose again."));
                return;
            }
            if (creating)
            {
                (void)Create(path);
            }
            else
            {
                (void)Open(path);
            }
        });
        picker->open();
    };
    connect(open, &QPushButton::clicked, this, [pick]() { pick(false); });
    connect(create, &QPushButton::clicked, this, [pick]() { pick(true); });
    connect(Entities_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
        if (!Rendering_ && !Pending_)
        {
            Selected_ = item == nullptr ? QString() : item->data(Qt::UserRole).toString();
            RenderInspector();
            Changed();
        }
    });
    connect(Add_, &QPushButton::clicked, this, [this]() {
        QString id, error;
        if (!Document_.CreateEntity(error, QStringLiteral("Entity"), id))
        {
            Changed(error);
            return;
        }
        Selected_ = id;
        Render();
        Snapshot();
        Changed(QStringLiteral("Entity created. Give it a meaningful name in the inspector."));
    });
    connect(Duplicate_, &QPushButton::clicked, this, [this]() {
        QString id, error;
        if (!Document_.DuplicateEntity(error, Selected_, id))
        {
            Changed(error);
            return;
        }
        Selected_ = id;
        Render();
        Snapshot();
        Changed(QStringLiteral("Duplicate created with a new stable ID."));
    });
    connect(Remove_, &QPushButton::clicked, this, [this]() {
        const auto answer = QMessageBox::question(
            this,
            QStringLiteral("Delete Entity"),
            QStringLiteral("Delete %1? Undo can restore it. References from other entities must be cleared first.")
                .arg(Document_.Entity(Selected_).value(QStringLiteral("name")).toString()),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel);
        if (answer != QMessageBox::Yes)
        {
            return;
        }
        QString error;
        if (!Document_.RemoveEntity(Selected_, error))
        {
            Changed(error);
            return;
        }
        Selected_.clear();
        Render();
        Snapshot();
        Changed(QStringLiteral("Entity deleted. Undo is available."));
    });
    connect(Attach_, &QPushButton::clicked, this, [this]() {
        QString error;
        if (!Document_.Attach(Selected_, error, Components_->currentData().toString()))
        {
            Changed(error);
            return;
        }
        Render();
        Snapshot();
        Changed(QStringLiteral("Component added with project defaults."));
    });
    connect(Apply_, &QPushButton::clicked, this, [this]() { (void)Apply(); });
    connect(Reset_, &QPushButton::clicked, this, [this]() {
        Pending_ = false;
        Render();
        Changed(QStringLiteral("Fields reset to the last applied edit."));
    });
    connect(Recovery_, &QPushButton::clicked, this, [this]() {
        if (ConfirmDiscard())
        {
            (void)Recover();
        }
    });
#if defined(Q_OS_WASM)
    create->setEnabled(false);
    open->setEnabled(false);
    help->setText(QStringLiteral("World authoring currently requires the native editor. Browser file import/export and "
                                 "runtime binding are not available for this document type."));
#endif
    Render();
    Changed(QStringLiteral("Open a project with an authoring schema to begin."));
}
void WorldWorkspace::SetProject(const QString& root, foundation::uint64 epoch)
{
    const auto canonical = root.isEmpty() ? QString() : QFileInfo(root).canonicalFilePath();
    if (Root_ == canonical && Epoch_ == epoch)
    {
        return;
    }
    Root_ = canonical;
    Epoch_ = epoch;
    Path_.clear();
    Selected_.clear();
    Disk_.clear();
    SchemaDisk_.clear();
    Pending_ = false;
    Unsaved_ = false;
    Document_.Clear();
    RecoveryFailed_ = false;
    Render();
    Changed(Root_.isEmpty() ? QStringLiteral("Open a project to begin.")
                            : QStringLiteral("New or Open World loads this project's authoring/world.schema.json. See "
                                             "the live-edit-game sample for a starter."));
}
bool WorldWorkspace::Inside(const QString& path, bool existing) const
{
    if (Root_.isEmpty())
    {
        return false;
    }
    const QFileInfo info(path);
    const auto absolute = info.absoluteFilePath();
    if (!absolute.startsWith(Root_ + QLatin1Char('/')) || info.isSymLink())
    {
        return false;
    }
    if (existing)
    {
        return info.isFile() && info.canonicalFilePath() == absolute;
    }
    const QFileInfo parent(info.absolutePath());
    return !info.exists() && parent.isDir() && parent.canonicalFilePath() == parent.absoluteFilePath();
}
bool WorldWorkspace::LoadProjectSchema(WorldDocument& candidate, QByteArray& schema)
{
    const auto path = QDir(Root_).filePath(QStringLiteral("authoring/world.schema.json"));
    QString error;
    if (!Inside(path, true) || !Read(path, schema, 65536))
    {
        Changed(QStringLiteral("Missing or unreadable authoring/world.schema.json. The game owner must provide a "
                               "schema; use the live-edit-game sample to learn the workflow."));
        return false;
    }
    if (!candidate.LoadSchema(schema, error))
    {
        Changed(QStringLiteral("Schema: ") + error);
        return false;
    }
    return true;
}
bool WorldWorkspace::Open(const QString& path)
{
    WorldDocument candidate;
    QByteArray schema, bytes;
    QString error;
    if (!Inside(path, true) || !Read(path, bytes, WorldDocument::MaxBytes))
    {
        Changed(QStringLiteral("Open a regular world file inside the project's source folder (at most 512 KiB)."));
        return false;
    }
    if (!LoadProjectSchema(candidate, schema))
    {
        return false;
    }
    if (!candidate.Load(bytes, error))
    {
        Changed(error);
        return false;
    }
    if (!ConfirmDiscard())
    {
        return false;
    }
    // Save in the confirmation can change the same file being reopened.
    // Validate the current bytes again before replacing the live document.
    QByteArray currentSchema;
    if (!Read(QDir(Root_).filePath(QStringLiteral("authoring/world.schema.json")), currentSchema, 65536) ||
        currentSchema != schema || !Read(path, bytes, WorldDocument::MaxBytes) || !candidate.Load(bytes, error))
    {
        Changed(QStringLiteral("Incoming world/schema changed while opening. Retry Open World."));
        return false;
    }
    RecoveryFailed_ = false;
    Document_ = candidate;
    SchemaDisk_ = schema;
    Disk_ = bytes;
    Path_ = QFileInfo(path).absoluteFilePath();
    Selected_.clear();
    Pending_ = false;
    Unsaved_ = false;
    Render();
    Changed(QStringLiteral("World opened. Select an entity or add one; recovery is restored only when requested."));
    return true;
}
bool WorldWorkspace::Create(const QString& path)
{
    if (!Inside(path, false))
    {
        Changed(QStringLiteral(
            "Choose a new file inside the project's source folder. Existing files are never replaced by New World."));
        return false;
    }
    WorldDocument candidate;
    QByteArray schema;
    QString error;
    if (!LoadProjectSchema(candidate, schema) || !candidate.New(error))
    {
        if (!error.isEmpty())
        {
            Changed(error);
        }
        return false;
    }
    if (!ConfirmDiscard())
    {
        return false;
    }
    RecoveryFailed_ = false;
    Document_ = candidate;
    SchemaDisk_ = schema;
    Disk_.clear();
    Path_ = QFileInfo(path).absoluteFilePath();
    Selected_.clear();
    Pending_ = false;
    Unsaved_ = true;
    Render();
    Changed(QStringLiteral("New world. Add entities, then Save World to create the file."));
    return true;
}
void WorldWorkspace::Render()
{
    Rendering_ = true;
    Entities_->clear();
    for (const auto value : Document_.Draft().value(QStringLiteral("entities")).toArray())
    {
        const auto entity = value.toObject();
        auto* item = new QListWidgetItem(entity.value(QStringLiteral("name")).toString(), Entities_);
        const auto id = entity.value(QStringLiteral("id")).toString();
        item->setData(Qt::UserRole, id);
        item->setToolTip(id);
        if (id == Selected_)
        {
            Entities_->setCurrentItem(item);
        }
    }
    if (Document_.Entity(Selected_).isEmpty())
    {
        Selected_.clear();
    }
    Rendering_ = false;
    RenderInspector();
}
void WorldWorkspace::RenderInspector()
{
    Fields_.clear();
    Name_ = nullptr;
    while (auto* item = Inspector_->takeAt(0))
    {
        delete item->widget();
        delete item;
    }
    const auto entity = Document_.Entity(Selected_);
    Components_->clear();
    if (entity.isEmpty())
    {
        auto* hint = new QLabel(
            Document_.Loaded()
                ? QStringLiteral("Select an entity. Add Entity starts with the components your project requires.")
                : QStringLiteral("No world open."));
        hint->setTextFormat(Qt::PlainText);
        hint->setWordWrap(true);
        Inspector_->addWidget(hint);
        Inspector_->addStretch();
        return;
    }
    auto* form = new QWidget;
    auto* nameLayout = new QFormLayout(form);
    Name_ = new QLineEdit(entity.value(QStringLiteral("name")).toString(), form);
    Name_->setObjectName(QStringLiteral("worldEntityName"));
    Name_->setAccessibleName(QStringLiteral("Entity name"));
    nameLayout->addRow(QStringLiteral("Name"), Name_);
    Inspector_->addWidget(form);
    connect(Name_, &QLineEdit::textEdited, this, [this]() { Pending(); });
    auto* id = new QLabel(QStringLiteral("Stable ID: ") + Selected_);
    id->setWordWrap(true);
    id->setTextInteractionFlags(Qt::TextSelectableByMouse);
    Inspector_->addWidget(id);
    const auto attached = entity.value(QStringLiteral("components")).toObject();
    for (const auto definition : Document_.Schema().value(QStringLiteral("components")).toArray())
    {
        const auto component = definition.toObject();
        const auto cid = component.value(QStringLiteral("id")).toString();
        const auto label = component.value(QStringLiteral("label")).toString();
        if (!attached.contains(cid))
        {
            Components_->addItem(label, cid);
            continue;
        }
        auto* group = new QGroupBox(label);
        auto* rows = new QFormLayout(group);
        const auto help = component.value(QStringLiteral("help")).toString();
        auto* hint = new QLabel(help, group);
        hint->setTextFormat(Qt::PlainText);
        hint->setWordWrap(true);
        hint->setMinimumHeight(hint->heightForWidth(360));
        rows->addRow(hint);
        const auto values = attached.value(cid).toObject();
        for (const auto value : component.value(QStringLiteral("properties")).toArray())
        {
            const auto p = value.toObject();
            const auto pid = p.value(QStringLiteral("id")).toString();
            const auto type = p.value(QStringLiteral("type")).toString();
            const auto current = values.value(pid);
            QWidget* field = nullptr;
            if (type == QStringLiteral("boolean"))
            {
                auto* check = new QCheckBox(group);
                check->setChecked(current.toBool());
                connect(check, &QCheckBox::toggled, this, [this]() { Pending(); });
                field = check;
            }
            else if (type == QStringLiteral("enum") || type == QStringLiteral("entity") ||
                     type == QStringLiteral("asset"))
            {
                auto* choices = new QComboBox(group);
                choices->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
                choices->setMinimumContentsLength(12);
                if (type == QStringLiteral("enum"))
                {
                    for (const auto choice : p.value(QStringLiteral("choices")).toArray())
                    {
                        choices->addItem(choice.toString(), choice.toString());
                    }
                }
                else
                {
                    choices->addItem(QStringLiteral("None"), QString());
                    const auto candidates = type == QStringLiteral("entity")
                                                ? Document_.Draft().value(QStringLiteral("entities")).toArray()
                                                : Document_.Schema().value(QStringLiteral("assets")).toArray();
                    for (const auto candidate : candidates)
                    {
                        const auto entry = candidate.toObject();
                        if (type == QStringLiteral("asset") &&
                            entry.value(QStringLiteral("kind")) != p.value(QStringLiteral("asset_kind")))
                        {
                            continue;
                        }
                        const auto key = entry.value(QStringLiteral("id")).toString();
                        choices->addItem(entry.value(type == QStringLiteral("entity") ? QStringLiteral("name")
                                                                                      : QStringLiteral("label"))
                                                 .toString() +
                                             QStringLiteral(" — ") + key,
                                         key);
                    }
                }
                choices->setCurrentIndex(choices->findData(current.toString()));
                connect(choices, &QComboBox::currentIndexChanged, this, [this]() { Pending(); });
                field = choices;
            }
            else
            {
                auto* text = new QLineEdit(Display(current), group);
                connect(text, &QLineEdit::textEdited, this, [this]() { Pending(); });
                field = text;
            }
            field->setObjectName(QStringLiteral("worldField:") + FieldKey(cid, pid));
            field->setProperty("worldType", type);
            field->setAccessibleName(p.value(QStringLiteral("label")).toString());
            QString tooltip = p.value(QStringLiteral("help")).toString();
            if (type == QStringLiteral("number") || type == QStringLiteral("integer"))
            {
                tooltip += QStringLiteral("\nRange: %1 to %2")
                               .arg(Display(p.value(QStringLiteral("min"))), Display(p.value(QStringLiteral("max"))));
            }
            field->setToolTip(tooltip);
            auto title = p.value(QStringLiteral("label")).toString();
            const auto unit = p.value(QStringLiteral("unit")).toString();
            if (!unit.isEmpty())
            {
                title += QStringLiteral(" (%1)").arg(unit);
            }
            rows->addRow(title, field);
            Fields_.insert(FieldKey(cid, pid), field);
        }
        if (component.value(QStringLiteral("required")).toBool())
        {
            rows->addRow(new QLabel(QStringLiteral("Required by this project"), group));
        }
        else
        {
            auto* remove = new QPushButton(QStringLiteral("Remove Component"), group);
            remove->setObjectName(QStringLiteral("worldDetach:") + cid);
            connect(remove, &QPushButton::clicked, this, [this, cid]() {
                if (Pending_)
                {
                    Changed(QStringLiteral("Apply or reset fields before removing a component."));
                    return;
                }
                QString error;
                if (!Document_.Detach(Selected_, error, cid))
                {
                    Changed(error);
                    return;
                }
                Render();
                Snapshot();
                Changed(QStringLiteral("Component removed. Undo is available."));
            });
            rows->addRow(remove);
        }
        group->setMinimumHeight(rows->totalHeightForWidth(360));
        group->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
        Inspector_->addWidget(group);
    }
    Inspector_->addStretch();
}
void WorldWorkspace::Pending()
{
    Pending_ = true;
    Changed(QStringLiteral(
        "Unapplied fields. Apply Changes validates them together; Reset Fields keeps the last applied edit."));
}
void WorldWorkspace::Changed(const QString& message)
{
    if (!message.isEmpty())
    {
        Status_->setText(
            message +
            (RecoveryFailed_
                 ? QStringLiteral(" Recovery could not be written; Save World is required to preserve this edit.")
                 : QString()));
    }
    Entities_->setEnabled(!Pending_);
    Add_->setEnabled(Document_.Loaded() && !Pending_ && Entities_->count() < 128);
    Duplicate_->setEnabled(!Selected_.isEmpty() && !Pending_ && Entities_->count() < 128);
    Remove_->setEnabled(!Selected_.isEmpty() && !Pending_);
    Attach_->setEnabled(!Selected_.isEmpty() && !Pending_ && Components_->count() > 0);
    Components_->setEnabled(!Pending_);
    Apply_->setEnabled(Pending_);
    Reset_->setEnabled(Pending_);
    Recovery_->setEnabled(Document_.Loaded() && !Dirty() && !Unsaved_ &&
                          QFileInfo::exists(Path_ + QStringLiteral(".ludus-recovery")));
    PathLabel_->setText(Path_.isEmpty()
                            ? QStringLiteral("No world open")
                            : (Dirty() ? QStringLiteral("● ") : QString()) + QDir(Root_).relativeFilePath(Path_));
    Q_EMIT DocumentChanged();
}
bool WorldWorkspace::Apply()
{
    if (!Pending_)
    {
        return Document_.Loaded();
    }
    auto components = Document_.Entity(Selected_).value(QStringLiteral("components")).toObject();
    QString error;
    for (auto c = components.begin(); c != components.end(); ++c)
    {
        auto values = c.value().toObject();
        for (const auto definition : Document_.Component(c.key()).value(QStringLiteral("properties")).toArray())
        {
            const auto p = definition.toObject();
            const auto pid = p.value(QStringLiteral("id")).toString();
            auto* field = Fields_.value(FieldKey(c.key(), pid));
            QJsonValue candidate;
            if (auto* check = qobject_cast<QCheckBox*>(field))
            {
                candidate = check->isChecked();
            }
            else if (auto* choice = qobject_cast<QComboBox*>(field))
            {
                candidate = choice->currentData().toString();
            }
            else if (auto* text = qobject_cast<QLineEdit*>(field))
            {
                // Preserve unchanged JSON numbers exactly rather than round them
                // through the float32 text converter during an unrelated edit.
                if (text->text() == Display(values.value(pid)))
                {
                    candidate = values.value(pid);
                }
                else if (!WorldDocument::ParseValue(p, text->text(), candidate, error))
                {
                    text->setFocus();
                    Changed(error);
                    return false;
                }
            }
            else
            {
                Changed(QStringLiteral("Inspector no longer matches the selected entity."));
                return false;
            }
            values.insert(pid, candidate);
        }
        c.value() = values;
    }
    if (!Document_.Edit(Selected_, components, Name_->text(), error))
    {
        Changed(error);
        return false;
    }
    Pending_ = false;
    // Update labels in place so Save preserves the focused field's text buffer.
    for (foundation::int32 i = 0; i < Entities_->count(); ++i)
    {
        auto* item = Entities_->item(i);
        item->setText(Document_.Entity(item->data(Qt::UserRole).toString()).value(QStringLiteral("name")).toString());
    }
    for (auto* field : Fields_)
    {
        auto* choice = qobject_cast<QComboBox*>(field);
        if (choice == nullptr || field->property("worldType").toString() != QStringLiteral("entity"))
        {
            continue;
        }
        for (foundation::int32 i = 1; i < choice->count(); ++i)
        {
            const auto id = choice->itemData(i).toString();
            choice->setItemText(i,
                                Document_.Entity(id).value(QStringLiteral("name")).toString() + QStringLiteral(" — ") +
                                    id);
        }
    }
    Snapshot();
    Changed(QStringLiteral("Changes applied. Save World writes them to the project."));
    return true;
}
bool WorldWorkspace::Save()
{
    if (!Document_.Loaded() || !Apply())
    {
        return false;
    }
    QByteArray schema;
    if (!Inside(QDir(Root_).filePath(QStringLiteral("authoring/world.schema.json")), true) ||
        !Read(QDir(Root_).filePath(QStringLiteral("authoring/world.schema.json")), schema, 65536) ||
        schema != SchemaDisk_)
    {
        Changed(QStringLiteral("The project schema changed. Save was refused; keep this world open and reconcile the "
                               "schema before retrying."));
        return false;
    }
    if (!Inside(Path_, !Unsaved_) || QFileInfo(Path_ + QStringLiteral(".ludus-editor-lock")).isSymLink())
    {
        Changed(QStringLiteral("The world location changed or already exists. Save was refused."));
        return false;
    }
    QLockFile lock(Path_ + QStringLiteral(".ludus-editor-lock"));
    QByteArray current;
    if (!lock.tryLock(0) ||
        (Unsaved_ ? QFileInfo::exists(Path_) : (!Read(Path_, current, WorldDocument::MaxBytes) || current != Disk_)))
    {
        Changed(QStringLiteral(
            "The world changed on disk or is locked. Your edits remain open; resolve the conflict before saving."));
        return false;
    }
    const auto bytes = Document_.Encode();
    if (!Replace(Path_, bytes))
    {
        Changed(QStringLiteral("Could not atomically save the world. Your edits remain open."));
        return false;
    }
    Disk_ = bytes;
    Unsaved_ = false;
    Document_.AcknowledgeSave();
    RecoveryFailed_ = false;
    (void)QFile::remove(Path_ + QStringLiteral(".ludus-recovery"));
    Changed(QStringLiteral("World saved."));
    return true;
}
QByteArray WorldWorkspace::RecoveryHeader() const
{
    return QByteArrayLiteral("LUDUS-WORLD-RECOVERY-1\n") +
           QCryptographicHash::hash(SchemaDisk_ + '\n' + Disk_, QCryptographicHash::Sha256).toHex() + '\n';
}
void WorldWorkspace::Snapshot()
{
    RecoveryFailed_ = Document_.Dirty() && !Unsaved_;
    if (!Unsaved_ && !Document_.Dirty() && Inside(Path_, true))
    {
        QByteArray old;
        if (Read(Path_ + QStringLiteral(".ludus-recovery"), old, WorldDocument::MaxBytes + 256) &&
            old.startsWith(RecoveryHeader()))
        {
            (void)QFile::remove(Path_ + QStringLiteral(".ludus-recovery"));
        }
    }
    if (Unsaved_ || !Document_.Dirty() || !Inside(Path_, true))
    {
        return;
    }
    const auto recovery = Path_ + QStringLiteral(".ludus-recovery");
    const auto lockPath = Path_ + QStringLiteral(".ludus-editor-lock");
    if (QFileInfo(lockPath).isSymLink())
    {
        return;
    }
    QLockFile lock(lockPath);
    QByteArray disk;
    if (!lock.tryLock(0) || !Read(Path_, disk, WorldDocument::MaxBytes) || disk != Disk_)
    {
        return;
    }
    const auto bytes = Document_.Encode();
    RecoveryFailed_ =
        !Replace(recovery,
                 RecoveryHeader() + QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() + '\n' + bytes);
}
bool WorldWorkspace::Recover()
{
    QByteArray envelope, current, schema;
    const auto header = RecoveryHeader();
    if (Dirty() || Unsaved_ || !Inside(Path_, true) || !Read(Path_, current, WorldDocument::MaxBytes) ||
        current != Disk_ || !Read(QDir(Root_).filePath(QStringLiteral("authoring/world.schema.json")), schema, 65536) ||
        schema != SchemaDisk_ ||
        !Read(Path_ + QStringLiteral(".ludus-recovery"), envelope, WorldDocument::MaxBytes + 256) ||
        !envelope.startsWith(header))
    {
        Changed(
            QStringLiteral("Recovery is missing or belongs to a different saved world/schema. Nothing was changed."));
        return false;
    }
    const auto bytes = envelope.mid(header.size() + 65);
    if (envelope.mid(header.size(), 65) != QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() + '\n')
    {
        Changed(QStringLiteral("Recovery is damaged. Nothing was changed."));
        return false;
    }
    WorldDocument candidate;
    QString error;
    if (!candidate.LoadSchema(SchemaDisk_, error) || !candidate.Load(bytes, error))
    {
        Changed(error);
        return false;
    }
    // Recovery is one undoable edit against the saved document.
    if (!Document_.Restore(candidate.Draft(), error))
    {
        Changed(error);
        return false;
    }
    Pending_ = false;
    Selected_.clear();
    Render();
    Changed(QStringLiteral("Recovery restored for review. Save World to publish it."));
    return true;
}
bool WorldWorkspace::ConfirmDiscard()
{
    if (!Dirty())
    {
        return true;
    }
    const auto answer =
        QMessageBox::warning(this,
                             QStringLiteral("Unsaved World"),
                             QStringLiteral("Save changes to %1? Unapplied fields must pass validation before saving.")
                                 .arg(QFileInfo(Path_).fileName()),
                             QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                             QMessageBox::Cancel);
    if (answer == QMessageBox::Save)
    {
        return Save();
    }
    if (answer != QMessageBox::Discard)
    {
        return false;
    }
    Pending_ = false;
    Document_.Discard();
    // Keep a new world marked unsaved until its caller closes/replaces it.
    // Discard only author-owned recovery for the same baseline, never symlinks.
    QByteArray envelope;
    if (Read(Path_ + QStringLiteral(".ludus-recovery"), envelope, WorldDocument::MaxBytes + 256) &&
        envelope.startsWith(RecoveryHeader()))
    {
        (void)QFile::remove(Path_ + QStringLiteral(".ludus-recovery"));
    }
    Render();
    Changed(QStringLiteral("Changes discarded."));
    return true;
}
void WorldWorkspace::Undo()
{
    if (!Pending_ && Document_.Undo())
    {
        Render();
        Snapshot();
        Changed(QStringLiteral("World edit undone."));
    }
}
void WorldWorkspace::Redo()
{
    if (!Pending_ && Document_.Redo())
    {
        Render();
        Snapshot();
        Changed(QStringLiteral("World edit redone."));
    }
}
} // namespace ludus::editor
