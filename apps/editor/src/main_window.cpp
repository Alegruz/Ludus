#include "internal/main_window.h"
#include "internal/audio_workspace.h"

#include <QAbstractItemModel>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

#include <ludus/foundation/base/types.h>

#include <cstring>

namespace ludus::editor
{

MainWindow::MainWindow(EditorController* controller, QWidget* parent) : QMainWindow(parent), Controller_(controller)
{
    setWindowTitle(QStringLiteral("Ludus Editor"));
    BuildUi();
    BuildMenus();
    connect(Controller_, &EditorController::StateChanged, this, &MainWindow::OnStateChanged);
    OnStateChanged();
}

void MainWindow::BuildUi()
{
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);

    auto* splitter = new QSplitter(Qt::Vertical, central);

    // --- Project settings form ---
    auto* form = new QWidget(splitter);
    auto* formLayout = new QFormLayout(form);
    NameEdit_ = new QLineEdit(form);
    NameEdit_->setAccessibleName(QStringLiteral("Project name"));
    ProviderBox_ = new QComboBox(form);
    ProviderBox_->addItem(QStringLiteral("ludus"));
    ProviderBox_->addItem(QStringLiteral("cmake"));
    SourceDirEdit_ = new QLineEdit(form);
    PresetBox_ = new QComboBox(form);
    PresetBox_->addItem(QStringLiteral("linux-clang-debug"));
    PresetBox_->addItem(QStringLiteral("linux-clang-development"));
    TargetBox_ = new QComboBox(form);
    TargetBox_->setEditable(true); // target can be entered before configuration
    CwdEdit_ = new QLineEdit(form);

    formLayout->addRow(QStringLiteral("Name"), NameEdit_);
    formLayout->addRow(QStringLiteral("Provider"), ProviderBox_);
    formLayout->addRow(QStringLiteral("Source directory"), SourceDirEdit_);
    formLayout->addRow(QStringLiteral("Preset"), PresetBox_);
    formLayout->addRow(QStringLiteral("Executable target"), TargetBox_);
    formLayout->addRow(QStringLiteral("Run working directory"), CwdEdit_);

    // Argument list editor: one string per item, Add/Remove; never a shell line.
    ArgsList_ = new QListWidget(form);
    auto* argsButtons = new QWidget(form);
    auto* argsButtonsLayout = new QHBoxLayout(argsButtons);
    AddArgButton_ = new QPushButton(QStringLiteral("Add argument"), argsButtons);
    RemoveArgButton_ = new QPushButton(QStringLiteral("Remove argument"), argsButtons);
    argsButtonsLayout->addWidget(AddArgButton_);
    argsButtonsLayout->addWidget(RemoveArgButton_);
    argsButtonsLayout->addStretch();
    formLayout->addRow(QStringLiteral("Run arguments"), ArgsList_);
    formLayout->addRow(QString(), argsButtons);

    // --- Runtime status area (describes the running app; not a framebuffer) ---
    RuntimeLabel_ = new QLabel(splitter);
    RuntimeLabel_->setText(QStringLiteral("No runtime."));
    RuntimeLabel_->setWordWrap(true);
    Properties_ = new QTableWidget(splitter);
    Properties_->setColumnCount(3);
    Properties_->setHorizontalHeaderLabels(
        {QStringLiteral("Property"), QStringLiteral("Value"), QStringLiteral("Scope")});
    Properties_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    Properties_->setSelectionBehavior(QAbstractItemView::SelectRows);
    Properties_->setSelectionMode(QAbstractItemView::SingleSelection);
    ApplySessionButton_ = new QPushButton(QStringLiteral("Apply selected value to session"), splitter);
    connect(ApplySessionButton_, &QPushButton::clicked, this, [this]() {
        const int row = Properties_->currentRow();
        if (row >= 0 && Properties_->item(row, 1) != nullptr)
        {
            Controller_->EditProperty(row, Properties_->item(row, 1)->text());
        }
    });
    ApplyDocumentButton_ = new QPushButton(QStringLiteral("Copy live value to tuning draft"), splitter);
    connect(ApplyDocumentButton_, &QPushButton::clicked, this, [this]() {
        Controller_->ApplyLivePropertyToTuningDocument(Properties_->currentRow());
    });
    connect(Properties_, &QTableWidget::currentCellChanged, this, [this]() {
        ApplyDocumentButton_->setEnabled(Controller_->CanApplyToTuningDocument(Properties_->currentRow()));
    });

    // --- Output panel (read-only) ---
    Output_ = new QPlainTextEdit(splitter);
    Output_->setReadOnly(true);
    Output_->setMaximumBlockCount(5000);

    splitter->addWidget(form);
    splitter->addWidget(RuntimeLabel_);
    splitter->addWidget(Properties_);
    splitter->addWidget(ApplySessionButton_);
    splitter->addWidget(ApplyDocumentButton_);
    splitter->addWidget(Output_);
    splitter->setStretchFactor(2, 1);

    StatusLabel_ = new QLabel(central);
    StatusLabel_->setObjectName(QStringLiteral("workspaceStatus"));
    layout->addWidget(splitter);
    layout->addWidget(StatusLabel_);
    setCentralWidget(central);
    auto* audioDock = new QDockWidget(QStringLiteral("Audio content"), this);
    Audio_ = new AudioWorkspace(audioDock);
    audioDock->setWidget(Audio_);
    addDockWidget(Qt::RightDockWidgetArea, audioDock);

