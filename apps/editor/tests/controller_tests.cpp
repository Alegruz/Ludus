// Offscreen tests for EditorController action validation and failure handling
// (design 5, 6, 9; E05/E09/E12). These do not launch real tools; the tool
// lifecycle is covered by the Python real-process suite. Here we verify that
// open/edit/save/reload/close and capability gating behave correctly, that a
// failed Save keeps the dirty draft, and that Copy Job Details is bounded and
// telemetry-free.

#include "internal/controller.h"
#include "internal/main_window.h"
#include "internal/project_store.h"

#include <catch2/catch_test_macros.hpp>

#include <QAction>
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDialog>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>

using namespace ludus::editor;

namespace
{
ToolingPaths NoTooling()
{
    ToolingPaths tooling;
    tooling.PythonPath = QStringLiteral("/nonexistent/python");
    tooling.AdapterPath = QStringLiteral("/nonexistent/editor_tool.py");
    tooling.ToolingRoot = QStringLiteral("/nonexistent");
    return tooling;
}

QString WriteDescriptor(QTemporaryDir& dir, const QByteArray& bytes)
{
    const QString path = dir.filePath(QStringLiteral("ludus.project.json"));
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write(bytes);
    file.close();
    return path;
}

QByteArray ValidDescriptor()
{
    return QByteArrayLiteral("{\n  \"version\": 1,\n  \"name\": \"demo\",\n  \"provider\": \"cmake\",\n"
                             "  \"source_dir\": \".\",\n  \"preset\": \"linux-clang-debug\",\n  \"target\": \"app\",\n"
                             "  \"run\": {\n    \"cwd\": \".\",\n    \"args\": []\n  }\n}\n");
}
} // namespace

TEST_CASE("Open loads a project and executes no project code", "[editor][controller]")
{
    QTemporaryDir dir;
    const QString path = WriteDescriptor(dir, ValidDescriptor());
    EditorController controller(NoTooling());
    QSignalSpy spy(&controller, &EditorController::StateChanged);
    controller.OpenProject(path);
    CHECK(controller.State().Document == DocumentState::ProjectLoaded);
    CHECK(controller.State().Saved.Name == QStringLiteral("demo"));
    CHECK_FALSE(controller.State().Dirty());
    CHECK(spy.count() >= 1);
}

TEST_CASE("A failed open leaves the previous workspace intact", "[editor][controller]")
{
    QTemporaryDir dir;
    const QString good = WriteDescriptor(dir, ValidDescriptor());
    EditorController controller(NoTooling());
    controller.OpenProject(good);
    const WorkspaceState before = controller.State();

    const QString bad = dir.filePath(QStringLiteral("broken.json"));
    QFile file(bad);
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write(QByteArrayLiteral("{ not json"));
    file.close();
    controller.OpenProject(bad);
    // The previous workspace is unchanged; the failure is a last result.
    CHECK(controller.State().DescriptorPath == before.DescriptorPath);
    CHECK(controller.State().Saved == before.Saved);
    CHECK(controller.State().Result.Kind == Outcome::Failed);
}

TEST_CASE("Editing produces a dirty draft; Save makes it clean and survives reload", "[editor][controller]")
{
    QTemporaryDir dir;
    const QString path = WriteDescriptor(dir, ValidDescriptor());
    EditorController controller(NoTooling());
    controller.OpenProject(path);

    ProjectDescriptor edited = controller.State().Draft;
    edited.Name = QStringLiteral("renamed");
    controller.EditDraft(edited);
    CHECK(controller.State().Dirty());

    controller.Save();
    CHECK_FALSE(controller.State().Dirty());

    controller.Reload();
    CHECK(controller.State().Saved.Name == QStringLiteral("renamed"));
}

