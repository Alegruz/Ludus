#include "internal/audio_workspace.h"
#include "internal/configuration_workspace.h"
#include "internal/content_browser.h"
#include "internal/main_window.h"
#include "internal/script_workspace.h"

#include <ludus/foundation/base/types.h>
#include <ludus/foundation/logging/category.hpp>
#include <ludus/foundation/logging/log.hpp>

#include <QAbstractItemView>
#include <QAction>
#include <QByteArray>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSizePolicy>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

namespace ludus::editor
{
namespace
{
constexpr foundation::int32 LAYOUT_VERSION = 2;
constexpr foundation::int64 MAX_SETTINGS_BYTES = foundation::int64{64} * 1024;
constexpr foundation::int32 MAX_STATE_BYTES = 32 * 1024;
LUDUS_DEFINE_LOG_CATEGORY(LOG_EDITOR_LAYOUT, "Editor.Layout");
} // namespace

void MainWindow::BuildUi()
{
    WorkTabs_ = new QTabWidget(this);
    WorkTabs_->setObjectName(QStringLiteral("workspaceTabs"));
    WorkTabs_->setAccessibleName(QStringLiteral("Authoring work areas"));
    setCentralWidget(WorkTabs_);
    auto* projectScroll = new QScrollArea(WorkTabs_);
    ProjectSettings_ = projectScroll;
    projectScroll->setObjectName(QStringLiteral("projectSettingsScroll"));
    projectScroll->setWidgetResizable(true);
    projectScroll->setFrameShape(QFrame::NoFrame);

    // --- Project settings form ---
    auto* form = new QWidget(projectScroll);
    auto* formLayout = new QFormLayout(form);
    NameEdit_ = new QLineEdit(form);
    NameEdit_->setObjectName(QStringLiteral("projectName"));
    NameEdit_->setAccessibleName(QStringLiteral("Project name"));
    ProviderBox_ = new QComboBox(form);
    ProviderBox_->addItem(QStringLiteral("ludus"));
    ProviderBox_->addItem(QStringLiteral("cmake"));
    SourceDirEdit_ = new QLineEdit(form);
    SourceDirEdit_->setObjectName(QStringLiteral("projectSource"));
    PresetBox_ = new QComboBox(form);
    PresetBox_->addItem(QStringLiteral("linux-clang-debug"));
    PresetBox_->addItem(QStringLiteral("linux-clang-development"));
    PresetBox_->addItem(QStringLiteral("linux-clang-release"));
    PresetBox_->addItem(QStringLiteral("macos-clang-debug"));
    PresetBox_->addItem(QStringLiteral("macos-clang-development"));
    PresetBox_->addItem(QStringLiteral("macos-clang-release"));
    TargetBox_ = new QComboBox(form);
    TargetBox_->setObjectName(QStringLiteral("projectTarget"));
    TargetBox_->setEditable(true); // target can be entered before configuration
    CwdEdit_ = new QLineEdit(form);

    formLayout->addRow(QStringLiteral("&Name"), NameEdit_);
    formLayout->addRow(QStringLiteral("&Provider"), ProviderBox_);
    formLayout->addRow(QStringLiteral("&Source directory"), SourceDirEdit_);
    formLayout->addRow(QStringLiteral("Pre&set"), PresetBox_);
    formLayout->addRow(QStringLiteral("Executable &target"), TargetBox_);
    formLayout->addRow(QStringLiteral("Run &working directory"), CwdEdit_);

    // Argument list editor: one string per item, Add/Remove; never a shell line.
    ArgsList_ = new QListWidget(form);
    ArgsList_->setObjectName(QStringLiteral("projectArguments"));
    connect(ArgsList_->model(), &QAbstractItemModel::dataChanged, this, &MainWindow::OnFieldEdited);
    auto* argsButtons = new QWidget(form);
    auto* argsButtonsLayout = new QHBoxLayout(argsButtons);
    AddArgButton_ = new QPushButton(QStringLiteral("Add argument"), argsButtons);
    RemoveArgButton_ = new QPushButton(QStringLiteral("Remove argument"), argsButtons);
    argsButtonsLayout->addWidget(AddArgButton_);
    argsButtonsLayout->addWidget(RemoveArgButton_);
    argsButtonsLayout->addStretch();
    formLayout->addRow(QStringLiteral("Run &arguments"), ArgsList_);
    formLayout->addRow(QString(), argsButtons);

    projectScroll->setWidget(form);
    WorkTabs_->addTab(projectScroll, QStringLiteral("&Project settings"));
    Content_ = new ContentWorkspace(WorkTabs_);
    WorkTabs_->addTab(Content_, QStringLiteral("&Content"));
    auto* audioScroll = new QScrollArea(WorkTabs_);
    audioScroll->setObjectName(QStringLiteral("audioWorkspaceScroll"));
    audioScroll->setWidgetResizable(true);
    audioScroll->setFrameShape(QFrame::NoFrame);
    Audio_ = new AudioWorkspace(audioScroll);
    audioScroll->setWidget(Audio_);
    WorkTabs_->addTab(audioScroll, QStringLiteral("&Audio"));
    Scripts_ = new ScriptWorkspace(Controller_, WorkTabs_);
    WorkTabs_->addTab(Scripts_, QStringLiteral("&Scripts"));
    Configuration_ = new ConfigurationWorkspace(WorkTabs_);
    WorkTabs_->addTab(Configuration_, QStringLiteral("&Configuration"));

    InspectorDock_ = new QDockWidget(QStringLiteral("Live Inspector"), this);
    InspectorDock_->setObjectName(QStringLiteral("liveInspectorDock"));
    auto* inspector = new QWidget(InspectorDock_);
    auto* inspectorLayout = new QVBoxLayout(inspector);
    // --- Runtime status area (describes the running app; not a framebuffer) ---
    RuntimeLabel_ = new QLabel(inspector);
    RuntimeLabel_->setText(QStringLiteral("No runtime."));
    RuntimeLabel_->setWordWrap(true);
    Properties_ = new QTableWidget(inspector);
    Properties_->setColumnCount(3);
    Properties_->setHorizontalHeaderLabels(
        {QStringLiteral("Property"), QStringLiteral("Value"), QStringLiteral("Scope")});
    Properties_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    Properties_->setSelectionBehavior(QAbstractItemView::SelectRows);
    Properties_->setSelectionMode(QAbstractItemView::SingleSelection);
    ApplySessionButton_ = new QPushButton(QStringLiteral("Apply to session"), inspector);
    ApplySessionButton_->setToolTip(QStringLiteral("Apply the selected edited value to the running session."));
    connect(ApplySessionButton_, &QPushButton::clicked, this, [this]() {
        const auto row = Properties_->currentRow();
        if (row >= 0 && Properties_->item(row, 1) != nullptr)
        {
            Controller_->EditProperty(row, Properties_->item(row, 1)->text());
        }
    });
    ApplyDocumentButton_ = new QPushButton(QStringLiteral("Copy live value to tuning draft"), inspector);
    ApplyDocumentButton_->setToolTip(
        QStringLiteral("Copy the selected live value into the tuning draft; save separately."));
    connect(ApplyDocumentButton_, &QPushButton::clicked, this, [this]() {
        Controller_->ApplyLivePropertyToTuningDocument(Properties_->currentRow());
    });
    connect(Properties_, &QTableWidget::currentCellChanged, this, [this]() {
        ApplyDocumentButton_->setEnabled(Controller_->CanApplyToTuningDocument(Properties_->currentRow()));
    });

    Properties_->setObjectName(QStringLiteral("liveProperties"));
    Properties_->setAccessibleName(QStringLiteral("Live runtime properties"));
    inspectorLayout->addWidget(RuntimeLabel_);
    inspectorLayout->addWidget(Properties_, 1);
    inspectorLayout->addWidget(ApplySessionButton_);
    inspectorLayout->addWidget(ApplyDocumentButton_);
    InspectorDock_->setWidget(inspector);
    addDockWidget(Qt::RightDockWidgetArea, InspectorDock_);

    OutputDock_ = new QDockWidget(QStringLiteral("Output"), this);
    OutputDock_->setObjectName(QStringLiteral("outputDock"));
    Output_ = new QPlainTextEdit(OutputDock_);
    Output_->setObjectName(QStringLiteral("workspaceOutput"));
    Output_->setAccessibleName(QStringLiteral("Build and runtime output"));
    Output_->setReadOnly(true);
    Output_->setMaximumBlockCount(5000);
    OutputDock_->setWidget(Output_);
    addDockWidget(Qt::BottomDockWidgetArea, OutputDock_);

    StatusLabel_ = new QLabel(this);
    StatusLabel_->setObjectName(QStringLiteral("workspaceStatus"));
    StatusLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    statusBar()->addWidget(StatusLabel_, 1);

    // Thanks to David Lightbown, Designing the User Experience of Game
    // Development Tools (2015), ch. 5, pp. 80-86, 112-115: show task-relevant choices
    // progressively. Recents belong to Welcome and File, freeing authoring space.
    // https://www.uxofgametools.com/; docs/architecture/editor-design-review.md.
    Welcome_ = new QWidget(WorkTabs_);
    Welcome_->setObjectName(QStringLiteral("projectWelcome"));
    auto* recentPanel = Welcome_;
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
    recentLayout->setContentsMargins(24, 24, 24, 24);
    recentLayout->setSpacing(12);
    auto* title = new QLabel(QStringLiteral("Welcome to Ludus"), recentPanel);
    auto font = title->font();
    font.setBold(true);
    title->setFont(font);
    auto* intro = new QLabel(QStringLiteral("Create a project or continue where you left off."), recentPanel);
    intro->setWordWrap(true);
    WelcomeNewButton_ = new QPushButton(QStringLiteral("New Project..."), recentPanel);
    WelcomeNewButton_->setObjectName(QStringLiteral("welcomeNewProject"));
    recentLayout->addWidget(title);
    recentLayout->addWidget(intro);
    recentLayout->addWidget(WelcomeNewButton_);
    recentLayout->addWidget(new QLabel(QStringLiteral("Recent projects"), recentPanel));
    recentLayout->addWidget(RecentEmptyLabel_);
    recentLayout->addWidget(RecentList_);
    recentLayout->addWidget(RecentOpenButton_);
    recentLayout->addWidget(BrowseProjectButton_);
    WorkTabs_->insertTab(0, Welcome_, QStringLiteral("Welcome"));
    WorkTabs_->setCurrentWidget(Welcome_);
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

void MainWindow::InitializeWorkspace()
{
    resize(1100, 760);
    resizeDocks({InspectorDock_}, {300}, Qt::Horizontal);
    resizeDocks({OutputDock_}, {180}, Qt::Vertical);
    DefaultLayout_ = saveState(LAYOUT_VERSION);
    if (WorkspaceSettingsFile_.isEmpty())
    {
        const auto config = (QDir::isAbsolutePath(qEnvironmentVariable("XDG_CONFIG_HOME"))
                                 ? qEnvironmentVariable("XDG_CONFIG_HOME")
                                 : QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation));
        if (!config.isEmpty())
        {
            WorkspaceSettingsFile_ = QDir(config).filePath(QStringLiteral("Ludus/Editor/workspace.json"));
        }
    }
    QFile file(WorkspaceSettingsFile_);
    if (WorkspaceSettingsFile_.isEmpty() || !file.exists())
    {
        return;
    }
    // This local file is only for state generated by this editor. Never load a
    // layout blob from a project, downloaded file, or host protocol response.
    QByteArray bytes;
    if (file.open(QIODevice::ReadOnly) && file.size() <= MAX_SETTINGS_BYTES)
    {
        bytes = file.read(MAX_SETTINGS_BYTES + 1);
        if (file.error() != QFileDevice::NoError || bytes.size() > MAX_SETTINGS_BYTES)
        {
            bytes.clear();
        }
    }
    const auto document = QJsonDocument::fromJson(bytes);
    const auto object = document.object();
    const auto geometryEncoded = object.value(QStringLiteral("geometry")).toString().toLatin1();
    const auto stateEncoded = object.value(QStringLiteral("state")).toString().toLatin1();
    const auto geometry = QByteArray::fromBase64(geometryEncoded);
    const auto state = QByteArray::fromBase64(stateEncoded);
    const auto defaultGeometry = saveGeometry();
    const bool compatible = document.isObject() && object.value(QStringLiteral("version")).toInt() == LAYOUT_VERSION &&
                            object.value(QStringLiteral("qtVersion")).toString() == QString::fromLatin1(qVersion()) &&
                            !geometry.isEmpty() && geometry.size() <= 4096 && !state.isEmpty() &&
                            state.size() <= MAX_STATE_BYTES && geometry.toBase64() == geometryEncoded &&
                            state.toBase64() == stateEncoded;
    if (!compatible || !restoreGeometry(geometry) || !restoreState(state, LAYOUT_VERSION))
    {
        (void)restoreGeometry(defaultGeometry);
        ResetWorkspaceLayout();
        statusBar()->showMessage(QStringLiteral("Workspace layout was incompatible or invalid. Using defaults."),
                                 10000);
    }
}

void MainWindow::ResetWorkspaceLayout()
{
    (void)restoreState(DefaultLayout_, LAYOUT_VERSION);
    // Also recover a pane that was floating when the user requested reset.
    for (auto* dock : {InspectorDock_, OutputDock_})
    {
        dock->setFloating(false);
        dock->show();
    }
    GameToolbar_->show();
    resizeDocks({InspectorDock_}, {300}, Qt::Horizontal);
    resizeDocks({OutputDock_}, {180}, Qt::Vertical);
}

void MainWindow::SaveWorkspaceLayout() const
{
    if (WorkspaceSettingsFile_.isEmpty())
    {
        return;
    }
    const auto state = saveState(LAYOUT_VERSION);
    const auto geometry = saveGeometry();
    if (state.size() > MAX_STATE_BYTES || geometry.size() > 4096 ||
        !QDir().mkpath(QFileInfo(WorkspaceSettingsFile_).absolutePath()))
    {
        LUDUS_LOG_TEXT(LOG_EDITOR_LAYOUT, Warning, "Could not save workspace layout; document saves are unaffected.");
        return;
    }
    QJsonObject object;
    object.insert(QStringLiteral("version"), LAYOUT_VERSION);
    object.insert(QStringLiteral("qtVersion"), QString::fromLatin1(qVersion()));
    object.insert(QStringLiteral("geometry"), QString::fromLatin1(geometry.toBase64()));
    object.insert(QStringLiteral("state"), QString::fromLatin1(state.toBase64()));
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    QSaveFile file(WorkspaceSettingsFile_);
    file.setDirectWriteFallback(false);
    if (bytes.size() > MAX_SETTINGS_BYTES || !file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() ||
        !file.commit())
    {
        LUDUS_LOG_TEXT(LOG_EDITOR_LAYOUT, Warning, "Could not save workspace layout; document saves are unaffected.");
    }
}

} // namespace ludus::editor
