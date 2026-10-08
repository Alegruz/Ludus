#include "internal/scene_store.h"
#include "internal/scene_workspace.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <catch2/catch_test_macros.hpp>

using namespace ludus::editor;
namespace
{
QString SceneFile(QTemporaryDir& directory)
{
    const auto path = QDir(QFileInfo(directory.path()).canonicalFilePath()).filePath(QStringLiteral("scene.json"));
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    const auto text = ludus::world_demo::ExampleLevel();
    REQUIRE(file.write(text.data(), static_cast<ludus::foundation::int64>(text.size())) ==
            static_cast<ludus::foundation::int64>(text.size()));
    file.close();
    return path;
}
void Write(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    REQUIRE(file.write(bytes) == bytes.size());
}
} // namespace
TEST_CASE("Scene workspace preserves invalid buffers saves transforms reopens and rejects source conflicts",
          "[editor][scene]")
{
    QTemporaryDir dir;
    const auto path = SceneFile(dir);
    SceneWorkspace view;
    view.SetProject(dir.path(), 1);
    REQUIRE(view.Open(path));
    auto* hierarchy = view.findChild<QListWidget*>(QStringLiteral("sceneHierarchy"));
    REQUIRE(hierarchy != nullptr);
    hierarchy->setCurrentRow(0);
    auto* x = view.findChild<QLineEdit*>(QStringLiteral("sceneTransform0"));
    REQUIRE(x != nullptr);
    const auto original = view.Document().Draft();
    x->setFocus();
    x->selectAll();
    QTest::keyClicks(x, QStringLiteral("-"));
    CHECK(view.Dirty());
    CHECK_FALSE(view.Save());
    CHECK(x->text() == QStringLiteral("-"));
    CHECK(view.Document().Draft() == original);
    x->selectAll();
    QTest::keyClicks(x, QStringLiteral("0.5"));
    REQUIRE(view.Save());
    CHECK_FALSE(view.Dirty());
    CHECK(view.Document().Draft().Level.Entities[0].Position.X == 0.5F);
    SceneWorkspace reopened;
    reopened.SetProject(dir.path(), 1);
    REQUIRE(reopened.Open(path));
    CHECK(reopened.Document().Draft() == view.Document().Draft());
    view.Undo();
    CHECK(view.Dirty());
    view.Redo();
    CHECK_FALSE(view.Dirty());
    auto candidate = view.Document().Draft();
    candidate.Level.Entities[0].Rotation = 0.5F;
    REQUIRE(view.Document().Commit(candidate, view.Document().Stamp(0, 0)));
    Write(path, QByteArrayLiteral("{}"));
    CHECK_FALSE(view.Save());
    CHECK(view.Document().Draft() == candidate);
    CHECK(view.Document().CanUndo());
    QByteArray disk;
    REQUIRE(ReadScene(path, disk));
    CHECK(disk == QByteArrayLiteral("{}"));
}
TEST_CASE("Scene recovery refuses stale truncated invalid and blocked publications", "[editor][scene]")
{
    QTemporaryDir dir;
    const auto path = SceneFile(dir);
    QByteArray baseline;
    REQUIRE(ReadScene(path, baseline));
    SceneDocument doc;
    REQUIRE(doc.Load(baseline.constData(), static_cast<ludus::foundation::usize>(baseline.size())).Error ==
            ludus::world_demo::LevelError::None);
    auto edited = doc.Draft();
    edited.Level.Entities[0].Rotation = 0.5F;
    REQUIRE(doc.Commit(edited, doc.Stamp(0, 0)));
    REQUIRE(WriteSceneRecovery(path, baseline, doc));
    SceneSnapshot restored;
    REQUIRE(ReadSceneRecovery(path, baseline, restored));
    CHECK(restored == edited);
    CHECK_FALSE(ReadSceneRecovery(path, baseline + ' ', restored));
    CHECK(restored == edited);
    Write(path + QStringLiteral(".ludus-recovery"), QByteArrayLiteral("LUDUS-SCENE-RECOVERY-1\n"));
    CHECK_FALSE(ReadSceneRecovery(path, baseline, restored));
    CHECK(restored == edited);
    REQUIRE(QFile::remove(path + QStringLiteral(".ludus-recovery")));
    REQUIRE(QDir().mkdir(path + QStringLiteral(".ludus-recovery")));
    CHECK_FALSE(WriteSceneRecovery(path, baseline, doc));
    CHECK(doc.Draft() == edited);
    CHECK(doc.Dirty());
}
TEST_CASE("Scene load rolls back and stable selection survives structural undo", "[editor][scene]")
{
    QTemporaryDir dir;
    const auto path = SceneFile(dir);
    SceneWorkspace view;
    view.SetProject(dir.path(), 1);
    REQUIRE(view.Open(path));
    const auto stamp = view.Document().Stamp(0, 0);
    const auto initial = view.Document().Draft();
    const auto bad = dir.filePath(QStringLiteral("invalid.json"));
    Write(bad, QByteArrayLiteral("{}"));
    CHECK_FALSE(view.Open(bad));
    CHECK(view.Document().Stamp(0, 0) == stamp);
    CHECK(view.Document().Draft() == initial);
    auto* list = view.findChild<QListWidget*>(QStringLiteral("sceneHierarchy"));
    REQUIRE(list != nullptr);
    list->setCurrentRow(1);
    auto* duplicate = view.findChild<QPushButton*>(QStringLiteral("scene.place"));
    REQUIRE(duplicate != nullptr);
    duplicate->click();
    CHECK(view.Document().Draft().Level.EntityCount == initial.Level.EntityCount + 1);
    const auto selected = view.Preview().Selected();
    view.Undo();
    CHECK(view.Document().Draft() == initial);
    view.Redo();
    CHECK(view.Preview().Selected() == selected);
    CHECK(QFile::exists(path + QStringLiteral(".ludus-recovery")));
}

TEST_CASE("Scene pending transform retains its target and rejects concurrent committed changes", "[editor][scene]")
{
    QTemporaryDir directory;
    const auto path = SceneFile(directory);
    SceneWorkspace view;
    view.SetProject(directory.path(), 1);
    REQUIRE(view.Open(path));
    auto* list = view.findChild<QListWidget*>(QStringLiteral("sceneHierarchy"));
    REQUIRE(list != nullptr);
    list->setCurrentRow(0);
    const auto target = view.Preview().Selected();
    auto* x = view.findChild<QLineEdit*>(QStringLiteral("sceneTransform0"));
    REQUIRE(x != nullptr);
    x->selectAll();
    QTest::keyClicks(x, QStringLiteral("1.25"));
    // Native selection is independent of the Qt list and cannot retarget a buffer.
    view.Preview().Select(view.Document().Draft().Level.Entities[1].Id);
    REQUIRE(view.Save());
    CHECK(view.Document().Draft().Level.Entities[SceneDocument::Find(view.Document().Draft(), target)].Position.X ==
          1.25F);
    list->setCurrentRow(0);
    x->selectAll();
    QTest::keyClicks(x, QStringLiteral("1.5"));
    auto changed = view.Document().Draft();
    changed.Level.Entities[1].Rotation += 0.1F;
    REQUIRE(view.Document().Commit(changed, view.Document().Stamp(0, 0)));
    CHECK_FALSE(view.Save());
    CHECK(x->text() == QStringLiteral("1.5"));
    CHECK(view.Document().Draft() == changed);
    auto* reset = view.findChild<QPushButton*>(QStringLiteral("scene.resetBuffer"));
    REQUIRE(reset != nullptr);
    reset->click();
    REQUIRE(view.Save());
}
