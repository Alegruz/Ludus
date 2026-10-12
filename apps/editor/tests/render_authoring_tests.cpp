#include "internal/render_panel.h"
#include "internal/render_scene.h"
#include "internal/workspace_style.h"
#include "internal/world_workspace.h"

#include <ludus/foundation/math/projection.hpp>
#include <ludus/foundation/math/transform.hpp>

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSlider>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace ludus::editor;
using namespace ludus::foundation;
namespace math = ludus::foundation::math;
namespace rr = ludus::graphics::renderer;
namespace
{
QByteArray Read(const QString& path)
{
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}
void Write(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    REQUIRE(file.write(bytes) == bytes.size());
}
struct Property final
{
    QString Component;
    QString Field;
};
struct Fixture final
{
    QTemporaryDir Directory;
    QString Root;
    WorldDocument World;
    RenderScene Scene;
    QString Error;
    Fixture()
    {
        REQUIRE(Directory.isValid());
        Root = QFileInfo(Directory.path()).canonicalFilePath();
        REQUIRE(QDir(Root).mkpath(QStringLiteral("authoring")));
        for (const auto& name : {QStringLiteral("world.schema.json"),
                                 QStringLiteral("rendering.world.json"),
                                 QStringLiteral("render.preview.json"),
                                 QStringLiteral("cube.mesh.json"),
                                 QStringLiteral("ludus.png")})
        {
            Write(Path(name), Read(QStringLiteral(":/samples/live-edit-game/authoring/") + name));
        }
        REQUIRE(World.LoadSchema(Read(Path(QStringLiteral("world.schema.json"))), Error));
        REQUIRE(World.Load(Read(Path(QStringLiteral("rendering.world.json"))), Error));
        const auto loaded = RenderScene::Load(Root, World, Scene, Error);
        INFO(Error.toStdString());
        REQUIRE(loaded);
    }
    [[nodiscard]] QString Path(const QString& file) const
    {
        return Root + QStringLiteral("/authoring/") + file;
    }
    void Descriptor(const QJsonObject& descriptor)
    {
        Write(Path(QStringLiteral("render.preview.json")), QJsonDocument(descriptor).toJson());
    }
    [[nodiscard]] QJsonObject Descriptor() const
    {
        return QJsonDocument::fromJson(Read(Path(QStringLiteral("render.preview.json")))).object();
    }
    bool Edit(const QString& id, const Property& property, const QJsonValue& value)
    {
        const auto entity = World.Entity(id);
        auto components = entity.value(QStringLiteral("components")).toObject();
        auto properties = components.value(property.Component).toObject();
        properties.insert(property.Field, value);
        components.insert(property.Component, properties);
        return World.Edit(id, components, entity.value(QStringLiteral("name")).toString(), Error);
    }
};
} // namespace
TEST_CASE("A creator opens a textured 3D World through its project bindings without writing files",
          "[editor][render-authoring]")
{
    Fixture f;
    CHECK(f.Scene.Objects.size() == 1);
    CHECK(f.Scene.Meshes.size() == 1);
    CHECK(f.Scene.Meshes[0].Positions.size() == 24);
    CHECK(f.Scene.Meshes[0].Indices.size() == 36);
    CHECK(f.Scene.Textures.size() == 1);
    CHECK(f.Scene.Textures[0].Pixels.format() == QImage::Format_RGBA8888);
    CHECK(f.Scene.Cameras.size() == 1);
    CHECK_FALSE(f.World.Dirty());
    const auto before = Read(f.Path(QStringLiteral("rendering.world.json")));
    rr::ViewDescription view;
    REQUIRE(f.Scene.View(
        { .Camera = 0, .OrbitDegrees = {0, 0}, .Distance = 3, .Width = 800, .Height = 600, .Reference = false },
        view,
        f.Error));
    math::Vector3 center;
    REQUIRE(math::TryPerspectiveDivide(view.WorldToClip * math::Vector4{0, 0, 0, 1}, .0001F, center) ==
            math::MathStatus::Success);
    CHECK(center.X == Catch::Approx(0));
    CHECK(center.Y == Catch::Approx(0));
    CHECK(center.Z > 0);
    CHECK(center.Z < 1);
    CHECK(view.Depth == rr::DepthConvention::ReverseZ);
    CHECK(Read(f.Path(QStringLiteral("rendering.world.json"))) == before);
}
TEST_CASE("Invalid preview bindings and duplicate JSON preserve the last accepted scene", "[editor][render-authoring]")
{
    Fixture f;
    const auto identity = f.Scene.Identity;
    auto descriptor = f.Descriptor();
    auto transform = descriptor.value(QStringLiteral("transform")).toObject();
    auto properties = transform.value(QStringLiteral("properties")).toObject();
    properties.insert(QStringLiteral("x"), QStringLiteral("missing"));
    transform.insert(QStringLiteral("properties"), properties);
    descriptor.insert(QStringLiteral("transform"), transform);
    f.Descriptor(descriptor);
    CHECK_FALSE(RenderScene::Load(f.Root, f.World, f.Scene, f.Error));
    CHECK(f.Error.contains(QStringLiteral("transform.x")));
    CHECK(f.Scene.Identity == identity);
    Write(f.Path(QStringLiteral("render.preview.json")), QByteArrayLiteral("{\"version\":1,\"version\":1}"));
    CHECK_FALSE(RenderScene::Load(f.Root, f.World, f.Scene, f.Error));
    CHECK(f.Scene.Identity == identity);
    CHECK_FALSE(f.World.Dirty());
}
TEST_CASE("Mesh import validation rejects corrupt indices and missing UVs atomically", "[editor][render-authoring]")
{
    Fixture f;
    const auto identity = f.Scene.Identity;
    const auto source = Read(f.Path(QStringLiteral("cube.mesh.json")));
    for (const QJsonValue& index : {QJsonValue(-1), QJsonValue(1.5), QJsonValue(24)})
    {
        auto mesh = QJsonDocument::fromJson(source).object();
        auto indices = mesh.value(QStringLiteral("indices")).toArray();
        indices[0] = index;
        mesh.insert(QStringLiteral("indices"), indices);
        Write(f.Path(QStringLiteral("cube.mesh.json")), QJsonDocument(mesh).toJson());
        CHECK_FALSE(RenderScene::Load(f.Root, f.World, f.Scene, f.Error));
        CHECK(f.Scene.Identity == identity);
    }
    auto mesh = QJsonDocument::fromJson(source).object();
    mesh.insert(QStringLiteral("uvs"), QJsonArray{});
    Write(f.Path(QStringLiteral("cube.mesh.json")), QJsonDocument(mesh).toJson());
    CHECK_FALSE(RenderScene::Load(f.Root, f.World, f.Scene, f.Error));
    CHECK(f.Scene.Identity == identity);
}
TEST_CASE("Preview files reject traversal and symlinks and do not escape the project", "[editor][render-authoring]")
{
    Fixture f;
    auto descriptor = f.Descriptor();
    auto meshes = descriptor.value(QStringLiteral("meshes")).toArray();
    auto mesh = meshes[0].toObject();
    mesh.insert(QStringLiteral("path"), QStringLiteral("authoring/../authoring/cube.mesh.json"));
    meshes[0] = mesh;
    descriptor.insert(QStringLiteral("meshes"), meshes);
    f.Descriptor(descriptor);
    const auto identity = f.Scene.Identity;
    CHECK_FALSE(RenderScene::Load(f.Root, f.World, f.Scene, f.Error));
    CHECK(f.Scene.Identity == identity);
    REQUIRE(QFile::link(f.Path(QStringLiteral("cube.mesh.json")), f.Path(QStringLiteral("linked.json"))));
    mesh.insert(QStringLiteral("path"), QStringLiteral("authoring/linked.json"));
    meshes[0] = mesh;
    descriptor.insert(QStringLiteral("meshes"), meshes);
    f.Descriptor(descriptor);
    CHECK_FALSE(RenderScene::Load(f.Root, f.World, f.Scene, f.Error));
    CHECK(f.Scene.Identity == identity);
}
TEST_CASE("Camera lens and rigid-pose validation explain failures without replacing preview data",
          "[editor][render-authoring]")
{
    Fixture f;
    const auto identity = f.Scene.Identity;
    REQUIRE(f.Edit(QStringLiteral("preview.camera"), {QStringLiteral("camera"), QStringLiteral("near")}, 200));
    CHECK_FALSE(RenderScene::Load(f.Root, f.World, f.Scene, f.Error));
    CHECK(f.Error.contains(QStringLiteral("camera"), Qt::CaseInsensitive));
    CHECK(f.Scene.Identity == identity);
    REQUIRE(f.World.Undo());
    REQUIRE(f.Edit(QStringLiteral("preview.camera"), {QStringLiteral("transform"), QStringLiteral("sx")}, 2));
    CHECK_FALSE(RenderScene::Load(f.Root, f.World, f.Scene, f.Error));
    CHECK(f.Error.contains(QStringLiteral("unit scale")));
    CHECK(f.Scene.Identity == identity);
}
TEST_CASE("An orthographic follow camera resolves the same authored target and publishes a C0 trace",
          "[editor][render-authoring]")
{
    Fixture f;
    REQUIRE(f.Edit(QStringLiteral("starter.cube"), {QStringLiteral("transform"), QStringLiteral("x")}, 4));
    REQUIRE(f.Edit(QStringLiteral("preview.camera"),
                   {QStringLiteral("camera"), QStringLiteral("projection")},
                   QStringLiteral("orthographic")));
    REQUIRE(f.Edit(QStringLiteral("preview.camera"),
                   {QStringLiteral("camera"), QStringLiteral("follow")},
                   QStringLiteral("starter.cube")));
    REQUIRE(RenderScene::Load(f.Root, f.World, f.Scene, f.Error));
    CHECK(f.Scene.Cameras[0].Sample.Pose.Position.X == 4);
    CHECK(f.Scene.Cameras[0].Sample.Pose.Position.Z == 6);
    CHECK(f.Scene.Cameras[0].Trace.Recipe == ludus::gameplay::camera::CameraRecipe::OrthographicFollow);
    rr::ViewDescription view;
    REQUIRE(f.Scene.View(
        { .Camera = 0, .OrbitDegrees = {0, 0}, .Distance = 3, .Width = 800, .Height = 600, .Reference = false },
        view,
        f.Error));
    CHECK(view.OrthographicCamera);
    REQUIRE(f.Edit(QStringLiteral("preview.camera"), {QStringLiteral("transform"), QStringLiteral("ry")}, 20));
    CHECK_FALSE(RenderScene::Load(f.Root, f.World, f.Scene, f.Error));
}
TEST_CASE("Editor orbit and reference comparison do not mutate authored cameras or World history",
          "[editor][render-authoring]")
{
    Fixture f;
    const auto draft = f.World.Draft();
    rr::ViewDescription view;
    REQUIRE(f.Scene.View(
        {
            .Camera = -1,
            .OrbitDegrees = {45, 30},
            .Distance = f.Scene.Radius * 3,
            .Width = 640,
            .Height = 480,
            .Reference = true,
        },
        view,
        f.Error));
    CHECK(view.Reference);
    CHECK(view.InfiniteFar);
    const auto matrix = view.WorldToClip;
    CHECK_FALSE(f.Scene.View(
        {
            .Camera = -1,
            .OrbitDegrees = {45, 90},
            .Distance = f.Scene.Radius * 3,
            .Width = 640,
            .Height = 480,
            .Reference = false,
        },
        view,
        f.Error));
    CHECK(view.WorldToClip == matrix);
    CHECK_FALSE(f.Scene.View(
        {
            .Camera = -1,
            .OrbitDegrees = {45, 30},
            .Distance = f.Scene.Radius * 3,
            .Width = 0,
            .Height = 480,
            .Reference = false,
        },
        view,
        f.Error));
    CHECK(view.WorldToClip == matrix);
    CHECK_FALSE(f.World.CanUndo());
    CHECK(f.World.Draft() == draft);
}
TEST_CASE("Preview UI validates assets, selects authored cameras and clears state on document replacement",
          "[editor][render-authoring]")
{
    Fixture f;
    RenderPanel panel;
    panel.SetWorld(f.Root, &f.World);
    QSignalSpy loaded(&panel, &RenderPanel::Loaded);
    REQUIRE(panel.Refresh(false));
    REQUIRE(loaded.wait(5000));
    REQUIRE(loaded.last()[0].toBool());
    auto* cameras = panel.findChild<QComboBox*>(QStringLiteral("worldPreviewCamera"));
    REQUIRE(cameras != nullptr);
    REQUIRE(cameras->count() == 2);
    cameras->setCurrentIndex(1);
    CHECK_FALSE(panel.findChild<QSlider*>(QStringLiteral("worldPreviewYaw"))->isEnabled());
    CHECK(panel.findChild<QLabel*>(QStringLiteral("worldPreviewTrace"))->text().contains(QStringLiteral("C0 fixed")));
    const auto draft = f.World.Draft();
    panel.findChild<QPushButton*>(QStringLiteral("worldPreviewFrameAll"))->click();
    CHECK(cameras->currentIndex() == 0);
    CHECK(f.World.Draft() == draft);
    const auto identity = panel.Preview().Scene().Identity;
    Write(f.Path(QStringLiteral("ludus.png")), QByteArrayLiteral("invalid PNG"));
    REQUIRE(panel.Refresh(false));
    REQUIRE(loaded.wait(5000));
    CHECK_FALSE(loaded.last()[0].toBool());
    CHECK(panel.Preview().Scene().Identity == identity);
    CHECK_FALSE(f.World.Dirty());
    panel.SetWorld({}, nullptr);
    CHECK(panel.Preview().Scene().Identity.isEmpty());
    CHECK(cameras->count() == 1);
    CHECK_FALSE(panel.findChild<QPushButton*>(QStringLiteral("worldPreview3D"))->isEnabled());
}
TEST_CASE("Late preview loads are discarded and repeated edits coalesce to the latest applied World",
          "[editor][render-authoring]")
{
    Fixture f;
    RenderPanel panel;
    panel.SetWorld(f.Root, &f.World);
    QSignalSpy loaded(&panel, &RenderPanel::Loaded);
    REQUIRE(panel.Refresh(false));
    panel.SetWorld({}, nullptr);
    QTest::qWait(100);
    CHECK(loaded.isEmpty());
    CHECK(panel.Preview().Scene().Identity.isEmpty());
    panel.SetWorld(f.Root, &f.World);
    REQUIRE(panel.Refresh(false));
    REQUIRE(f.Edit(QStringLiteral("starter.cube"), {QStringLiteral("transform"), QStringLiteral("x")}, 3));
    panel.WorldChanged();
    REQUIRE(f.Edit(QStringLiteral("starter.cube"), {QStringLiteral("transform"), QStringLiteral("x")}, 6));
    panel.WorldChanged();
    REQUIRE(loaded.wait(5000));
    REQUIRE(loaded.last()[0].toBool());
    REQUIRE(panel.Preview().Scene().Objects.size() == 1);
    CHECK(panel.Preview().Scene().Objects[0].Transform.Translation.X == 6);
    CHECK_FALSE(panel.Preview().Running());
}
TEST_CASE("Preview extraction keeps the GUI event loop responsive while assets decode", "[editor][render-authoring]")
{
    Fixture f;
    RenderPanel panel;
    panel.SetWorld(f.Root, &f.World);
    QSignalSpy loaded(&panel, &RenderPanel::Loaded);
    bool heartbeat = false;
    QTimer::singleShot(0, &panel, [&heartbeat]() { heartbeat = true; });
    REQUIRE(panel.Refresh(false));
    CHECK_FALSE(heartbeat);
    REQUIRE(loaded.wait(5000));
    CHECK(heartbeat);
    CHECK(loaded.last()[0].toBool());
    CHECK_FALSE(f.World.Dirty());
}
TEST_CASE("Closing a pending preview prevents its worker from opening a native surface later",
          "[editor][render-authoring]")
{
    Fixture f;
    RenderPanel panel;
    panel.SetWorld(f.Root, &f.World);
    QSignalSpy loaded(&panel, &RenderPanel::Loaded);
    REQUIRE(panel.Refresh(true));
    CHECK(panel.findChild<QPushButton*>(QStringLiteral("worldPreviewStop"))->isEnabled());
    panel.findChild<QPushButton*>(QStringLiteral("worldPreviewStop"))->click();
    QTest::qWait(100);
    CHECK(loaded.isEmpty());
    CHECK_FALSE(panel.Preview().Running());
    CHECK(panel.Preview().Scene().Identity.isEmpty());
    CHECK_FALSE(panel.findChild<QPushButton*>(QStringLiteral("worldPreviewStop"))->isEnabled());
}
TEST_CASE("Renderer controls and inspector remain reachable in a small styled World window",
          "[editor][render-authoring]")
{
    Fixture f;
    WorldWorkspace world;
    world.SetProject(f.Root, 1);
    REQUIRE(world.Open(f.Path(QStringLiteral("rendering.world.json"))));
    ApplyWorkspaceStyle(&world);
    world.findChild<QListWidget*>(QStringLiteral("worldEntities"))->setCurrentRow(0);
    world.resize(800, 640);
    world.show();
    world.findChild<QPushButton*>(QStringLiteral("worldPreviewDetails"))->click();
    QTest::qWait(30);
    CHECK(world.width() <= 800);
    CHECK(world.height() <= 640);
    auto* scroll = world.findChild<QScrollArea*>(QStringLiteral("worldPreviewSettingsScroll"));
    REQUIRE(scroll != nullptr);
    CHECK(scroll->height() <= 120);
    CHECK(scroll->verticalScrollBar()->maximum() > 0);
    for (const auto& name : {QStringLiteral("worldPreview3D"),
                             QStringLiteral("worldPreviewReload"),
                             QStringLiteral("worldAdd"),
                             QStringLiteral("worldApply")})
    {
        auto* control = world.findChild<QPushButton*>(name);
        REQUIRE(control != nullptr);
        CHECK(control->isVisible());
        const auto top = control->mapTo(&world, QPoint(0, 0));
        CHECK(top.y() >= 0);
        CHECK(top.y() + control->height() <= world.height());
    }
    if (!qEnvironmentVariable("LUDUS_RENDER_SCREENSHOT").isEmpty())
    {
        REQUIRE(world.grab().save(qEnvironmentVariable("LUDUS_RENDER_SCREENSHOT")));
    }
}
TEST_CASE("Artists choose display tint into pending linear fields with atomic Apply and Undo",
          "[editor][render-authoring]")
{
    Fixture f;
    WorldWorkspace world;
    world.SetProject(f.Root, 1);
    REQUIRE(world.Open(f.Path(QStringLiteral("rendering.world.json"))));
    world.findChild<QListWidget*>(QStringLiteral("worldEntities"))->setCurrentRow(0);
    auto* panel = world.findChild<RenderPanel*>();
    REQUIRE(panel != nullptr);
    QSignalSpy loaded(panel, &RenderPanel::Loaded);
    REQUIRE(panel->Refresh(false));
    REQUIRE(loaded.wait(5000));
    const auto original = world.Document().Draft();
    auto* tint = world.findChild<QPushButton*>(QStringLiteral("worldPickTint"));
    REQUIRE(tint->isEnabled());
    tint->click();
    auto* picker = world.findChild<QColorDialog*>(QStringLiteral("worldTintPicker"));
    REQUIRE(picker != nullptr);
    picker->setCurrentColor(QColor(128, 128, 128));
    picker->accept();
    CHECK(world.Document().Draft() == original);
    CHECK(world.Dirty());
    CHECK_FALSE(world.CanUndo());
    CHECK_FALSE(tint->isEnabled());
    REQUIRE(world.Apply());
    const auto rgb = world.Document()
                         .Entity(QStringLiteral("starter.cube"))
                         .value(QStringLiteral("components"))
                         .toObject()
                         .value(QStringLiteral("renderable"))
                         .toObject();
    // Independent broad band distinguishes linear gray from the display 0.502 value.
    CHECK(rgb.value(QStringLiteral("r")).toDouble() > .21);
    CHECK(rgb.value(QStringLiteral("r")).toDouble() < .22);
    CHECK(rgb.value(QStringLiteral("g")) == rgb.value(QStringLiteral("r")));
    CHECK(rgb.value(QStringLiteral("b")) == rgb.value(QStringLiteral("r")));
    world.Undo();
    CHECK(world.Document().Draft() == original);
}
TEST_CASE("Orbit-only projects and tiny geometry do not need a camera component or unit-sized framing",
          "[editor][render-authoring]")
{
    Fixture f;
    auto descriptor = f.Descriptor();
    descriptor.insert(QStringLiteral("camera"), QJsonValue(QJsonValue::Null));
    f.Descriptor(descriptor);
    for (const auto& axis : {QStringLiteral("sx"), QStringLiteral("sy"), QStringLiteral("sz")})
    {
        REQUIRE(f.Edit(QStringLiteral("starter.cube"), {QStringLiteral("transform"), axis}, .001));
    }
    REQUIRE(RenderScene::Load(f.Root, f.World, f.Scene, f.Error));
    CHECK(f.Scene.Cameras.isEmpty());
    CHECK(f.Scene.Radius < .01F);
    rr::ViewDescription view;
    REQUIRE(f.Scene.View(
        {
            .Camera = -1,
            .OrbitDegrees = {25, 20},
            .Distance = f.Scene.Radius * 3,
            .Width = 800,
            .Height = 600,
            .Reference = false,
        },
        view,
        f.Error));
    CHECK(view.InfiniteFar);
}
TEST_CASE("Tint dialogs preserve data on unchanged color and reject callbacks after World replacement",
          "[editor][render-authoring]")
{
    Fixture f;
    WorldWorkspace world;
    world.SetProject(f.Root, 1);
    const auto path = f.Path(QStringLiteral("rendering.world.json"));
    REQUIRE(world.Open(path));
    auto* list = world.findChild<QListWidget*>(QStringLiteral("worldEntities"));
    list->setCurrentRow(0);
    auto* panel = world.findChild<RenderPanel*>();
    QSignalSpy loaded(panel, &RenderPanel::Loaded);
    REQUIRE(panel->Refresh(false));
    REQUIRE(loaded.wait(5000));
    auto* tint = world.findChild<QPushButton*>(QStringLiteral("worldPickTint"));
    REQUIRE(tint->isEnabled());
    tint->click();
    auto* picker = world.findChild<QColorDialog*>(QStringLiteral("worldTintPicker"));
    REQUIRE(picker != nullptr);
    picker->accept();
    CHECK_FALSE(world.Dirty());
    QTest::qWait(1);
    tint->click();
    picker = world.findChild<QColorDialog*>(QStringLiteral("worldTintPicker"));
    REQUIRE(picker != nullptr);
    REQUIRE(world.Open(path));
    picker->setCurrentColor(QColor(128, 128, 128));
    picker->accept();
    CHECK_FALSE(world.Dirty());
    CHECK(world.findChild<QLabel*>(QStringLiteral("worldStatus"))
              ->text()
              .contains(QStringLiteral("World or selection changed")));
}
TEST_CASE("Duplicate textured objects share surfaces while distinct material capacity failures preserve the scene",
          "[editor][render-authoring]")
{
    Fixture f;
    for (int32 i = 0; i < 11; ++i)
    {
        QString id;
        REQUIRE(f.World.DuplicateEntity(f.Error, QStringLiteral("starter.cube"), id));
    }
    REQUIRE(RenderScene::Load(f.Root, f.World, f.Scene, f.Error));
    CHECK(f.Scene.Objects.size() == 12);
    CHECK(f.Scene.Surfaces.size() == 1);
    for (const auto& object : f.Scene.Objects)
    {
        CHECK(object.Surface == 0);
    }
    const auto identity = f.Scene.Identity;
    const auto objects = f.Scene.Objects;
    for (int32 i = 0; i < 9; ++i)
    {
        REQUIRE(
            f.Edit(objects[i].Id, {QStringLiteral("renderable"), QStringLiteral("r")}, static_cast<float64>(i) / 10));
    }
    CHECK_FALSE(RenderScene::Load(f.Root, f.World, f.Scene, f.Error));
    CHECK(f.Error.contains(QStringLiteral("eight distinct")));
    CHECK(f.Scene.Identity == identity);
}
TEST_CASE("Renderer snapshot admission protects fixed handle arrays even for an invalid private caller",
          "[editor][render-authoring]")
{
    Fixture f;
    RenderPreview preview;
    REQUIRE(preview.SetScene(f.Scene));
    auto invalid = f.Scene;
    invalid.Surfaces.clear();
    CHECK_FALSE(preview.SetScene(invalid));
    CHECK(preview.Scene().Identity == f.Scene.Identity);
    invalid = f.Scene;
    for (int32 i = 0; i < 6; ++i)
    {
        invalid.Meshes.append(f.Scene.Meshes[0]);
    }
    CHECK_FALSE(preview.SetScene(invalid));
    CHECK(preview.Scene().Identity == f.Scene.Identity);
}
