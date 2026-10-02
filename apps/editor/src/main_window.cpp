#include "internal/main_window.h"

#include <QAbstractItemModel>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QKeySequence>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
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
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

namespace ludus::editor
{

MainWindow::MainWindow(EditorController* controller, QWidget* parent)
    : QMainWindow(parent), Controller_(controller)
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

    // --- Output panel (read-only) ---
    Output_ = new QPlainTextEdit(splitter);
    Output_->setReadOnly(true);
    Output_->setMaximumBlockCount(5000);

    splitter->addWidget(form);
    splitter->addWidget(RuntimeLabel_);
    splitter->addWidget(Output_);
    splitter->setStretchFactor(2, 1);

    StatusLabel_ = new QLabel(central);
    layout->addWidget(splitter);
    layout->addWidget(StatusLabel_);
    setCentralWidget(central);

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
    SaveAction_ = fileMenu->addAction(QStringLiteral("&Save"));
    SaveAction_->setShortcut(QKeySequence::Save);
    ReloadAction_ = fileMenu->addAction(QStringLiteral("&Reload"));

    QMenu* buildMenu = menuBar()->addMenu(QStringLiteral("&Build"));
    ConfigureAction_ = buildMenu->addAction(QStringLiteral("&Configure / Refresh Targets"));
    BuildAction_ = buildMenu->addAction(QStringLiteral("&Build"));
    BuildRunAction_ = buildMenu->addAction(QStringLiteral("Build and &Run"));
    StopAction_ = buildMenu->addAction(QStringLiteral("&Stop"));

    QMenu* outputMenu = menuBar()->addMenu(QStringLiteral("&Output"));
    ClearAction_ = outputMenu->addAction(QStringLiteral("&Clear Output"));
    CopyAction_ = outputMenu->addAction(QStringLiteral("Copy &Job Details"));

    connect(OpenAction_, &QAction::triggered, this, &MainWindow::OnOpenRequested);
    connect(SaveAction_, &QAction::triggered, this, &MainWindow::OnSaveRequested);
    connect(ReloadAction_, &QAction::triggered, this, &MainWindow::OnReloadRequested);
    connect(ConfigureAction_, &QAction::triggered, Controller_, &EditorController::Configure);
    connect(BuildAction_, &QAction::triggered, Controller_, &EditorController::Build);
    connect(BuildRunAction_, &QAction::triggered, Controller_, &EditorController::BuildRun);
    connect(StopAction_, &QAction::triggered, Controller_, &EditorController::Stop);
    connect(ClearAction_, &QAction::triggered, Controller_, &EditorController::ClearOutput);
    connect(CopyAction_, &QAction::triggered, this, &MainWindow::OnCopyJobDetails);
}

ProjectDescriptor MainWindow::DraftFromFields() const
{
    ProjectDescriptor draft = Controller_->State().Draft;
    draft.Name = NameEdit_->text();
    draft.ProviderKind =
        ProviderBox_->currentText() == QStringLiteral("cmake") ? Provider::Cmake : Provider::Ludus;
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
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Open Project Descriptor"),
                                                      QString(), QStringLiteral("Ludus Project (*.json)"));
    if (path.isEmpty())
    {
        return;
    }
    // Dirty-document confirmation is offered asynchronously via a message box.
    if (Controller_->State().Dirty() && Controller_->State().Document == DocumentState::ProjectLoaded)
    {
        const auto choice = QMessageBox::question(
            this, QStringLiteral("Unsaved changes"),
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

void MainWindow::OnSaveRequested() { Controller_->Save(); }

void MainWindow::OnReloadRequested()
{
    if (Controller_->State().Dirty())
    {
        const auto choice = QMessageBox::question(
            this, QStringLiteral("Unsaved changes"),
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
    RenderCapabilities();
    RenderStatus();

    // If a close was requested and the workspace is now closeable, finish.
    if (CloseConfirmed_ && ComputeCapabilities(Controller_->State()).CanCloseImmediately)
    {
        close();
    }
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
    connect(ArgsList_->model(), &QAbstractItemModel::dataChanged, this, &MainWindow::OnFieldEdited,
            Qt::UniqueConnection);

    // Render output and runtime status.
    Output_->setPlainText(Controller_->Log().Text());
    Rendering_ = false;
}

void MainWindow::RenderCapabilities()
{
    const Capabilities caps = ComputeCapabilities(Controller_->State());
    OpenAction_->setEnabled(caps.CanOpen);
    SaveAction_->setEnabled(caps.CanSave);
    ReloadAction_->setEnabled(caps.CanReload);
    ConfigureAction_->setEnabled(caps.CanConfigure);
    BuildAction_->setEnabled(caps.CanBuild);
    BuildRunAction_->setEnabled(caps.CanBuildRun);
    StopAction_->setEnabled(caps.CanStop);
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
    case Phase::Idle: phase = QStringLiteral("Idle"); break;
    case Phase::Starting: phase = QStringLiteral("Starting"); break;
    case Phase::Configuring: phase = QStringLiteral("Configuring"); break;
    case Phase::Building: phase = QStringLiteral("Building"); break;
    case Phase::Launching: phase = QStringLiteral("Launching"); break;
    case Phase::Running: phase = QStringLiteral("Running"); break;
    case Phase::Stopping: phase = QStringLiteral("Stopping"); break;
    case Phase::CleanupUnknown: phase = QStringLiteral("Cleanup unknown"); break;
    }

    QString status = phase;
    if (state.Result.Kind != Outcome::None)
    {
        status += QStringLiteral("  —  last result: %1")
                      .arg(QString::fromLatin1(ResultCodeName(state.Result.Code)));
        if (!state.Result.Message.isEmpty())
        {
            status += QStringLiteral(" (%1)").arg(state.Result.Message);
        }
    }
    if (state.OperationPhase == Phase::CleanupUnknown)
    {
        status += QStringLiteral("  —  operator recovery required; see Copy Job Details");
    }
    StatusLabel_->setText(status);

    if (state.OperationPhase == Phase::Running)
    {
        RuntimeLabel_->setText(QStringLiteral(
            "The application is running in its own window. This status area describes it; "
            "it does not embed the game's framebuffer."));
    }
    else
    {
        RuntimeLabel_->setText(QStringLiteral("No runtime."));
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (ComputeCapabilities(Controller_->State()).CanCloseImmediately)
    {
        event->accept();
        return;
    }
    // Busy: offer Stop and Close or Keep Open. Do not block on a nested loop
    // while output flows; the actual close happens when cleanup is confirmed.
    const auto choice = QMessageBox::question(
        this, QStringLiteral("Operation running"),
        QStringLiteral("An operation is running. Stop it and close, or keep the editor open?"),
        QMessageBox::Close | QMessageBox::Cancel);
    if (choice == QMessageBox::Close)
    {
        CloseConfirmed_ = true;
        Controller_->RequestClose(); // begins asynchronous cancellation
    }
    else
    {
        CloseConfirmed_ = false; // a close request is cancellable
    }
    event->ignore();
}

} // namespace ludus::editor
