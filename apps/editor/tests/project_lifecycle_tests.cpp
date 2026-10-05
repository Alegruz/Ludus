#include "internal/configuration_workspace.h"
#include "internal/controller.h"
#include "internal/main_window.h"
#include "internal/project_creation_dialog.h"

#include <catch2/catch_test_macros.hpp>

#include <QAction>
#include <QApplication>
#include <QColor>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QKeySequence>
#include <QMessageBox>
#include <QPalette>
#include <QPixmap>
#include <QPushButton>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace ludus::editor;

namespace
{
QString Project(QTemporaryDir& directory)
{
    QFile file(directory.filePath(QStringLiteral("ludus.project.json")));
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write(QByteArrayLiteral(R"json({"version":1,"name":"Demo","provider":"cmake",
"source_dir":".","preset":"linux-clang-debug","target":"app","run":{"cwd":".","args":[]}})json"));
    return file.fileName();
}
} // namespace

TEST_CASE("Close Project retires identity and returns to Welcome with recent history", "[editor][lifecycle]")
{
    QTemporaryDir directory;
    EditorController controller({}, nullptr, directory.filePath(QStringLiteral("recent.json")));
    MainWindow window(&controller, nullptr, directory.filePath(QStringLiteral("workspace.json")));
    window.show();
    auto* tabs = window.findChild<QTabWidget*>();
    auto* welcome = window.findChild<QWidget*>(QStringLiteral("projectWelcome"));
    auto* close = window.findChild<QAction*>(QStringLiteral("closeProjectAction"));
    auto* configuration = window.findChild<ConfigurationWorkspace*>(QStringLiteral("configurationWorkspace"));
    REQUIRE(tabs != nullptr);
    REQUIRE(welcome != nullptr);
    REQUIRE(close != nullptr);
    REQUIRE(configuration != nullptr);
    CHECK(tabs->isTabVisible(tabs->indexOf(configuration)));
    CHECK(tabs->currentWidget() == welcome);
    CHECK_FALSE(close->isEnabled());
    const auto path = Project(directory);
    controller.OpenProject(path);
    QApplication::processEvents();
    CHECK_FALSE(welcome->isVisible());
    CHECK(close->isEnabled());
    REQUIRE(configuration->Edit("host.headless", QStringLiteral("true")));
    tabs->setCurrentWidget(configuration);
    const auto epoch = controller.State().ProjectEpoch;
    const auto nextJob = controller.State().NextJob;
    QTest::keyClick(&window, Qt::Key_W, Qt::ControlModifier | Qt::ShiftModifier);
    QApplication::processEvents();
    CHECK(controller.State().Document == DocumentState::NoProject);
    CHECK(controller.State().DescriptorPath.isEmpty());
    CHECK(controller.State().ProjectEpoch == epoch + 1);
    CHECK(controller.State().NextJob == nextJob);
    CHECK(controller.State().SetupStatus.isEmpty());
    CHECK(controller.RecentProjects().first().DescriptorPath == path);
    CHECK(welcome->isVisible());
    CHECK(tabs->currentWidget() == welcome);
    CHECK(tabs->isTabVisible(tabs->indexOf(configuration)));
    CHECK(configuration->Dirty()); // The separately loaded preview remains accessible.
    controller.OpenProject(path);
    CHECK(controller.State().Saved.Name == QStringLiteral("Demo"));
}