    RecentDock_ = new QDockWidget(QStringLiteral("Recent Projects"), this);
    RecentDock_->setObjectName(QStringLiteral("recentProjectsDock"));
    auto* recentPanel = new QWidget(RecentDock_);
    auto* recentLayout = new QVBoxLayout(recentPanel);
    RecentEmptyLabel_ =
        new QLabel(QStringLiteral("No recent projects yet. Open a project to add it here."), recentPanel);
    RecentEmptyLabel_->setWordWrap(true);
    RecentList_ = new QListWidget(recentPanel);
    RecentList_->setObjectName(QStringLiteral("recentProjectsList"));
    RecentList_->setAccessibleName(QStringLiteral("Recent projects"));
    RecentList_->setWordWrap(true);
    RecentOpenButton_ = new QPushButton(QStringLiteral("Open Selected"), recentPanel);
    RecentOpenButton_->setObjectName(QStringLiteral("openRecentProjectButton"));
    BrowseProjectButton_ = new QPushButton(QStringLiteral("Open Project..."), recentPanel);
    recentLayout->addWidget(RecentEmptyLabel_);
    recentLayout->addWidget(RecentList_);
    recentLayout->addWidget(RecentOpenButton_);
    recentLayout->addWidget(BrowseProjectButton_);
    RecentDock_->setWidget(recentPanel);
    addDockWidget(Qt::LeftDockWidgetArea, RecentDock_);
    connect(RecentList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        const QString path = item->data(Qt::UserRole).toString();
        QTimer::singleShot(0, this, [this, path]() { OpenProjectPath(path); });
    });
    connect(RecentList_, &QListWidget::currentItemChanged, this, [this]() {
        RecentOpenButton_->setEnabled(Controller_->Caps().CanOpen && RecentList_->currentItem() != nullptr);
    });
    connect(RecentOpenButton_, &QPushButton::clicked, this, [this]() {
        if (const auto* item = RecentList_->currentItem())
        {
            OpenProjectPath(item->data(Qt::UserRole).toString());
        }
    });
    connect(BrowseProjectButton_, &QPushButton::clicked, this, &MainWindow::OnOpenRequested);

    // Typed signal connections (pointer-to-member); no string-based SIGNAL/SLOT.
    connect(NameEdit_, &QLineEdit::textEdited, this, &MainWindow::OnFieldEdited);
    connect(SourceDirEdit_, &QLineEdit::textEdited, this, &MainWindow::OnFieldEdited);
    connect(CwdEdit_, &QLineEdit::textEdited, this, &MainWindow::OnFieldEdited);
    connect(ProviderBox_, &QComboBox::currentTextChanged, this, &MainWindow::OnFieldEdited);
    connect(PresetBox_, &QComboBox::currentTextChanged, this, &MainWindow::OnFieldEdited);
    connect(TargetBox_, &QComboBox::currentTextChanged, this, &MainWindow::OnFieldEdited);
    connect(AddArgButton_, &QPushButton::clicked, this, &MainWindow::OnAddArgument);
    connect(RemoveArgButton_, &QPushButton::clicked, this, &MainWindow::OnRemoveArgument);
}

