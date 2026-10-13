#include "internal/level_workspace.h"
#include "internal/preview_viewport.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QSaveFile>
#include <QUuid>
#include <QVBoxLayout>
#if !defined(Q_OS_WASM)
#    include <QProcessEnvironment>
#endif

namespace ludus::editor
{
LevelWorkspace::LevelWorkspace(const ToolingPaths& tooling, QWidget* parent) : QWidget(parent), Tooling_(tooling)
{
    setObjectName(QStringLiteral("levelWorkspace"));
    auto* layout = new QVBoxLayout(this);
    auto* actions = new QHBoxLayout;
    New_ = new QPushButton(QStringLiteral("New 3D scene"), this);
    Open_ = new QPushButton(QStringLiteral("Open 3D scene"), this);
    Import_ = new QPushButton(QStringLiteral("Import reference scene"), this);
    Save_ = new QPushButton(QStringLiteral("Save scene"), this);
    Cancel_ = new QPushButton(QStringLiteral("Cancel import"), this);
    for (auto* button : {New_, Open_, Import_, Save_, Cancel_})
    {
        actions->addWidget(button);
    }
    layout->addLayout(actions);
    File_ = new QLabel(this);
    Status_ = new QLabel(this);
    Status_->setObjectName(QStringLiteral("levelStatus"));
    Status_->setWordWrap(true);
    layout->addWidget(File_);
    auto* body = new QHBoxLayout;
    Objects_ = new QListWidget(this);
    Objects_->setObjectName(QStringLiteral("levelObjects"));
    body->addWidget(Objects_, 1);
    Viewport_ = new PreviewViewport(&Preview_, this, false);
    Viewport_->setAlignment(Qt::AlignCenter);

    Viewport_->setObjectName(QStringLiteral("levelViewport"));
    body->addWidget(Viewport_, 2);
    layout->addLayout(body, 1);
    auto* inspector = new QHBoxLayout;
    Name_ = new QLineEdit(this);
    Name_->setPlaceholderText(QStringLiteral("Object name"));
    inspector->addWidget(Name_);
    for (auto*& translation : Translation_)
    {
        translation = new QDoubleSpinBox(this);
        translation->setRange(-1000000, 1000000);
        translation->setDecimals(4);
        inspector->addWidget(translation);
        connect(translation, &QDoubleSpinBox::editingFinished, this, &LevelWorkspace::Edit);
    }
    Remove_ = new QPushButton(QStringLiteral("Remove object"), this);
    inspector->addWidget(Remove_);
    layout->addLayout(inspector);
    layout->addWidget(Status_);
    Warnings_ = new QLabel(this);
    Warnings_->setTextFormat(Qt::PlainText);
    Warnings_->setWordWrap(true);
    layout->addWidget(Warnings_);
    connect(New_, &QPushButton::clicked, this, [this]() {
        const auto path = QFileDialog::getSaveFileName(this,
                                                       QStringLiteral("New 3D scene"),
                                                       QDir(Root_).filePath(QStringLiteral("scenes/main.lulevel")),
                                                       QStringLiteral("Ludus levels (*.lulevel)"));
        if (!path.isEmpty())
        {
            (void)Create(path);
        }
    });
    connect(Open_, &QPushButton::clicked, this, [this]() {
        const auto path = QFileDialog::getOpenFileName(this,
                                                       QStringLiteral("Open 3D scene"),
                                                       Root_,
                                                       QStringLiteral("Ludus levels (*.lulevel)"));
        if (!path.isEmpty())
        {
            (void)Open(path);
        }
    });
    connect(Import_, &QPushButton::clicked, this, [this]() {
        const auto file = QFileDialog::getOpenFileName(this,
                                                       QStringLiteral("Import supported PBRT reference scene"),
                                                       {},
                                                       QStringLiteral("PBRT scene (scene-v4.pbrt)"));
        if (!file.isEmpty())
        {
            (void)Import(QFileInfo(file).absolutePath());
        }
    });
    connect(Save_, &QPushButton::clicked, this, [this]() { (void)Save(); });
    connect(Cancel_, &QPushButton::clicked, this, &LevelWorkspace::Cancel);
    connect(Objects_, &QListWidget::currentRowChanged, this, &LevelWorkspace::Inspect);
    connect(Name_, &QLineEdit::editingFinished, this, &LevelWorkspace::Edit);
    connect(Remove_, &QPushButton::clicked, this, [this]() {
        const auto index = Objects_->currentRow();
        if (Busy_ || index < 0)
        {
            return;
        }
        auto objects = Level_.value(QStringLiteral("objects")).toArray();
        const auto id = objects[index].toObject().value(QStringLiteral("id")).toString();
        for (const auto& object : objects)
        {
            if (object.toObject().value(QStringLiteral("parent")).toString() == id)
            {
                Status_->setText(QStringLiteral("Remove child objects before removing their parent."));
                return;
            }
        }
        auto candidate = Level_;
        objects.removeAt(index);
        candidate.insert(QStringLiteral("objects"), objects);
        Candidate_ = candidate;
        (void)Start(QStringLiteral("assemble"));
    });
    connect(&Preview_, &RenderPreview::Changed, this, [this]() {
        if (!Busy_)
        {
            Status_->setText(Preview_.Status());
        }
    });
    connect(&Preview_, &RenderPreview::Failed, this, [this](const QString& error) {
        if (AwaitingViewport_)
        {
            Rollback(error);
        }
    });
    connect(&Preview_, &RenderPreview::FrameReady, this, [this](const QImage& frame) {
        if (PublishingCatalog_)
        {
            CandidateFrame_ = frame;
            return;
        }
        if (AwaitingViewport_)
        {
            AwaitingViewport_ = false;
            if (Operation_ == QStringLiteral("import"))
            {
                CandidateFrame_ = frame;
                CommitImport();
                return;
            }
            CommitCandidate();
        }
        Viewport_->ShowFrame(frame);
    });
#if !defined(Q_OS_WASM)
    connect(&Process_, &QProcess::readyReadStandardOutput, this, [this]() {
        Output_ += Process_.readAllStandardOutput();
        if (Output_.size() > foundation::int64{1024} * 1024)
        {
            Cancel();
            Error_ = QStringLiteral("Scene response exceeded its bounded protocol.");
        }
    });
    connect(&Process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
        {
            Error_ = QStringLiteral("Managed scene tools could not start.");
            if (PublishingCatalog_)
            {
                Rollback(Error_);
            }
            else
            {
                Publish();
            }
        }
    });
    connect(&Process_, &QProcess::finished, this, &LevelWorkspace::Completed);
#endif
    connect(&Decode_, &QThread::finished, this, &LevelWorkspace::Publish);
    Render();
}
LevelWorkspace::~LevelWorkspace()
{
    StopPreview();
    disconnect(&Decode_, nullptr, this, nullptr);
#if !defined(Q_OS_WASM)
    disconnect(&Process_, nullptr, this, nullptr);
    if (Process_.state() != QProcess::NotRunning)
    {
        Process_.terminate();
        if (!Process_.waitForFinished(5000))
        {
            Process_.kill();
            (void)Process_.waitForFinished();
        }
    }
#endif
    Decode_.quit();
    (void)Decode_.wait();
}
void LevelWorkspace::SetProject(const QString& root, foundation::uint64 epoch)
{
    if (Epoch_ == epoch && Root_ == root)
    {
        return;
    }
    if (Busy_)
    {
        PendingRoot_ = root;
        PendingEpoch_ = epoch;
        ProjectPending_ = true;
        Cancel();
        return;
    }
    StopPreview();
    (void)Preview_.SetScene({});
    Viewport_->ClearFrame(QStringLiteral("Import a reference scene to render it here."));
    Warnings_->clear();
    Scene_ = {};
    Candidate_ = {};
    PreviousScene_ = {};
    Root_ = root;
    Epoch_ = epoch;
    Level_ = {};
    Path_.clear();
    Disk_.clear();
    Dirty_ = false;
    Status_->setText(
        QStringLiteral("New/open a 3D scene, then import a reference scene. Assets are stored in this project."));
    Render();
}
bool LevelWorkspace::Relative(const QString& path, QString& relative) const
{
    if (Root_.isEmpty())
    {
        return false;
    }
    const auto absolute = QFileInfo(path).absoluteFilePath();
    relative = QDir(Root_).relativeFilePath(absolute);
    auto parent = QFileInfo(absolute).absolutePath();
    while (!QFileInfo::exists(parent))
    {
        parent = QFileInfo(parent).absolutePath();
    }
    const auto realParent = QFileInfo(parent).canonicalFilePath();
    const auto realRoot = QFileInfo(Root_).canonicalFilePath();
    return !relative.startsWith(QStringLiteral("../")) && !QDir::isAbsolutePath(relative) &&
           absolute.endsWith(QStringLiteral(".lulevel")) &&
           (realParent == realRoot || realParent.startsWith(realRoot + QLatin1Char('/')));
}
bool LevelWorkspace::Create(const QString& path)
{
    QString relative;
    if (Busy_)
    {
        return false;
    }
    if (!Relative(path, relative) || QFileInfo::exists(path))
    {
        Status_->setText(QStringLiteral("Choose a new .lulevel path inside this project, or open the existing scene."));
        return false;
    }
    if (!ConfirmDiscard())
    {
        return false;
    }
    StopPreview();
    (void)Preview_.SetScene({});
    Viewport_->ClearFrame(QStringLiteral("Import a reference scene to render it here."));
    Warnings_->clear();
    Level_ = QJsonObject{{QStringLiteral("version"), 1},
                         {QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
                         {QStringLiteral("name"), QFileInfo(path).completeBaseName()},
                         {QStringLiteral("profile"), QStringLiteral("resident-reference-v1")},
                         {QStringLiteral("objects"), QJsonArray{}},
                         {QStringLiteral("imports"), QJsonArray{}},
                         {QStringLiteral("camera"),
                          QJsonObject{{QStringLiteral("eye"), QJsonArray{0, 0, 3}},
                                      {QStringLiteral("target"), QJsonArray{0, 0, 0}},
                                      {QStringLiteral("up"), QJsonArray{0, 1, 0}},
                                      {QStringLiteral("fov"), 1.0471975512}}}};
    Path_ = relative;
    Disk_.clear();
    Dirty_ = true;
    Render();
    return true;
}
bool LevelWorkspace::Open(const QString& path)
{
    QString relative;
    if (Busy_ || !Relative(path, relative) || !ConfirmDiscard())
    {
        return false;
    }
    return Start(QStringLiteral("open"), relative);
}
bool LevelWorkspace::Import(const QString& source)
{
    if (Busy_ || Level_.isEmpty())
    {
        return false;
    }
    return Start(QStringLiteral("import"), {}, source);
}
QWidget* LevelWorkspace::TakeViewport(QWidget* parent)
{
    WorldOwned_ = true;
    Dirty_ = false;
    Viewport_->setParent(parent);
    return Viewport_;
}
QString LevelWorkspace::StatusText() const
{
    return Status_->text();
}
bool LevelWorkspace::VerifyAssets(const QJsonObject& level)
{
    if (Busy_ || level.isEmpty())
    {
        return false;
    }
    VerificationOnly_ = true;
    Candidate_ = level;
    if (!Start(QStringLiteral("assemble")))
    {
        VerificationOnly_ = false;
        return false;
    }
    return true;
}
bool LevelWorkspace::Assemble(const QJsonObject& level)
{
    if (Busy_ || level.isEmpty())
    {
        return false;
    }
    VerificationOnly_ = false;
    Candidate_ = level;
    return Start(QStringLiteral("assemble"));
}
bool LevelWorkspace::ImportInto(const QJsonObject& level, const QString& source)
{
    if (Busy_ || level.isEmpty())
    {
        return false;
    }
    // Start(import) copies Level_. Retain the authoritative document for this service operation.
    Level_ = level;
    return Import(source);
}
bool LevelWorkspace::Save()
{
    if (!CanSave())
    {
        return false;
    }
    return Start(QStringLiteral("save"), Path_);
}
bool LevelWorkspace::Start(const QString& operation, const QString& path, const QString& source)
{
#if defined(Q_OS_WASM)
    (void)operation;
    (void)path;
    (void)source;
    Status_->setText(QStringLiteral("Reference scene import requires the native Editor and installed host tools."));
    return false;
#else
    if (Busy_ || Root_.isEmpty() || !Scratch_.isValid() || Tooling_.PythonPath.isEmpty())
    {
        return false;
    }
    if (operation != QStringLiteral("assemble"))
    {
        Candidate_ = Level_;
    }
    QJsonObject request{
        {QStringLiteral("schema"), 1},
        {QStringLiteral("operation"), operation},
        {QStringLiteral("project"), Root_},
        {QStringLiteral("path"), path},
        {QStringLiteral("source"), source},
        {QStringLiteral("level"), Candidate_},
        {QStringLiteral("expected"), Disk_.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(Disk_)}};
    const auto input = QDir(Scratch_.path()).filePath(QStringLiteral("request.json"));
    QSaveFile file(input);
    const auto bytes = QJsonDocument(request).toJson(QJsonDocument::Compact);
    file.setDirectWriteFallback(false);
    if (bytes.size() > foundation::int64{1024} * 1024 || !file.open(QIODevice::WriteOnly) ||
        file.write(bytes) != bytes.size() || !file.commit())
    {
        Status_->setText(QStringLiteral("Could not write bounded scene request; current scene retained."));
        return false;
    }
    auto environment = QProcessEnvironment::systemEnvironment();
    const auto modules = QDir(Tooling_.ToolingRoot).filePath(QStringLiteral("scripts/python"));
    if (QFileInfo::exists(QDir(modules).filePath(QStringLiteral("ludus_tools/scene_editor.py"))))
    {
        environment.insert(QStringLiteral("PYTHONPATH"), modules);
    }
    Process_.setProcessEnvironment(environment);
    Process_.setProcessChannelMode(QProcess::SeparateChannels);
    Process_.setProgram(Tooling_.PythonPath);
    Process_.setArguments({QStringLiteral("-m"),
                           QStringLiteral("ludus_tools.scene_editor"),
                           input,
                           QDir(Scratch_.path()).filePath(QStringLiteral("scene.luscn"))});
    Output_.clear();
    Error_.clear();
    Scene_ = {};
    Success_ = false;
    Cancelling_ = false;
    Source_ = source;
    Operation_ = operation;
    CandidatePath_ = path;
    Busy_ = true;
    Status_->setText(QStringLiteral("Validating scene assets… Previous viewport remains available."));
    Render();
    Process_.start();
    return true;
#endif
}
void LevelWorkspace::Completed()
{
#if !defined(Q_OS_WASM)
    Output_ += Process_.readAllStandardOutput();
    const auto result = QJsonDocument::fromJson(Output_).object();
    if (PublishingCatalog_)
    {
        PublishingCatalog_ = false;
        if (result.value(QStringLiteral("schema")).toInt() == 1 && result.value(QStringLiteral("ok")).toBool() &&
            result.value(QStringLiteral("committed")).toBool() && Process_.exitCode() == 0)
        {
            Cancelling_ = false; // Publication already crossed its atomic commit point.
            Viewport_->ShowFrame(CandidateFrame_);
            CommitCandidate();
        }
        else
        {
            Rollback(result.value(QStringLiteral("message")).toString());
        }
        CandidateFrame_ = {};
        return;
    }
    if (Cancelling_ || Process_.exitCode() != 0 || !result.value(QStringLiteral("ok")).toBool() ||
        result.value(QStringLiteral("schema")).toInt() != 1 || Output_.size() > foundation::int64{1024} * 1024)
    {
        Error_ = Cancelling_ ? QStringLiteral("Import cancelled; current scene and saved level retained.")
                             : result.value(QStringLiteral("message")).toString();
        if (Error_.isEmpty())
        {
            Error_ = QStringLiteral("Scene operation failed; current scene retained.");
        }
        Publish();
        return;
    }
    Candidate_ = result.value(QStringLiteral("level")).toObject();
    CandidateRevisions_ = result.value(QStringLiteral("artifacts")).toObject();
    CatalogExpected_ = result.value(QStringLiteral("catalog_expected"));
    QStringList warnings;
    for (const auto& warning : result.value(QStringLiteral("warnings")).toArray())
    {
        warnings.append(warning.toString());
    }
    PendingWarnings_ = warnings.join(QLatin1Char('\n'));
    if (Operation_ == QStringLiteral("save") || Operation_ == QStringLiteral("open"))
    {
        CandidateDisk_ = result.value(QStringLiteral("disk")).toString();
    }
    auto* worker = new QObject;
    worker->moveToThread(&Decode_);
    connect(&Decode_, &QThread::started, worker, [this]() {
        QFile file(QDir(Scratch_.path()).filePath(QStringLiteral("scene.luscn")));
        if (!file.open(QIODevice::ReadOnly) || file.size() > foundation::int64{64} * 1024 * 1024)
        {
            Error_ = QStringLiteral("Cooked scene scratch input missing or exceeds 64 MiB.");
        }
        else
        {
            const auto bytes = file.read(foundation::int64{64} * 1024 * 1024 + 1);
            Success_ = bytes.isEmpty() && Candidate_.value(QStringLiteral("objects")).toArray().isEmpty();
            if (!Success_)
            {
                Success_ = RenderScene::LoadCooked(bytes, Candidate_, Scene_, Error_, CandidateRevisions_);
            }
            auto authoredWorld = WorldSnapshot_;
            if (Success_ && !VerificationOnly_ && authoredWorld.Loaded() &&
                (Operation_ == QStringLiteral("import") || Operation_ == QStringLiteral("open")))
            {
                // Validate the complete entity/component transaction before any
                // candidate viewport or catalog can be published.
                Success_ = authoredWorld.ImportScene(Candidate_, Error_);
            }
            if (Success_ && !VerificationOnly_ && authoredWorld.Loaded() &&
                QFileInfo::exists(QDir(Root_).filePath(QStringLiteral("authoring/render.preview.json"))))
            {
                RenderScene authored;
                Success_ = RenderScene::Load(Root_, authoredWorld, authored, Error_) &&
                           RenderScene::Merge(authored, bytes, Candidate_, Scene_, Error_, CandidateRevisions_);
            }
        }
        Decode_.quit();
    });
    connect(&Decode_, &QThread::finished, worker, &QObject::deleteLater);
    Decode_.start();
#endif
}
void LevelWorkspace::CommitImport()
{
#if !defined(Q_OS_WASM)
    QJsonObject request{{QStringLiteral("schema"), 1},
                        {QStringLiteral("operation"), QStringLiteral("commit-import")},
                        {QStringLiteral("project"), Root_},
                        {QStringLiteral("path"), QString()},
                        {QStringLiteral("source"), Source_},
                        {QStringLiteral("level"), Candidate_},
                        {QStringLiteral("expected"), CatalogExpected_}};
    QSaveFile file(QDir(Scratch_.path()).filePath(QStringLiteral("request.json")));
    file.setDirectWriteFallback(false);
    const auto bytes = QJsonDocument(request).toJson(QJsonDocument::Compact);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
    {
        Rollback(QStringLiteral("Could not publish the prepared catalog."));
        return;
    }
    Output_.clear();
    PublishingCatalog_ = true;
    Status_->setText(QStringLiteral("Committing prepared scene assets…"));
    Process_.start();
#else
    Rollback(QStringLiteral("Native host tools are required."));
#endif
}
void LevelWorkspace::CommitCandidate()
{
    Level_ = Candidate_;
    if (Scene_.Identity.isEmpty())
    {
        Viewport_->ClearFrame(QStringLiteral("No visible scene instances."));
    }
    Warnings_->setText(PendingWarnings_);
    if (Operation_ == QStringLiteral("save") || Operation_ == QStringLiteral("open"))
    {
        Disk_ = CandidateDisk_;
    }
    if (Operation_ == QStringLiteral("open"))
    {
        Path_ = CandidatePath_;
    }
    Dirty_ = !WorldOwned_ && Operation_ != QStringLiteral("save") && Operation_ != QStringLiteral("open");
    Busy_ = false;
    PreviousScene_ = {};
    Status_->setText(Operation_ == QStringLiteral("save")
                         ? QStringLiteral("Scene saved with verified project-owned assets.")
                         : QStringLiteral("Scene ready."));
    Render();
    Q_EMIT OperationFinished(true);
    ApplyPendingProject();
}
void LevelWorkspace::Rollback(const QString& error)
{
    AwaitingViewport_ = false;
    PublishingCatalog_ = false;
    Preview_.Stop();
    if (Preview_.SetScene(PreviousScene_) && !PreviousScene_.Identity.isEmpty())
    {
        (void)Preview_.Start();
    }
    PreviousScene_ = {};
    Busy_ = false;
    Status_->setText(error + QStringLiteral(" Previous scene and level retained."));
    Render();
    Q_EMIT OperationFinished(false);
    ApplyPendingProject();
}
void LevelWorkspace::Publish()
{
    if (!Success_ || Cancelling_)
    {
        Busy_ = false;
        Status_->setText(Error_);
        const bool verification = VerificationOnly_;
        VerificationOnly_ = false;
        Render();
        if (verification)
        {
            Q_EMIT AssetsVerified(false);
        }
        else
        {
            Q_EMIT OperationFinished(false);
        }
        ApplyPendingProject();
        return;
    }
    if (VerificationOnly_)
    {
        VerificationOnly_ = false;
        Busy_ = false;
        Status_->setText(QStringLiteral("World scene assets verified."));
        Render();
        Q_EMIT AssetsVerified(true);
        ApplyPendingProject();
        return;
    }
    if (Operation_ == QStringLiteral("save"))
    {
        CommitCandidate();
        return;
    }
    PreviousScene_ = Preview_.Scene();
    const bool existingFrame = Preview_.FrameAvailable() && PreviousScene_.PacketIdentity == Scene_.PacketIdentity &&
                               PreviousScene_.AssetsIdentity == Scene_.AssetsIdentity;
    Q_EMIT PreviewStarting();
    if (!Preview_.SetScene(Scene_))
    {
        Rollback(Preview_.Status());
        return;
    }
    if (Scene_.Identity.isEmpty() || existingFrame)
    {
        if (Operation_ == QStringLiteral("import"))
        {
            CommitImport();
        }
        else
        {
            CommitCandidate();
        }
        return;
    }
#if defined(LUDUS_EDITOR_NATIVE_PREVIEW)
    AwaitingViewport_ = true;
    if (!Preview_.Start())
    {
        Rollback(Preview_.Status());
        return;
    }
    Status_->setText(
        QStringLiteral("Preparing candidate viewport; the current level stays active until its first rendered frame."));
#else
    if (Operation_ == QStringLiteral("import"))
    {
        CommitImport();
    }
    else
    {
        CommitCandidate();
    }
    Status_->setText(QStringLiteral("Scene ready. This Editor build has no native rendered viewport."));
#endif
}
void LevelWorkspace::ApplyPendingProject()
{
    if (ProjectPending_ && !Busy_)
    {
        ProjectPending_ = false;
        SetProject(PendingRoot_, PendingEpoch_);
    }
}
void LevelWorkspace::Cancel()
{
    if (!Busy_)
    {
        return;
    }
    Cancelling_ = true;
    if (AwaitingViewport_)
    {
        Rollback(QStringLiteral("Import cancelled."));
        return;
    }
#if !defined(Q_OS_WASM)
    if (Process_.state() != QProcess::NotRunning)
    {
        Process_.terminate();
    }
#endif
    Status_->setText(QStringLiteral("Cancelling; waiting for the owned operation to drain…"));
}
bool LevelWorkspace::ConfirmDiscard()
{
    if (Busy_)
    {
        Status_->setText(
            QStringLiteral("Finish or cancel the current scene operation before closing/changing projects."));
        return false;
    }
    if (WorldOwned_ || !Dirty_)
    {
        return true;
    }
    const auto choice = QMessageBox::warning(this,
                                             QStringLiteral("Unsaved 3D scene"),
                                             QStringLiteral("Discard unsaved scene edits?"),
                                             QMessageBox::Discard | QMessageBox::Cancel,
                                             QMessageBox::Cancel);
    return choice == QMessageBox::Discard;
}
void LevelWorkspace::StopPreview() noexcept
{
    Preview_.Stop();
}
void LevelWorkspace::Render()
{
    Rendering_ = true;
    const auto selected = Objects_->currentRow();
    Objects_->clear();
    for (const auto& value : Level_.value(QStringLiteral("objects")).toArray())
    {
        Objects_->addItem(value.toObject().value(QStringLiteral("name")).toString());
    }
    if (selected >= 0 && selected < Objects_->count())
    {
        Objects_->setCurrentRow(selected);
    }
    File_->setText(Path_.isEmpty() ? QStringLiteral("No 3D scene open")
                                   : Path_ + (Dirty_ ? QStringLiteral(" *") : QString()));
    New_->setEnabled(!Busy_ && !Root_.isEmpty());
    Open_->setEnabled(!Busy_ && !Root_.isEmpty());
    Import_->setEnabled(!Busy_ && !Level_.isEmpty());
    Save_->setEnabled(CanSave());
    Cancel_->setEnabled(Busy_ && Operation_ != QStringLiteral("save") && !PublishingCatalog_);
    Objects_->setEnabled(!Busy_);
    Rendering_ = false;
    Inspect();
    Q_EMIT DocumentChanged();
}
void LevelWorkspace::Inspect()
{
    if (Rendering_)
    {
        return;
    }
    Rendering_ = true;
    const auto index = Objects_->currentRow();
    const auto objects = Level_.value(QStringLiteral("objects")).toArray();
    const bool available = !Busy_ && index >= 0 && index < objects.size();
    Name_->setEnabled(available);
    Remove_->setEnabled(available);
    const auto object = available ? objects[index].toObject() : QJsonObject{};
    Name_->setText(object.value(QStringLiteral("name")).toString());
    const auto transform = object.value(QStringLiteral("transform")).toArray();
    for (foundation::int32 i = 0; i < 3; ++i)
    {
        Translation_[i]->setEnabled(available);
        Translation_[i]->setValue(transform.size() == 12 ? transform[9 + i].toDouble() : 0);
    }
    Rendering_ = false;
}
void LevelWorkspace::Edit()
{
    if (Rendering_ || Busy_ || Objects_->currentRow() < 0 || Name_->text().isEmpty())
    {
        return;
    }
    auto objects = Level_.value(QStringLiteral("objects")).toArray();
    const auto index = Objects_->currentRow();
    auto object = objects[index].toObject();
    auto transform = object.value(QStringLiteral("transform")).toArray();
    object.insert(QStringLiteral("name"), Name_->text());
    for (foundation::int32 i = 0; i < 3; ++i)
    {
        transform[9 + i] = Translation_[i]->value();
    }
    object.insert(QStringLiteral("transform"), transform);
    if (objects[index].toObject() == object)
    {
        return;
    }
    objects[index] = object;
    Candidate_ = Level_;
    Candidate_.insert(QStringLiteral("objects"), objects);
    (void)Start(QStringLiteral("assemble"));
}
} // namespace ludus::editor