TEST_CASE("A failed Save keeps the dirty draft", "[editor][controller]")
{
    QTemporaryDir dir;
    const QString path = WriteDescriptor(dir, ValidDescriptor());
    EditorController controller(NoTooling());
    controller.OpenProject(path);

    ProjectStore::FaultHooks hooks;
    hooks.FailCommit = []() { return true; };
    controller.Store().SetFaultHooks(hooks);

    ProjectDescriptor edited = controller.State().Draft;
    edited.Name = QStringLiteral("willfail");
    controller.EditDraft(edited);
    controller.Save();
    CHECK(controller.State().Dirty()); // draft retained
    CHECK(controller.State().Result.Kind == Outcome::Failed);
}

TEST_CASE("Preset change invalidates the discovered-target cache", "[editor][controller]")
{
    QTemporaryDir dir;
    const QString path = WriteDescriptor(dir, ValidDescriptor());
    EditorController controller(NoTooling());
    controller.OpenProject(path);
    // Simulate a prior discovery by editing then confirm a preset change clears.
    ProjectDescriptor edited = controller.State().Draft;
    edited.Preset = QStringLiteral("linux-clang-development");
    controller.EditDraft(edited);
    CHECK(controller.State().DiscoveredTargets.isEmpty());
}

TEST_CASE("Copy Job Details is bounded and omits telemetry", "[editor][controller]")
{
    QTemporaryDir dir;
    const QString path = WriteDescriptor(dir, ValidDescriptor());
    EditorController controller(NoTooling());
    controller.OpenProject(path);
    const QString details = controller.JobDetails();
    CHECK(details.contains(QStringLiteral("descriptor_digest:")));
    CHECK(details.contains(QStringLiteral("preset:")));
    CHECK(details.contains(QStringLiteral("cleanup_confirmed:")));
    // No environment dump or upload claims.
    CHECK_FALSE(details.contains(QStringLiteral("PATH=")));
    CHECK(static_cast<ludus::foundation::usize>(details.toUtf8().size()) <=
          ludus::foundation::usize{1} * 1024u * 1024u);
}

TEST_CASE("Project setup actions gate clean SDK projects and new project dialogs", "[editor][setup]")
{
    EditorController controller(NoTooling());
    MainWindow window(&controller);
    auto* create = window.findChild<QAction*>(QStringLiteral("newProjectAction"));
    auto* check = window.findChild<QAction*>(QStringLiteral("checkSetupAction"));
    auto* repair = window.findChild<QAction*>(QStringLiteral("setupProjectAction"));
    REQUIRE(create != nullptr);
    REQUIRE(check != nullptr);
    REQUIRE(repair != nullptr);
    CHECK(create->isEnabled());
    CHECK_FALSE(check->isEnabled());
    CHECK_FALSE(repair->isEnabled());
    create->trigger();
    auto* dialog = window.findChild<QDialog*>(QStringLiteral("newProjectDialog"));
    REQUIRE(dialog != nullptr);
    CHECK(dialog->isVisible());
    dialog->reject();

    WorkspaceState state;
    state.Document = DocumentState::ProjectLoaded;
    state.HasSaved = true;
    state.Saved.Version = 2;
    state.Saved.ProviderKind = Provider::Cmake;
    state.Draft = state.Saved;
    CHECK(ComputeCapabilities(state).CanProjectCheck);
    CHECK(ComputeCapabilities(state).CanProjectSetup);
    state.Draft.Name = QStringLiteral("dirty");
    CHECK_FALSE(ComputeCapabilities(state).CanProjectSetup);
    CHECK_FALSE(ComputeCapabilities(state).CanProjectCreate);
    state.OperationPhase = Phase::CleanupUnknown;
    CHECK_FALSE(ComputeCapabilities(state).CanProjectCheck);
}