void MainWindow::BuildMenus()
{
    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    OpenAction_ = fileMenu->addAction(QStringLiteral("&Open Project..."));
    OpenAction_->setShortcut(QKeySequence::Open);
    RecentMenu_ = fileMenu->addMenu(QStringLiteral("Recent &Projects"));
    RecentMenu_->setObjectName(QStringLiteral("recentProjectsMenu"));
    connect(RecentMenu_, &QMenu::aboutToShow, this, &MainWindow::RenderRecentProjects);
    ClearRecentAction_ = fileMenu->addAction(QStringLiteral("Clear Recent Projects"));
    ClearRecentAction_->setObjectName(QStringLiteral("clearRecentProjectsAction"));
    connect(ClearRecentAction_, &QAction::triggered, Controller_, &EditorController::ClearRecentProjects);
    SaveAction_ = fileMenu->addAction(QStringLiteral("&Save"));
    SaveAction_->setShortcut(QKeySequence::Save);
    ReloadAction_ = fileMenu->addAction(QStringLiteral("&Reload"));

    QMenu* projectMenu = menuBar()->addMenu(QStringLiteral("&Project"));
    NewProjectAction_ = projectMenu->addAction(QStringLiteral("&New Project..."));
    NewProjectAction_->setObjectName(QStringLiteral("newProjectAction"));
    CheckSetupAction_ = projectMenu->addAction(QStringLiteral("&Check Setup"));
    CheckSetupAction_->setObjectName(QStringLiteral("checkSetupAction"));
    SetupProjectAction_ = projectMenu->addAction(QStringLiteral("&Initialize / Repair / Update Setup..."));
    SetupProjectAction_->setObjectName(QStringLiteral("setupProjectAction"));
    connect(NewProjectAction_, &QAction::triggered, this, &MainWindow::OnNewProject);
    connect(CheckSetupAction_, &QAction::triggered, Controller_, &EditorController::CheckProjectSetup);
    connect(SetupProjectAction_, &QAction::triggered, this, &MainWindow::OnSetupProject);

    QMenu* buildMenu = menuBar()->addMenu(QStringLiteral("&Build"));
    ConfigureAction_ = buildMenu->addAction(QStringLiteral("&Configure / Refresh Targets"));
    BuildAction_ = buildMenu->addAction(QStringLiteral("&Build"));
    BuildRunAction_ = buildMenu->addAction(QStringLiteral("Build and &Run"));
    BuildDebugAction_ = buildMenu->addAction(QStringLiteral("Build && &Debug in RAD..."));
    BuildDebugAction_->setObjectName(QStringLiteral("buildDebugAction"));
    BuildDebugAction_->setToolTip(QStringLiteral("Debug native games in RAD. Optional setup is offered when missing."));
    BuildDebugAction_->setShortcut(QKeySequence(Qt::Key_F5));
    connect(BuildDebugAction_, &QAction::triggered, this, [this]() { LaunchAfterPreview(LaunchAction::Debug); });
    connect(Controller_, &EditorController::DebuggerSetupRequested, this, &MainWindow::OnDebuggerSetupRequested);
    StopAction_ = buildMenu->addAction(QStringLiteral("&Stop"));
    auto* gameToolbar = addToolBar(QStringLiteral("Game"));
    gameToolbar->setObjectName(QStringLiteral("gameToolbar"));
    gameToolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    gameToolbar->addAction(BuildAction_);
    gameToolbar->addAction(BuildRunAction_);
    gameToolbar->addAction(BuildDebugAction_);
    gameToolbar->addAction(StopAction_);

    QMenu* releaseMenu = menuBar()->addMenu(QStringLiteral("&Release"));
    SetupReleaseAction_ = releaseMenu->addAction(QStringLiteral("Set Up &Releases..."));
    PackageReleaseAction_ = releaseMenu->addAction(QStringLiteral("&Package Release..."));
    connect(SetupReleaseAction_, &QAction::triggered, this, &MainWindow::OnSetupRelease);
    connect(PackageReleaseAction_, &QAction::triggered, this, &MainWindow::OnPackageRelease);
    QMenu* playMenu = menuBar()->addMenu(QStringLiteral("&Play"));
    PlayAction_ = playMenu->addAction(QStringLiteral("Build and Play"));
    PlayAction_->setObjectName(QStringLiteral("play.start"));
    BuildReloadAction_ = playMenu->addAction(QStringLiteral("Build and Reload Code"));
    BuildReloadAction_->setObjectName(QStringLiteral("play.reload"));
    AutoReloadAction_ = playMenu->addAction(QStringLiteral("Automatically Reload Source Changes"));
    AutoReloadAction_->setCheckable(true);
    connect(AutoReloadAction_, &QAction::toggled, Controller_, &EditorController::SetAutoReload);
    ReloadAssetAction_ = playMenu->addAction(QStringLiteral("Reload Frame-clear Configuration..."));
    connect(ReloadAssetAction_, &QAction::triggered, this, [this]() {
        const auto directory = QFileInfo(Controller_->State().DescriptorPath).absolutePath();
        const auto path = QFileDialog::getOpenFileName(this,
                                                       QStringLiteral("Frame-clear configuration"),
                                                       directory,
                                                       QStringLiteral("JSON configuration (*.json)"));
        if (!path.isEmpty())
        {
            Controller_->ReloadClearConfiguration(path);
        }
    });
    PauseAction_ = playMenu->addAction(QStringLiteral("Pause"));
    StepAction_ = playMenu->addAction(QStringLiteral("Step"));
    ResumeAction_ = playMenu->addAction(QStringLiteral("Resume"));
    RefreshPropertiesAction_ = playMenu->addAction(QStringLiteral("Refresh Inspector"));
    auto* details = playMenu->addAction(QStringLiteral("Refresh Session Details"));
    connect(details, &QAction::triggered, Controller_, &EditorController::RefreshSessionDetails);
    UndoSessionAction_ = playMenu->addAction(QStringLiteral("Undo Session Edit"));
    RedoSessionAction_ = playMenu->addAction(QStringLiteral("Redo Session Edit"));
    playMenu->addSeparator();
    UndoTuningAction_ = playMenu->addAction(QStringLiteral("Undo Tuning Document Edit"));
    RedoTuningAction_ = playMenu->addAction(QStringLiteral("Redo Tuning Document Edit"));
    SaveTuningAction_ = playMenu->addAction(QStringLiteral("Save Tuning Document"));
    DiscardTuningAction_ = playMenu->addAction(QStringLiteral("Discard Tuning Draft"));
    connect(UndoSessionAction_, &QAction::triggered, Controller_, &EditorController::UndoSessionEdit);
    connect(RedoSessionAction_, &QAction::triggered, Controller_, &EditorController::RedoSessionEdit);
    connect(UndoTuningAction_, &QAction::triggered, Controller_, &EditorController::UndoTuningDocument);
    connect(RedoTuningAction_, &QAction::triggered, Controller_, &EditorController::RedoTuningDocument);
    connect(SaveTuningAction_, &QAction::triggered, Controller_, &EditorController::SaveTuningDocument);
    connect(DiscardTuningAction_, &QAction::triggered, Controller_, &EditorController::DiscardTuningDocumentDraft);
    connect(PlayAction_, &QAction::triggered, this, [this]() { LaunchAfterPreview(LaunchAction::Play); });
    connect(BuildReloadAction_, &QAction::triggered, Controller_, &EditorController::BuildReload);
    connect(PauseAction_, &QAction::triggered, this, [this]() { Controller_->PlayCommand(QStringLiteral("Pause")); });
    connect(StepAction_, &QAction::triggered, this, [this]() { Controller_->PlayCommand(QStringLiteral("Step")); });
    connect(ResumeAction_, &QAction::triggered, this, [this]() { Controller_->PlayCommand(QStringLiteral("Resume")); });
    connect(RefreshPropertiesAction_, &QAction::triggered, Controller_, &EditorController::RefreshProperties);

    QMenu* outputMenu = menuBar()->addMenu(QStringLiteral("&Output"));
    ClearAction_ = outputMenu->addAction(QStringLiteral("&Clear Output"));
    CopyAction_ = outputMenu->addAction(QStringLiteral("Copy &Job Details"));

    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
    viewMenu->addAction(RecentDock_->toggleViewAction());

    connect(OpenAction_, &QAction::triggered, this, &MainWindow::OnOpenRequested);
    connect(SaveAction_, &QAction::triggered, this, &MainWindow::OnSaveRequested);
    connect(ReloadAction_, &QAction::triggered, this, &MainWindow::OnReloadRequested);
    connect(ConfigureAction_, &QAction::triggered, Controller_, &EditorController::Configure);
    connect(BuildAction_, &QAction::triggered, Controller_, &EditorController::Build);
    connect(BuildRunAction_, &QAction::triggered, this, [this]() { LaunchAfterPreview(LaunchAction::Run); });
    connect(StopAction_, &QAction::triggered, Controller_, &EditorController::Stop);
    connect(ClearAction_, &QAction::triggered, Controller_, &EditorController::ClearOutput);
    connect(CopyAction_, &QAction::triggered, this, &MainWindow::OnCopyJobDetails);
}

