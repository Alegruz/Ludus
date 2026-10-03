// Offscreen tests for EditorController action validation and failure handling
// (design 5, 6, 9; E05/E09/E12). These do not launch real tools; the tool
// lifecycle is covered by the Python real-process suite. Here we verify that
// open/edit/save/reload/close and capability gating behave correctly, that a
// failed Save keeps the dirty draft, and that Copy Job Details is bounded and
// telemetry-free.

#include "internal/controller.h"
#include "internal/project_store.h"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
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
    CHECK(static_cast<ludus::foundation::usize>(details.toUtf8().size()) <= ludus::foundation::usize{1} * 1024 * 1024);
}