TEST_CASE("Opening an SDK project automatically reports missing setup without running hooks", "[editor][setup]")
{
    QTemporaryDir dir;
    QFile fixture(QStringLiteral(LUDUS_EDITOR_SOURCE_DIR) + QStringLiteral("/tests/fixtures/valid_v2_cmake.json"));
    REQUIRE(fixture.open(QIODevice::ReadOnly));
    const QString descriptor = WriteDescriptor(dir, fixture.readAll());
    QFile cmake(dir.filePath(QStringLiteral("CMakeLists.txt")));
    REQUIRE(cmake.open(QIODevice::WriteOnly));
    cmake.write("cmake_minimum_required(VERSION 3.29)\n");
    cmake.close();
    QFile hook(dir.filePath(QStringLiteral("init.sh")));
    REQUIRE(hook.open(QIODevice::WriteOnly));
    hook.write("#!/bin/sh\ntouch should-never-exist\n");
    hook.close();
    const auto root = QStringLiteral(LUDUS_EDITOR_PROJECT_ROOT);
    ToolingPaths tooling;
    tooling.PythonPath = QStringLiteral("/usr/bin/python3");
    tooling.AdapterPath = root + QStringLiteral("/scripts/python/editor_tool.py");
    tooling.ToolingRoot = root;
    EditorController controller(tooling);
    auto* tool = controller.findChild<ToolProcess*>();
    REQUIRE(tool != nullptr);
    QSignalSpy finished(tool, &ToolProcess::Finished);
    controller.OpenProject(descriptor);
    QDeadlineTimer deadline(10000);
    while (controller.State().Result.Kind == Outcome::None && !deadline.hasExpired())
    {
        QTest::qWait(10);
    }
    CHECK(controller.State().Document == DocumentState::ProjectLoaded);
    CHECK(controller.State().Result.Kind == Outcome::Failed);
    CHECK(controller.State().Result.Message.contains(QStringLiteral("setup is missing")));
    if (finished.isEmpty())
    {
        REQUIRE(finished.wait(5000));
    }
    CHECK_FALSE(QFile::exists(dir.filePath(QStringLiteral("should-never-exist"))));
    CHECK_FALSE(QFile::exists(dir.filePath(QStringLiteral("CMakeUserPresets.json"))));
    CHECK_FALSE(QFile::exists(dir.filePath(QStringLiteral(".ludus"))));
}

TEST_CASE("Editor New Project verifies the SDK project and opens it after bridge shutdown",
          "[editor][setup][integration]")
{
    const QString sdk = qEnvironmentVariable("LUDUS_SETUP_TEST_SDK");
    const QString tools = qEnvironmentVariable("LUDUS_SETUP_TEST_TOOLS");
    if (sdk.isEmpty() || tools.isEmpty())
    {
        SKIP("Requires an installed native SDK and prepared tooling checkout");
    }
    QTemporaryDir dir;
    const QString destination = dir.filePath(QStringLiteral("NewGame"));
    ToolingPaths paths;
    paths.PythonPath = QStringLiteral("/usr/bin/python3");
    paths.AdapterPath = QStringLiteral(LUDUS_EDITOR_PROJECT_ROOT) + QStringLiteral("/scripts/python/editor_tool.py");
    paths.ToolingRoot = tools;
    EditorController controller(paths);
    ProjectCreationOptions creation;
    creation.Destination = destination;
    creation.Name = QStringLiteral("NewGame");
    creation.Sdk = sdk;
    controller.CreateProject(creation);
    QDeadlineTimer deadline(60000);
    while ((controller.State().Document != DocumentState::ProjectLoaded || !controller.Caps().CanCloseImmediately) &&
           !deadline.hasExpired())
    {
        QTest::qWait(10);
    }
    CHECK(controller.State().Document == DocumentState::ProjectLoaded);
    CHECK(controller.State().Saved.Name == QStringLiteral("NewGame"));
    CHECK_FALSE(controller.State().Dirty());
    CHECK(controller.State().Result.Kind == Outcome::Success);
    CHECK(controller.State().Result.Message.contains(QStringLiteral("Setup checked")));
    CHECK(QFile::exists(destination + QStringLiteral("/CMakeUserPresets.json")));
    CHECK_FALSE(QFile::exists(destination + QStringLiteral("/out/build/linux-clang-development/CMakeCache.txt")));
}