void MainWindow::LaunchAfterPreview(LaunchAction action, const QString& debugger, bool setup)
{
    if (AudioLaunchPending_ || AudioClosing_)
    {
        return;
    }
    AudioLaunchPending_ = true;
    Audio_->setEnabled(false);
    Audio_->ShutdownPreview();
    RenderCapabilities();
    const auto epoch = Controller_->State().ProjectEpoch;
    const auto digest = Controller_->State().SavedDigest;
    auto* timer = new QTimer(this);
    timer->setInterval(20);
    connect(timer, &QTimer::timeout, this, [this, timer, epoch, digest, action, debugger, setup]() {
        if (!Audio_->PreviewFinished())
        {
            return;
        }
        timer->stop();
        timer->deleteLater();
        AudioLaunchPending_ = false;
        if (AudioClosing_)
        {
            return;
        }
        Audio_->ResetPreview();
        Audio_->setEnabled(Controller_->State().Document == DocumentState::ProjectLoaded);
        RenderCapabilities();
        if (epoch != Controller_->State().ProjectEpoch || digest != Controller_->State().SavedDigest)
        {
            return;
        }
        switch (action)
        {
            case LaunchAction::Run:
                Controller_->BuildRun();
                break;
            case LaunchAction::Play:
                Controller_->Play();
                break;
            case LaunchAction::Debug:
                Controller_->BuildDebug(debugger, setup);
                break;
        }
    });
    timer->start();
}

ProjectDescriptor MainWindow::DraftFromFields() const
{
    ProjectDescriptor draft = Controller_->State().Draft;
    draft.Name = NameEdit_->text();
    draft.ProviderKind = ProviderBox_->currentText() == QStringLiteral("cmake") ? Provider::Cmake : Provider::Ludus;
    draft.SourceDir = SourceDirEdit_->text();
    draft.Preset = PresetBox_->currentText();
    draft.Target = TargetBox_->currentText();
    draft.RunCwd = CwdEdit_->text();
    draft.RunArgs.clear();
    for (int i = 0; i < ArgsList_->count(); ++i)
    {
        draft.RunArgs.append(ArgsList_->item(i)->text());
    }
    return draft;
}

void MainWindow::OnFieldEdited()
{
    if (Rendering_)
    {
        return; // field change came from RenderFields, not a user edit
    }
    // Defer the action to the next event-loop turn so a render-triggered signal
    // never dispatches a nested edit, then revalidate against current state.
    const ProjectDescriptor draft = DraftFromFields();
    QTimer::singleShot(0, Controller_, [this, draft]() { Controller_->EditDraft(draft); });
}

void MainWindow::OnAddArgument()
{
    if (Rendering_)
    {
        return;
    }
    auto* item = new QListWidgetItem(QString(), ArgsList_);
    item->setFlags(item->flags() | Qt::ItemIsEditable);
    ArgsList_->editItem(item);
    OnFieldEdited();
}

void MainWindow::OnRemoveArgument()
{
    if (Rendering_)
    {
        return;
    }
    delete ArgsList_->takeItem(ArgsList_->currentRow());
    OnFieldEdited();
}

void MainWindow::OnOpenRequested()
{
    if (!Controller_->Caps().CanOpen)
    {
        return;
    }
    const QString directory = Controller_->State().DescriptorPath.isEmpty()
                                  ? QString()
                                  : QFileInfo(Controller_->State().DescriptorPath).absolutePath();
    const QString path = QFileDialog::getOpenFileName(this,
                                                      QStringLiteral("Open Project Descriptor"),
                                                      directory,
                                                      QStringLiteral("Ludus Project (*.json)"));
    OpenProjectPath(path);
}

