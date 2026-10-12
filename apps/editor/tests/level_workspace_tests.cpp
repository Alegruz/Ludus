#include "internal/level_workspace.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <catch2/catch_test_macros.hpp>

using namespace ludus::editor;
namespace
{
ToolingPaths SceneTools()
{
    ToolingPaths tools;
    tools.ToolingRoot = QStringLiteral(LUDUS_EDITOR_PROJECT_ROOT);
    tools.PythonPath = QStandardPaths::findExecutable(QStringLiteral("python3"));
    return tools;
}
void Drain(LevelWorkspace& workspace)
{
    QElapsedTimer timer;
    timer.start();
    while (!workspace.CanSave() && timer.elapsed() < 10000)
    {
        QTest::qWait(10);
    }
    REQUIRE(workspace.CanSave());
}
} // namespace
TEST_CASE("3D level workflow saves and opens a fresh scene without an authoring schema", "[editor][level]")
{
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const auto path = QDir(directory.path()).filePath(QStringLiteral("scenes/test.lulevel"));
    LevelWorkspace workspace(SceneTools());
    workspace.SetProject(directory.path(), 1);
    REQUIRE(workspace.Create(path));
    REQUIRE(workspace.Dirty());
    REQUIRE(workspace.Save());
    Drain(workspace);
    REQUIRE_FALSE(workspace.Dirty());
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    const auto saved = file.readAll();
    const auto value = QJsonDocument::fromJson(saved).object();
    REQUIRE(value.value(QStringLiteral("objects")).toArray().isEmpty());
    LevelWorkspace reopened(SceneTools());
    reopened.SetProject(directory.path(), 2);
    REQUIRE(reopened.Open(path));
    Drain(reopened);
    REQUIRE(reopened.Document() == value);
    REQUIRE_FALSE(reopened.Dirty());
    REQUIRE_FALSE(QFileInfo::exists(QDir(directory.path()).filePath(QStringLiteral("authoring/world.schema.json"))));
}
TEST_CASE("unsupported scene import and saved-file conflicts retain the active scene", "[editor][level]")
{
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const auto path = QDir(directory.path()).filePath(QStringLiteral("scene.lulevel"));
    LevelWorkspace workspace(SceneTools());
    workspace.SetProject(directory.path(), 1);
    REQUIRE(workspace.Create(path));
    REQUIRE(workspace.Save());
    Drain(workspace);
    const auto original = workspace.Document();
    REQUIRE(workspace.Import(directory.path()));
    Drain(workspace);
    REQUIRE(workspace.Document() == original);
    REQUIRE_FALSE(workspace.Dirty());
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    REQUIRE(file.write("external edit") == 13);
    file.close();
    REQUIRE(workspace.Save());
    Drain(workspace);
    REQUIRE(workspace.Document() == original);
    REQUIRE(file.open(QIODevice::ReadOnly));
    REQUIRE(file.readAll() == QByteArray("external edit"));
}

#if defined(LUDUS_EDITOR_NATIVE_PREVIEW)
TEST_CASE("saved cooked level renders after moving the project and removing source inputs", "[editor][level][render]")
{
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const auto root = QDir(directory.path()).filePath(QStringLiteral("project"));
    const auto source = QDir(directory.path()).filePath(QStringLiteral("external"));
    const auto tools = SceneTools();
    QProcess prepare;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PYTHONPATH"),
                       QDir(tools.ToolingRoot).filePath(QStringLiteral("scripts/python")));
    prepare.setProcessEnvironment(environment);
    prepare.start(
        tools.PythonPath,
        {QStringLiteral("-c"),
         QStringLiteral(
             "from pathlib import Path; import sys; from unittest.mock import patch; "
             "from ludus_tools import scene_assets as a; from test_scene_assets import fixture; "
             "r=Path(sys.argv[1]); s=Path(sys.argv[2]); r.mkdir(); s.mkdir(); (s/'source').write_text('fixture'); "
             "p=patch.object(a.scene_cook,'cook',side_effect=fixture); p.start(); "
             "v=a.import_scene(r,s,a.new_level()); p.stop(); a.save_level(r,'scenes/main.lulevel',v)"),
         root,
         source});
    REQUIRE(prepare.waitForFinished(10000));
    INFO(prepare.readAllStandardError().constData());
    REQUIRE(prepare.exitCode() == 0);
    REQUIRE(QDir(source).removeRecursively());
    const auto moved = QDir(directory.path()).filePath(QStringLiteral("moved"));
    REQUIRE(QDir().rename(root, moved));
    LevelWorkspace workspace(tools);
    workspace.SetProject(moved, 1);
    REQUIRE(workspace.Open(QDir(moved).filePath(QStringLiteral("scenes/main.lulevel"))));
    Drain(workspace);
    REQUIRE(workspace.Document().value(QStringLiteral("objects")).toArray().size() == 1);
    auto* viewport = workspace.findChild<QLabel*>(QStringLiteral("levelViewport"));
    REQUIRE(viewport != nullptr);
    REQUIRE_FALSE(viewport->pixmap().isNull());
    const auto image = viewport->pixmap().toImage();
    bool emission = false;
    for (ludus::foundation::int32 y = 0; y < image.height(); ++y)
    {
        for (ludus::foundation::int32 x = 0; x < image.width(); ++x)
        {
            emission = emission || qRed(image.pixel(x, y)) > 0;
        }
    }
    REQUIRE(emission);
    REQUIRE_FALSE(workspace.Dirty());
    REQUIRE(workspace.Save());
    Drain(workspace);
    workspace.StopPreview();
}
#endif