TEST_CASE("Close Project confirms dirty settings and preserves them on Cancel or failed Save", "[editor][lifecycle]")
{
    QTemporaryDir directory;
    EditorController controller({}, nullptr, directory.filePath(QStringLiteral("recent.json")));
    const auto path = Project(directory);
    controller.OpenProject(path);
    MainWindow window(&controller, nullptr, directory.filePath(QStringLiteral("workspace.json")));
    window.show();
    auto draft = controller.State().Draft;
    draft.Name = QStringLiteral("Changed");
    controller.EditDraft(draft);
    CHECK_FALSE(controller.CloseProject()); // API never silently discards.
    auto choice = QMessageBox::Cancel;
    bool closed = false;
    SECTION("Cancel") {}
    SECTION("Discard")
    {
        choice = QMessageBox::Discard;
        closed = true;
    }
    SECTION("Save")
    {
        choice = QMessageBox::Save;
        closed = true;
    }
    SECTION("Failed Save")
    {
        choice = QMessageBox::Save;
        ProjectStore::FaultHooks hooks;
        hooks.FailCommit = []() { return true; };
        controller.Store().SetFaultHooks(hooks);
    }
    bool prompted = false;
    QTimer::singleShot(0, &window, [&prompted, choice]() {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
        {
            prompted = true;
            box->button(choice)->click();
        }
    });
    window.findChild<QAction*>(QStringLiteral("closeProjectAction"))->trigger();
    CHECK(prompted);
    CHECK((controller.State().Document == DocumentState::NoProject) == closed);
    if (!closed)
    {
        CHECK(controller.State().Draft.Name == QStringLiteral("Changed"));
        CHECK(controller.State().Dirty());
    }
    ProjectStore store;
    CHECK(store.Load(path).Parse.Descriptor.Name ==
          (closed && choice == QMessageBox::Save ? QStringLiteral("Changed") : QStringLiteral("Demo")));
}

TEST_CASE("Project switching is unavailable while an operation owns the workspace", "[editor][lifecycle]")
{
    WorkspaceState state;
    state.Document = DocumentState::ProjectLoaded;
    state.HasSaved = true;
    CHECK(ComputeCapabilities(state).CanCloseProject);
    for (const auto phase : {Phase::Building, Phase::Running, Phase::Stopping, Phase::CleanupUnknown})
    {
        state.OperationPhase = phase;
        CHECK_FALSE(ComputeCapabilities(state).CanCloseProject);
    }
}

TEST_CASE("New shortcut opens the creation dialog and Escape cancels without a job", "[editor][lifecycle]")
{
    QTemporaryDir directory;
    EditorController controller({}, nullptr, directory.filePath(QStringLiteral("recent.json")));
    MainWindow window(&controller, nullptr, directory.filePath(QStringLiteral("workspace.json")));
    window.show();
    QApplication::processEvents();
    QTest::keyClick(&window, Qt::Key_N, Qt::ControlModifier);
    QApplication::processEvents();
    auto* dialog = window.findChild<QDialog*>(QStringLiteral("newProjectDialog"));
    REQUIRE(dialog != nullptr);
    REQUIRE(dialog->isVisible());
    QTest::keyClick(dialog, Qt::Key_Escape);
    QApplication::processEvents();
    CHECK(controller.State().Document == DocumentState::NoProject);
    CHECK(controller.State().ActiveJob == 0);
}

TEST_CASE("Project creation dark and light captures support visual review", "[editor][visual]")
{
    const auto capture = qEnvironmentVariable("LUDUS_EDITOR_CREATION_CAPTURE");
    if (capture.isEmpty())
    {
        return;
    }
    const auto original = QApplication::palette();
    for (const bool dark : {false, true})
    {
        auto palette = original;
        if (dark)
        {
            palette.setColor(QPalette::Window, QColor(QStringLiteral("#282c30")));
            palette.setColor(QPalette::Base, QColor(QStringLiteral("#202428")));
            palette.setColor(QPalette::Button, QColor(QStringLiteral("#343a40")));
            for (const auto role : {QPalette::Text, QPalette::WindowText, QPalette::ButtonText})
            {
                palette.setColor(role, QColor(QStringLiteral("#edf1f5")));
            }
            palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#167b83")));
        }
        QApplication::setPalette(palette);
        QTemporaryDir directory;
        EditorController controller({}, nullptr, directory.filePath(QStringLiteral("recent.json")));
        MainWindow window(&controller, nullptr, directory.filePath(QStringLiteral("workspace.json")));
        window.resize(900, 740);
        window.show();
        ProjectCreationDialog dialog(&window);
        dialog.show();
        QApplication::processEvents();
        REQUIRE(window.grab().save(
            capture + (dark ? QStringLiteral("-dark-workspace.png") : QStringLiteral("-light-workspace.png"))));
        REQUIRE(dialog.grab().save(capture + (dark ? QStringLiteral("-dark.png") : QStringLiteral("-light.png"))));
    }
    QApplication::setPalette(original);
}