void MainWindow::OpenProjectPath(const QString& path)
{
    if (path.isEmpty() || !Controller_->Caps().CanOpen || AudioClosing_ || !Audio_->ConfirmDiscard())
    {
        return;
    }
    Audio_->StopPreview();
    // Dirty-document confirmation is offered asynchronously via a message box.
    if (Controller_->State().Dirty() && Controller_->State().Document == DocumentState::ProjectLoaded)
    {
        const auto choice = QMessageBox::question(this,
                                                  QStringLiteral("Unsaved changes"),
                                                  QStringLiteral("Save changes before opening another project?"),
                                                  QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (choice == QMessageBox::Cancel)
        {
            return;
        }
        if (choice == QMessageBox::Save)
        {
            Controller_->Save();
            if (Controller_->State().Dirty())
            {
                return; // failed Save aborts the Open
            }
        }
    }
    Controller_->OpenProject(path);
}

void MainWindow::OnSaveRequested()
{
    const auto& state = Controller_->State();
    if (state.Draft.SourceDir != state.Saved.SourceDir && !Audio_->ConfirmDiscard())
    {
        return;
    }
    Controller_->Save();
}

void MainWindow::OnReloadRequested()
{
    if (!Audio_->ConfirmDiscard())
    {
        return;
    }
    Audio_->StopPreview();
    if (Controller_->State().Dirty())
    {
        const auto choice = QMessageBox::question(this,
                                                  QStringLiteral("Unsaved changes"),
                                                  QStringLiteral("Discard changes and reload from disk?"),
                                                  QMessageBox::Discard | QMessageBox::Cancel);
        if (choice != QMessageBox::Discard)
        {
            return;
        }
    }
    Controller_->Reload();
}

void MainWindow::OnCopyJobDetails()
{
    QApplication::clipboard()->setText(Controller_->JobDetails());
}

void MainWindow::OnStateChanged()
{
    RenderFields();
    RenderRecentProjects();
    RenderCapabilities();
    RenderStatus();
    const auto& state = Controller_->State();
    Audio_->setEnabled(state.Document == DocumentState::ProjectLoaded && !AudioClosing_ && !AudioLaunchPending_);
    if (state.Document == DocumentState::ProjectLoaded)
    {
        const auto root = QDir(QFileInfo(state.DescriptorPath).absolutePath()).absoluteFilePath(state.Saved.SourceDir);
        Audio_->SetRoot(QDir(root).absoluteFilePath(QStringLiteral("content")));
    }

    // If a close was requested and the workspace is now closeable, finish.
    RenderProperties();
    if (CloseConfirmed_ && Controller_->Caps().CanCloseImmediately)
    {
        close();
    }
}

void MainWindow::RenderRecentProjects()
{
    const auto& projects = Controller_->RecentProjects();
    QStringList stamp;
    for (const RecentProject& project : projects)
    {
        stamp.append(project.Name);
        stamp.append(project.DescriptorPath);
        stamp.append(QFileInfo(project.DescriptorPath).isFile() ? QString() : QStringLiteral("Missing"));
    }
    RecentEmptyLabel_->setVisible(projects.isEmpty());
    RecentList_->setVisible(!projects.isEmpty());
    if (stamp == RecentProjectsStamp_)
    {
        return;
    }
    RecentProjectsStamp_ = stamp;
    const auto* selected = RecentList_->currentItem();
    const QString selectedPath = selected != nullptr ? selected->data(Qt::UserRole).toString() : QString();
    const QSignalBlocker blocker(RecentList_);
    RecentList_->clear();
    RecentMenu_->clear();
    for (const RecentProject& project : projects)
    {
        const bool missing = !QFileInfo(project.DescriptorPath).isFile();
        const QString name = missing ? QStringLiteral("%1 (missing)").arg(project.Name) : project.Name;
        auto* item = new QListWidgetItem(name + QStringLiteral("\n") + project.DescriptorPath, RecentList_);
        item->setData(Qt::UserRole, project.DescriptorPath);
        item->setToolTip(project.DescriptorPath);
        if (project.DescriptorPath == selectedPath)
        {
            RecentList_->setCurrentItem(item);
        }
        // Escape ampersands so project names/paths are literal menu labels.
        QString label = name + QStringLiteral(" — ") + project.DescriptorPath;
        label.replace(QStringLiteral("&"), QStringLiteral("&&"));
        auto* action = RecentMenu_->addAction(label);
        action->setData(project.DescriptorPath);
        action->setToolTip(project.DescriptorPath);
        connect(action, &QAction::triggered, this, [this, path = project.DescriptorPath]() {
            QTimer::singleShot(0, this, [this, path]() { OpenProjectPath(path); });
        });
    }
    if (RecentList_->currentItem() == nullptr && RecentList_->count() != 0)
    {
        RecentList_->setCurrentRow(0);
    }
    RecentOpenButton_->setEnabled(Controller_->Caps().CanOpen && RecentList_->currentItem() != nullptr);
}

void MainWindow::RenderFields()
{
    Rendering_ = true;
    const QSignalBlocker b1(NameEdit_);
    const QSignalBlocker b2(ProviderBox_);
    const QSignalBlocker b3(SourceDirEdit_);
    const QSignalBlocker b4(PresetBox_);
    const QSignalBlocker b5(TargetBox_);
    const QSignalBlocker b6(CwdEdit_);
    const QSignalBlocker b7(ArgsList_);

    const ProjectDescriptor& draft = Controller_->State().Draft;
    NameEdit_->setText(draft.Name);
    ProviderBox_->setCurrentText(draft.ProviderKind == Provider::Cmake ? QStringLiteral("cmake")
                                                                       : QStringLiteral("ludus"));
    SourceDirEdit_->setText(draft.SourceDir);
    PresetBox_->setCurrentText(draft.Preset);
    CwdEdit_->setText(draft.RunCwd);

    // Executable-only dropdown after Configure; the current (possibly invalid)
    // target stays visible until corrected.
    const QStringList& discovered = Controller_->State().DiscoveredTargets;
    const QString current = draft.Target;
    TargetBox_->clear();
    for (const QString& name : discovered)
    {
        TargetBox_->addItem(name);
    }
    TargetBox_->setCurrentText(current);

    ArgsList_->clear();
    for (const QString& arg : draft.RunArgs)
    {
        auto* item = new QListWidgetItem(arg, ArgsList_);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
    }
    connect(ArgsList_->model(),
            &QAbstractItemModel::dataChanged,
            this,
            &MainWindow::OnFieldEdited,
            Qt::UniqueConnection);

    // Render output and runtime status.
    Output_->setPlainText(Controller_->Log().Text());
    Rendering_ = false;
}

void MainWindow::RenderCapabilities()
{
    const Capabilities caps = Controller_->Caps();
    PlayAction_->setEnabled(Controller_->CanPlay() && !AudioLaunchPending_);
    BuildReloadAction_->setEnabled(Controller_->CanBuildReload());
    const auto play = Controller_->PlayState().Phase;
    const bool debuggerStopped = Controller_->PlayState().DebuggerStopped;
    AutoReloadAction_->setEnabled(play == PlayPhase::Running || play == PlayPhase::Paused);
    ReloadAssetAction_->setEnabled(!debuggerStopped && (play == PlayPhase::Running || play == PlayPhase::Paused));
    {
        const QSignalBlocker blocker(AutoReloadAction_);
        AutoReloadAction_->setChecked(Controller_->AutoReloadEnabled());
    }
    PauseAction_->setEnabled(!debuggerStopped && play == PlayPhase::Running);
    ResumeAction_->setEnabled(!debuggerStopped && play == PlayPhase::Paused);
    StepAction_->setEnabled(!debuggerStopped && play == PlayPhase::Paused);
    RefreshPropertiesAction_->setEnabled(play == PlayPhase::Running || play == PlayPhase::Paused);
    ApplySessionButton_->setEnabled(Controller_->CanEditProperties());
    ApplyDocumentButton_->setEnabled(Controller_->CanApplyToTuningDocument(Properties_->currentRow()));
    UndoSessionAction_->setEnabled(Controller_->CanUndoSession());
    RedoSessionAction_->setEnabled(Controller_->CanRedoSession());
    const auto& playState = Controller_->PlayState();
    UndoTuningAction_->setEnabled(playState.TuningDocumentAvailable && playState.TuningCanUndo);
    RedoTuningAction_->setEnabled(playState.TuningDocumentAvailable && playState.TuningCanRedo);
    SaveTuningAction_->setEnabled(Controller_->CanSaveTuningDocument());
    DiscardTuningAction_->setEnabled(playState.TuningDocumentAvailable && playState.TuningDocumentDirty);
    NewProjectAction_->setEnabled(caps.CanProjectCreate);
    CheckSetupAction_->setEnabled(caps.CanProjectCheck);
    SetupProjectAction_->setEnabled(caps.CanProjectSetup);
    OpenAction_->setEnabled(caps.CanOpen);
    RecentMenu_->setEnabled(caps.CanOpen && !Controller_->RecentProjects().isEmpty());
    RecentList_->setEnabled(caps.CanOpen);
    RecentOpenButton_->setEnabled(caps.CanOpen && RecentList_->currentItem() != nullptr);
    BrowseProjectButton_->setEnabled(caps.CanOpen);
    ClearRecentAction_->setEnabled(caps.CanOpen && !Controller_->RecentProjects().isEmpty());
    SaveAction_->setEnabled(caps.CanSave);
    ReloadAction_->setEnabled(caps.CanReload);
    ConfigureAction_->setEnabled(caps.CanConfigure);
    BuildAction_->setEnabled(caps.CanBuild);
    BuildRunAction_->setEnabled(caps.CanBuildRun && !AudioLaunchPending_);
    BuildDebugAction_->setEnabled(caps.CanBuildDebug && !AudioLaunchPending_);
    StopAction_->setEnabled(caps.CanStop);
    SetupReleaseAction_->setEnabled(caps.CanReleaseInit);
    PackageReleaseAction_->setEnabled(caps.CanPackage);
    ClearAction_->setEnabled(caps.CanClearOutput);
    CopyAction_->setEnabled(caps.CanCopyJobDetails);

    const bool editable = caps.CanEdit;
    NameEdit_->setEnabled(editable);
    ProviderBox_->setEnabled(editable);
    SourceDirEdit_->setEnabled(editable);
    PresetBox_->setEnabled(editable);
    TargetBox_->setEnabled(editable);
    CwdEdit_->setEnabled(editable);
    ArgsList_->setEnabled(editable);
    AddArgButton_->setEnabled(editable);
    RemoveArgButton_->setEnabled(editable);
}

void MainWindow::RenderStatus()
{
    const WorkspaceState& state = Controller_->State();
    QString phase;
    switch (state.OperationPhase)
    {
        case Phase::Idle:
            phase = QStringLiteral("Idle");
            break;
        case Phase::Starting:
            phase = QStringLiteral("Starting");
            break;
        case Phase::Configuring:
            phase = QStringLiteral("Configuring");
            break;
        case Phase::Building:
            phase = QStringLiteral("Building");
            break;
        case Phase::Publishing:
            phase = QStringLiteral("Publishing generation");
            break;
        case Phase::Launching:
            phase = QStringLiteral("Launching");
            break;
        case Phase::Debugging:
            phase = QStringLiteral("RAD session open");
            break;
        case Phase::Running:
            phase = QStringLiteral("Running");
            break;
        case Phase::Stopping:
            phase = QStringLiteral("Stopping");
            break;
        case Phase::CleanupUnknown:
            phase = QStringLiteral("Cleanup unknown");
            break;
    }

    QString status = phase;
    if (state.Result.Kind != Outcome::None)
    {
        status += QStringLiteral("  —  last result: %1").arg(QString::fromLatin1(ResultCodeName(state.Result.Code)));
        if (!state.Result.Message.isEmpty())
        {
            status += QStringLiteral(" (%1)").arg(state.Result.Message);
        }
    }
    if (state.OperationPhase == Phase::CleanupUnknown)
    {
        status += QStringLiteral("  —  operator recovery required; see Copy Job Details");
    }
    if (!state.SetupStatus.isEmpty())
    {
        status += QStringLiteral("\n") + state.SetupStatus;
    }
    StatusLabel_->setText(status);

    if (state.OperationPhase == Phase::Debugging)
    {
        RuntimeLabel_->setText(
            QStringLiteral("Set breakpoints, Run and Step in RAD. The game may be paused or running. "
                           "Stop ends this debugger session and its game."));
    }
    else if (Controller_->PlayState().Phase != PlayPhase::Stopped)
    {
        const auto& play = Controller_->PlayState();
        RuntimeLabel_->setText(QStringLiteral("Play session %1 · generation %2 · PID %4\n%3")
                                   .arg(play.Session, play.Generation, play.Message)
                                   .arg(play.HostPid));
    }
    else if (state.OperationPhase == Phase::Running)
    {
        RuntimeLabel_->setText(
            QStringLiteral("The application is running in its own window. This status area describes it; "
                           "it does not embed the game's framebuffer."));
    }
    else
    {
        RuntimeLabel_->setText(QStringLiteral("No runtime."));
    }
}

namespace
{
struct DirectoryFieldOptions
{
    QString Label;
    QString Hint;
};

QLineEdit* AddDirectoryField(QFormLayout* form, QDialog* dialog, const DirectoryFieldOptions& field)
{
    auto* row = new QWidget(dialog);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* edit = new QLineEdit(row);
    edit->setPlaceholderText(field.Hint);
    auto* browse = new QPushButton(QStringLiteral("Browse..."), row);
    layout->addWidget(edit);
    layout->addWidget(browse);
    QObject::connect(browse, &QPushButton::clicked, dialog, [dialog, edit]() {
        const QString selected =
            QFileDialog::getExistingDirectory(dialog, QStringLiteral("Select directory"), edit->text());
        if (!selected.isEmpty())
        {
            edit->setText(selected);
        }
    });
    form->addRow(field.Label, row);
    return edit;
}
} // namespace

void MainWindow::OnDebuggerSetupRequested()
{
    if (!Controller_->Caps().CanBuildDebug)
    {
        return;
    }
    const uint64 epoch = Controller_->State().ProjectEpoch;
    const QString digest = Controller_->State().SavedDigest;
    auto* dialog =
        new QMessageBox(QMessageBox::Information,
                        QStringLiteral("Set Up RAD Debugger"),
                        QStringLiteral("RAD is unavailable. Set Up downloads and builds the pinned debugger locally, "
                                       "then continues debugging. Missing system dependencies are listed in Output. "
                                       "You can also choose an existing RAD executable."),
                        QMessageBox::Cancel,
                        this);
    dialog->setObjectName(QStringLiteral("debuggerSetupDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    auto* setup = dialog->addButton(QStringLiteral("Set Up RAD"), QMessageBox::AcceptRole);
    setup->setObjectName(QStringLiteral("setupRadButton"));
    auto* choose = dialog->addButton(QStringLiteral("Choose Existing Installation..."), QMessageBox::ActionRole);
    choose->setObjectName(QStringLiteral("chooseRadButton"));
    dialog->setDefaultButton(QMessageBox::Cancel);
    connect(dialog, &QMessageBox::finished, this, [this, dialog, setup, choose, epoch, digest](int) {
        const auto current = [this, epoch, digest]() {
            return Controller_->Caps().CanBuildDebug && Controller_->State().ProjectEpoch == epoch &&
                   Controller_->State().SavedDigest == digest;
        };
        if (!current())
        {
            return;
        }
        if (dialog->clickedButton() == setup)
        {
            LaunchAfterPreview(LaunchAction::Debug, {}, true);
        }
        else if (dialog->clickedButton() == choose)
        {
            auto* picker = new QFileDialog(this, QStringLiteral("Choose RAD Executable"));
            picker->setAttribute(Qt::WA_DeleteOnClose);
            picker->setFileMode(QFileDialog::ExistingFile);
            connect(picker, &QFileDialog::fileSelected, this, [this, current](const QString& path) {
                if (current())
                {
                    LaunchAfterPreview(LaunchAction::Debug, path);
                }
            });
            picker->open();
        }
    });
    dialog->open();
}

void MainWindow::OnSetupProject()
{
    auto* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setObjectName(QStringLiteral("projectSetupDialog"));
    dialog->setWindowTitle(QStringLiteral("Initialize / Repair / Update Project Setup"));
    auto* form = new QFormLayout(dialog);
    auto* sdk = AddDirectoryField(form,
                                  dialog,
                                  {
                                      .Label = QStringLiteral("Native SDK"),
                                      .Hint = QStringLiteral("Installed SDK prefix; blank keeps saved selection"),
                                  });
    sdk->setObjectName(QStringLiteral("setupSdk"));
    auto* web = AddDirectoryField(form,
                                  dialog,
                                  {
                                      .Label = QStringLiteral("Web SDK"),
                                      .Hint = QStringLiteral("Optional installed Web SDK; blank keeps saved selection"),
                                  });
    auto* prepare = new QCheckBox(QStringLiteral("Prepare tools and build/install the native engine SDK"), dialog);
    form->addRow(prepare);
    auto* note = new QLabel(
        QStringLiteral("Choose a different compatible SDK to update the project. Repair refreshes local CMake presets "
                       "and caches, then builds and runs tests. Custom presets and unrelated IDE settings are "
                       "preserved. Engine preparation may download dependencies from the trusted tooling checkout."),
        dialog);
    note->setWordWrap(true);
    form->addRow(note);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(dialog, &QDialog::accepted, this, [this, sdk, web, prepare]() {
        Controller_->SetupProject(sdk->text().trimmed(), web->text().trimmed(), prepare->isChecked());
    });
    dialog->open();
}

void MainWindow::OnNewProject()
{
    if (!Audio_->ConfirmDiscard())
    {
        return;
    }
    Audio_->StopPreview();
    auto* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setObjectName(QStringLiteral("newProjectDialog"));
    dialog->setWindowTitle(QStringLiteral("New Ludus Project"));
    auto* form = new QFormLayout(dialog);
    auto* name = new QLineEdit(dialog);
    name->setObjectName(QStringLiteral("newProjectName"));
    form->addRow(QStringLiteral("Name"), name);
    auto* destination = new QLineEdit(dialog);
    destination->setPlaceholderText(QStringLiteral("Full path to a new directory"));
    destination->setObjectName(QStringLiteral("newProjectDestination"));
    form->addRow(QStringLiteral("Destination"), destination);
    auto* sdk = AddDirectoryField(form,
                                  dialog,
                                  {
                                      .Label = QStringLiteral("Development SDK"),
                                      .Hint = QStringLiteral("Installed native Development SDK prefix"),
                                  });
    auto* prepare = new QCheckBox(QStringLiteral("Prepare tools and build/install the native engine SDK"), dialog);
    form->addRow(prepare);
    auto* note = new QLabel(
        QStringLiteral("Create the minimal native template and verify configure, build and tests before opening it. "
                       "The destination must be new. Engine preparation may download dependencies."),
        dialog);
    note->setWordWrap(true);
    form->addRow(note);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(dialog, &QDialog::accepted, this, [this, name, destination, sdk, prepare]() {
        ProjectCreationOptions creation;
        creation.Destination = destination->text().trimmed();
        creation.Name = name->text().trimmed();
        creation.Sdk = sdk->text().trimmed();
        creation.PrepareEngine = prepare->isChecked();
        Controller_->CreateProject(creation);
    });
    dialog->open();
}

void MainWindow::OnSetupRelease()
{
    auto* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("Set Up Releases"));
    auto* layout = new QFormLayout(dialog);
    auto* platform = new QComboBox(dialog);
    platform->addItem(QStringLiteral("Linux native"), QStringLiteral("linux-x64"));
    platform->addItem(QStringLiteral("Browser (Emscripten)"), QStringLiteral("web"));
    auto* target = new QLineEdit(dialog);
    target->setPlaceholderText(QStringLiteral("username/game (optional)"));
    auto* note = new QLabel(
        QStringLiteral("Save release files into this project's repository. Setup also adds "
                       "a GitHub workflow for v* tags and manual releases. Set ITCH_IO_TARGET on GitHub if left blank. "
                       "Add BUTLER_API_KEY in the "
                       "itch-release environment on GitHub. Setup never uploads or overwrites existing files."),
        dialog);
    note->setWordWrap(true);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    layout->addRow(QStringLiteral("Platform"), platform);
    layout->addRow(QStringLiteral("itch.io project"), target);
    layout->addRow(note);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(dialog, &QDialog::accepted, this, [this, platform, target]() {
        Controller_->SetupRelease(platform->currentData().toString(), target->text().trimmed());
    });
    dialog->open();
}

void MainWindow::OnPackageRelease()
{
    auto* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("Package Release"));
    auto* layout = new QFormLayout(dialog);
    auto* profile = new QComboBox(dialog);
    profile->setEditable(true);
    profile->addItem(QStringLiteral("linux-release"));
    profile->addItem(QStringLiteral("web-release"));
    auto* version = new QLineEdit(QStringLiteral("0.1.0"), dialog);
    auto* sdk = new QLineEdit(dialog);
    sdk->setPlaceholderText(QStringLiteral("Optional Release SDK prefix"));
    auto* note = new QLabel(
        QStringLiteral("Build and validate a player package. The package directory appears in Output. "
                       "Browser projects need a configured web-emscripten-release preset. Packaging never uploads."),
        dialog);
    note->setWordWrap(true);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    layout->addRow(QStringLiteral("Package profile"), profile);
    layout->addRow(QStringLiteral("Version"), version);
    layout->addRow(QStringLiteral("SDK"), sdk);
    layout->addRow(note);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(dialog, &QDialog::accepted, this, [this, profile, version, sdk]() {
        Controller_->PackageRelease(profile->currentText(), version->text().trimmed(), sdk->text().trimmed());
    });
    dialog->open();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (!AudioClosing_)
    {
        if (!Audio_->ConfirmDiscard())
        {
            event->ignore();
            return;
        }
        const bool busy = !Controller_->Caps().CanCloseImmediately;
        if (busy && !CloseConfirmed_)
        {
            const auto choice = QMessageBox::question(
                this,
                QStringLiteral("Operation running"),
                QStringLiteral("An operation is running. Stop it and close, or keep the editor open?"),
                QMessageBox::Close | QMessageBox::Cancel);
            if (choice != QMessageBox::Close)
            {
                event->ignore();
                return;
            }
            CloseConfirmed_ = true;
        }
        AudioClosing_ = true;
        Audio_->ShutdownPreview();
        if (busy)
        {
            (void)Controller_->RequestClose();
        }
    }
    if (!Audio_->PreviewFinished())
    {
        event->ignore();
        QTimer::singleShot(20, this, [this]() { close(); });
        return;
    }
    if (Controller_->Caps().CanCloseImmediately)
    {
        event->accept();
        return;
    }
    event->ignore();
}

void MainWindow::RenderProperties()
{
    const auto& snapshot = Controller_->PlayState();
    const QString stamp = QString::fromUtf8(QJsonDocument(snapshot.Properties).toJson(QJsonDocument::Compact));
    if (stamp == PropertiesStamp_)
    {
        return;
    }
    PropertiesStamp_ = stamp;
    const QSignalBlocker blocker(Properties_);
    Properties_->setRowCount(static_cast<int>(snapshot.Properties.size()));
    for (int row = 0; row < snapshot.Properties.size(); ++row)
    {
        const auto property = snapshot.Properties.at(row).toObject();
        const int kind = property.value(QStringLiteral("kind")).toInt(-1);
        QString text;
        if (kind == 2)
        {
            const auto bits = static_cast<foundation::uint32>(property.value(QStringLiteral("bits")).toInteger());
            foundation::float32 value = 0.0F;
            std::memcpy(&value, &bits, sizeof(value));
            text = QString::number(static_cast<foundation::float64>(value), 'g', 9);
        }
        else if (kind == 0)
        {
            text = property.value(QStringLiteral("value")).toBool() ? QStringLiteral("true") : QStringLiteral("false");
        }
        else if (kind == 4)
        {
            text = property.value(QStringLiteral("value")).toString();
        }
        else
        {
            text = QString::number(property.value(QStringLiteral("value")).toInteger());
        }
        auto* name = new QTableWidgetItem(property.value(QStringLiteral("label")).toString());
        auto* value = new QTableWidgetItem(text);
        const bool writable = property.value(QStringLiteral("writable")).toBool();
        if (!writable)
        {
            value->setFlags(value->flags() & ~Qt::ItemIsEditable);
        }
        name->setFlags(name->flags() & ~Qt::ItemIsEditable);
        auto* scope = new QTableWidgetItem(property.value(QStringLiteral("scope")).toInt() == 1
                                               ? QStringLiteral("Persistable")
                                               : QStringLiteral("Session/output"));
        scope->setFlags(scope->flags() & ~Qt::ItemIsEditable);
        Properties_->setItem(row, 0, name);
        Properties_->setItem(row, 1, value);
        Properties_->setItem(row, 2, scope);
    }
}

} // namespace ludus::editor