#if defined(LUDUS_EDITOR_NATIVE_PREVIEW)
TEST_CASE("staircase2 imports, saves and renders from a moved project without its source", "[editor][staircase2]")
{
    const auto reference = qEnvironmentVariable("LUDUS_STAGE5_REFERENCE_SOURCE");
    if (reference.isEmpty())
    {
        SUCCEED("Set LUDUS_STAGE5_REFERENCE_SOURCE to run the supplied-scene acceptance test.");
        return;
    }
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const auto root = QDir(directory.path()).filePath(QStringLiteral("project"));
    const auto source = QDir(directory.path()).filePath(QStringLiteral("external"));
    REQUIRE(QDir().mkpath(root));
    const auto tools = SceneTools();
    QProcess copy;
    copy.start(tools.PythonPath,
               {QStringLiteral("-c"),
                QStringLiteral("import shutil,sys; shutil.copytree(sys.argv[1],sys.argv[2])"),
                reference,
                source});
    REQUIRE(copy.waitForFinished(10000));
    REQUIRE(copy.exitCode() == 0);
    LevelWorkspace workspace(tools);
    workspace.SetProject(root, 1);
    const auto path = QDir(root).filePath(QStringLiteral("scenes/main.lulevel"));
    REQUIRE(workspace.Create(path));
    REQUIRE(workspace.Import(source));
    Drain(workspace);
    REQUIRE(workspace.Dirty());
    const auto original = workspace.Document();
    REQUIRE(original.value(QStringLiteral("objects")).toArray().size() == 17);
    REQUIRE(workspace.Save());
    Drain(workspace);
    REQUIRE_FALSE(workspace.Dirty());
    workspace.StopPreview();
    REQUIRE(QDir(source).removeRecursively());
    const auto moved = QDir(directory.path()).filePath(QStringLiteral("moved"));
    REQUIRE(QDir().rename(root, moved));
    LevelWorkspace reopened(tools);
    reopened.SetProject(moved, 2);
    REQUIRE(reopened.Open(QDir(moved).filePath(QStringLiteral("scenes/main.lulevel"))));
    Drain(reopened);
    REQUIRE(reopened.Document() == original);
    auto* viewport = reopened.findChild<QLabel*>(QStringLiteral("levelViewport"));
    REQUIRE(viewport != nullptr);
    REQUIRE_FALSE(viewport->pixmap().isNull());
    const auto output = qEnvironmentVariable("LUDUS_STAGE5_REFERENCE_IMAGE");
    if (!output.isEmpty())
    {
        auto* status = reopened.findChild<QLabel*>(QStringLiteral("levelStatus"));
        REQUIRE(status != nullptr);
        QElapsedTimer timer;
        timer.start();
        while (!status->text().contains(QStringLiteral("64/64")) && timer.elapsed() < 120000)
        {
            QTest::qWait(20);
        }
        REQUIRE(status->text().contains(QStringLiteral("64/64")));
        REQUIRE(viewport->pixmap().save(output));
    }
    reopened.StopPreview();
}
#endif
