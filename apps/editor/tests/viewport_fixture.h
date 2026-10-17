#pragma once

#include "internal/render_scene.h"
#include "internal/world_document.h"

#include <ludus/foundation/base/types.h>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QString>

#include <catch2/catch_test_macros.hpp>

namespace ludus::editor::tests
{
/// Shadowed point lamps in the lit regression scene: six cube faces each gives
/// 72 shadow regions, within the renderer's 80-region atlas.
inline constexpr foundation::int32 LAMP_COUNT = 12;
/// Extra copies of the sample cube. Each caster draws once per shadow region, so
/// five casters need 360 atlas draws: above the RHI's default 256 draws per plan,
/// like the supplied staircase2 scene (6 casters x 78 regions).
inline constexpr foundation::int32 EXTRA_CASTER_COUNT = 4;
/// Spacing between lamps along the row above the World origin, in world units.
inline constexpr foundation::float64 LAMP_SPACING = 1.5;
/// Height of the lamp row above the World origin, in world units.
inline constexpr foundation::float64 LAMP_HEIGHT = 3;
/// Reads a test input completely; a missing file fails the current test.
inline QByteArray ReadTestFile(const QString& path)
{
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly) == true);
    return file.readAll();
}
/// Copies the live-edit sample World into root, adds shadow-casting cube copies and
/// a row of authored point lamps, so the cooked scene takes the authored-lighting path.
inline void MakeLitScene(const QString& root, RenderScene& outScene)
{
    REQUIRE(QDir(root).mkpath(QStringLiteral("authoring")) == true);
    for (const QString& name : {QStringLiteral("world.schema.json"),
                                QStringLiteral("rendering.world.json"),
                                QStringLiteral("render.preview.json"),
                                QStringLiteral("cube.mesh.json"),
                                QStringLiteral("ludus.png")})
    {
        QFile target(root + QStringLiteral("/authoring/") + name);
        REQUIRE(target.open(QIODevice::WriteOnly) == true);
        const QByteArray bytes = ReadTestFile(QStringLiteral(":/samples/live-edit-game/authoring/") + name);
        REQUIRE(target.write(bytes) == bytes.size());
    }
    QString error;
    WorldDocument world;
    REQUIRE(world.LoadSchema(ReadTestFile(root + QStringLiteral("/authoring/world.schema.json")), error) == true);
    REQUIRE(world.Load(ReadTestFile(root + QStringLiteral("/authoring/rendering.world.json")), error) == true);
    for (foundation::int32 i = 0; i < EXTRA_CASTER_COUNT; ++i)
    {
        QString casterId;
        REQUIRE(world.DuplicateEntity(error, QStringLiteral("starter.cube"), casterId) == true);
        QJsonObject components = world.Entity(casterId).value(QStringLiteral("components")).toObject();
        QJsonObject transform = components.value(QStringLiteral("transform")).toObject();
        transform.insert(QStringLiteral("x"),
                         transform.value(QStringLiteral("x")).toDouble() +
                             static_cast<foundation::float64>(i + 1) * LAMP_SPACING);
        components.insert(QStringLiteral("transform"), transform);
        REQUIRE(world.Edit(casterId, components, QStringLiteral("Caster"), error) == true);
    }
    for (foundation::int32 i = 0; i < LAMP_COUNT; ++i)
    {
        // Lamps carry no renderable, so they add lights without adding meshes.
        QString lampId;
        REQUIRE(world.CreateEntity(error, QStringLiteral("Lamp"), lampId) == true);
        QJsonObject components = world.Entity(lampId).value(QStringLiteral("components")).toObject();
        QJsonObject transform = components.value(QStringLiteral("transform")).toObject();
        transform.insert(QStringLiteral("x"),
                         (static_cast<foundation::float64>(i) - static_cast<foundation::float64>(LAMP_COUNT) / 2) *
                             LAMP_SPACING);
        transform.insert(QStringLiteral("y"), LAMP_HEIGHT);
        components.insert(QStringLiteral("transform"), transform);
        components.insert(QStringLiteral("light"),
                          QJsonObject{{QStringLiteral("kind"), QStringLiteral("point")},
                                      {QStringLiteral("enabled"), true},
                                      {QStringLiteral("r"), 1},
                                      {QStringLiteral("g"), 1},
                                      {QStringLiteral("b"), 1},
                                      {QStringLiteral("intensity"), 20},
                                      {QStringLiteral("range"), 20},
                                      {QStringLiteral("innerAngle"), 25},
                                      {QStringLiteral("outerAngle"), 30}});
        REQUIRE(world.Edit(lampId, components, QStringLiteral("Lamp"), error) == true);
    }
    const bool isLoaded = RenderScene::Load(root, world, outScene, error);
    INFO(error.toStdString());
    REQUIRE(isLoaded == true);
    REQUIRE(outScene.Lit == true);
    REQUIRE(outScene.AuthoredLights.Count == static_cast<foundation::uint32>(LAMP_COUNT));
}
} // namespace ludus::editor::tests
