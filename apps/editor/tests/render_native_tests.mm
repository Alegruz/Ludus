#include "internal/preview_viewport.h"
#include "internal/readback.h"
#include "internal/render_preview.h"
#include "internal/world_workspace.h"
#include "renderer_composite.h"
#include "renderer_flat.h"
#include <QQuickWindow>

#import <Cocoa/Cocoa.h>

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>

#include <catch2/catch_test_macros.hpp>

namespace
{
using namespace ludus::editor;
using namespace ludus::foundation;
bool Frames(RenderPreview& preview, uint64 target)
{
    QElapsedTimer timer;
    timer.start();
    while (preview.Frames() < target && timer.elapsed() < 10000 && preview.Running())
    {
        QTest::qWait(20);
    }
    if (preview.Frames() >= target)
    {
        QTest::qWait(100);
    }
    if (preview.Frames() < target)
    {
        WARN("Viewport frames " << preview.Frames() << " / " << target << "; pending=" << preview.HasPendingFrame());
        for (QWindow* window : QGuiApplication::allWindows())
        {
            if (window->objectName() == QStringLiteral("LudusGpuViewport"))
            {
                WARN("Native viewport exposed=" << window->isExposed() << "; visible=" << window->isVisible());
            }
        }
    }
    return preview.Frames() >= target;
}
QImage ReadViewport()
{
    for (QWindow* window : QGuiApplication::allWindows())
    {
        if (window->objectName() == QStringLiteral("LudusGpuViewport"))
        {
            // Independent test-only Qt framebuffer readback; product presentation never reads pixels.
            return static_cast<QQuickWindow*>(window)->grabWindow();
        }
    }
    return {};
}
} // namespace
TEST_CASE("Supplied imported World remains visible in solid and albedo GPU views", "[.render-native]")
{
    const QString root = qEnvironmentVariable("LUDUS_VIEWPORT_TEST_PROJECT");
    if (root.isEmpty() == true)
    {
        SUCCEED("Set LUDUS_VIEWPORT_TEST_PROJECT and LUDUS_VIEWPORT_TEST_WORLD for imported-World acceptance.");
        return;
    }
    WorldWorkspace workspace;
    workspace.resize(1200, 850);
    workspace.show();
    workspace.SetProject(root, 1);
    REQUIRE(workspace.Open(QDir(root).filePath(qEnvironmentVariable("LUDUS_VIEWPORT_TEST_WORLD"))));
    PreviewViewport* viewport =
        static_cast<PreviewViewport*>(workspace.findChild<QWidget*>(QStringLiteral("worldSceneViewport")));
    REQUIRE(viewport != nullptr);
    QComboBox* shading = viewport->findChild<QComboBox*>(QStringLiteral("viewportShading"));
    REQUIRE(shading != nullptr);
    const QJsonObject draft = workspace.Document().Draft();
    const QString imagePrefix = qEnvironmentVariable("LUDUS_VIEWPORT_TEST_IMAGE");
    QImage previous;
    for (int32 mode = 0; mode < 3; ++mode)
    {
        const uint64 target = viewport->PresentedFrames() + 2;
        shading->setCurrentIndex(mode);
        QElapsedTimer timer;
        timer.start();
        while (viewport->PresentedFrames() < target && timer.elapsed() < 15000)
        {
            QTest::qWait(20);
        }
        REQUIRE(viewport->PresentedFrames() >= target);
        const QImage image = ReadViewport();
        REQUIRE_FALSE(image.isNull());
        if (mode == 0)
        {
            bool hasContrast = false;
            const QColor first = image.pixelColor(0, 0);
            for (int32 y = 0; y < image.height(); y += 16)
            {
                for (int32 x = 0; x < image.width(); x += 16)
                {
                    hasContrast = hasContrast || image.pixelColor(x, y) != first;
                }
            }
            CHECK(hasContrast);
        }
        if (mode > 0)
        {
            CHECK(image != previous);
        }
        if (imagePrefix.isEmpty() == false)
        {
            REQUIRE(image.save(imagePrefix + QString::number(mode) + QStringLiteral(".png")));
        }
        previous = image;
    }
    CHECK(workspace.Document().Draft() == draft);
    CHECK_FALSE(workspace.Dirty());
    workspace.StopPreview();
}
TEST_CASE("Native World preview renders authored assets, cameras, culling, updates and resize", "[.render-native]")
{
    REQUIRE(QApplication::platformName() == QStringLiteral("cocoa"));
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const auto root = QFileInfo(directory.path()).canonicalFilePath();
    REQUIRE(QDir(root).mkpath(QStringLiteral("authoring")));
    for (const auto& name : {QStringLiteral("world.schema.json"),
                             QStringLiteral("rendering.world.json"),
                             QStringLiteral("render.preview.json"),
                             QStringLiteral("cube.mesh.json"),
                             QStringLiteral("ludus.png")})
    {
        QFile source(QStringLiteral(":/samples/live-edit-game/authoring/") + name);
        REQUIRE(source.open(QIODevice::ReadOnly));
        QFile target(root + QStringLiteral("/authoring/") + name);
        REQUIRE(target.open(QIODevice::WriteOnly));
        const auto bytes = source.readAll();
        REQUIRE(target.write(bytes) == bytes.size());
    }
    const auto read = [&root](const QString& name) {
        QFile file(root + QStringLiteral("/authoring/") + name);
        REQUIRE(file.open(QIODevice::ReadOnly));
        return file.readAll();
    };
    WorldDocument world;
    QString error;
    REQUIRE(world.LoadSchema(read(QStringLiteral("world.schema.json")), error));
    REQUIRE(world.Load(read(QStringLiteral("rendering.world.json")), error));
    RenderScene scene;
    REQUIRE(RenderScene::Load(root, world, scene, error));
    const auto original = world.Draft();
    RenderPreview preview;
    PreviewViewport viewport(&preview);
    viewport.resize(640, 480);
    viewport.show();
    REQUIRE(preview.SetScene(scene));
    preview.Navigate(0, 0, 0, 3, false);
    REQUIRE(preview.Start());
    const bool presented = Frames(preview, 3);
    INFO(preview.Status().toStdString());
    REQUIRE(presented);
    QImage embedded = ReadViewport();
    REQUIRE(viewport.PresentedFrames() >= 1);
    REQUIRE_FALSE(embedded.isNull());
    CHECK(embedded.width() >= 640);
    CHECK(embedded.pixelColor(embedded.width() / 2, embedded.height() / 2) != embedded.pixelColor(0, 0));
    if (qEnvironmentVariableIsSet("LUDUS_GPU_VIEWPORT_IMAGE") == true)
    {
        REQUIRE(embedded.save(qEnvironmentVariable("LUDUS_GPU_VIEWPORT_IMAGE")));
    }
    // Native window events are forwarded to the persistent editor navigation model.
    for (QWindow* window : QGuiApplication::allWindows())
    {
        if (window->objectName() == QStringLiteral("LudusGpuViewport"))
        {
            QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, QPoint(100, 100));
            QTest::mouseMove(window, QPoint(145, 120));
            QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, QPoint(145, 120));
        }
    }
    REQUIRE(Frames(preview, preview.Frames() + 3));
    CHECK(preview.Navigation().value(QStringLiteral("yaw")).toDouble() != 0);
    const QJsonObject navigation = preview.Navigation();
    preview.Navigate(-1, 70, 30, 4, false);
    preview.RestoreNavigation(navigation);
    CHECK(preview.Navigation() == navigation);
    preview.Navigate(0, 0, 0, 3, false);
    REQUIRE(Frames(preview, preview.Frames() + 3));
    CHECK(preview.Report().Submitted == 1);
    CHECK(preview.Report().Visible == 1);
    CHECK(preview.Report().Draws == 1);
    CHECK(world.Draft() == original);
    CHECK_FALSE(world.Dirty());
    // More than eight identical textured objects consume one shared material.
    for (int32 i = 0; i < 11; ++i)
    {
        QString id;
        REQUIRE(world.DuplicateEntity(error, QStringLiteral("starter.cube"), id));
    }
    REQUIRE(RenderScene::Load(root, world, scene, error));
    CHECK(scene.Surfaces.size() == 1);
    REQUIRE(preview.SetScene(scene));
    preview.Navigate(0, 0, 0, 3, false);
    REQUIRE(Frames(preview, 3));
    CHECK(preview.Report().Visible == 12);
    CHECK(preview.Report().Draws == 1);
    for (int32 i = 0; i < 11; ++i)
    {
        REQUIRE(world.Undo());
    }
    REQUIRE(RenderScene::Load(root, world, scene, error));
    REQUIRE(preview.SetScene(scene));
    preview.Navigate(0, 0, 0, 3, false);
    REQUIRE(Frames(preview, 3));
    CHECK_FALSE(world.Dirty());
    CHECK(preview.WindowInfo().System == ludus::platform::WindowSystem::Headless);
    CHECK(preview.WindowInfo().CocoaWindow == nullptr);
    viewport.resize(720, 480);
    REQUIRE(Frames(preview, preview.Frames() + 3));
    CHECK(preview.Report().Visible == 1);
    embedded = ReadViewport();
    CHECK(embedded.width() >= 720);
    auto entity = world.Entity(QStringLiteral("starter.cube"));
    auto components = entity.value(QStringLiteral("components")).toObject();
    auto transform = components.value(QStringLiteral("transform")).toObject();
    transform.insert(QStringLiteral("x"), 50);
    components.insert(QStringLiteral("transform"), transform);
    REQUIRE(
        world.Edit(QStringLiteral("starter.cube"), components, entity.value(QStringLiteral("name")).toString(), error));
    REQUIRE(RenderScene::Load(root, world, scene, error));
    REQUIRE(preview.SetScene(scene));
    preview.Navigate(0, 0, 0, 3, false);
    REQUIRE(Frames(preview, 3));
    CHECK(preview.Report().Culled == 1);
    CHECK(preview.Report().Visible == 0);
    embedded = ReadViewport();
    CHECK(embedded.pixelColor(embedded.width() / 2, embedded.height() / 2) == embedded.pixelColor(0, 0));
    preview.Navigate(0, 0, 0, 3, true);
    REQUIRE(Frames(preview, preview.Frames() + 3));
    CHECK(preview.Report().Visible == 1);
    CHECK(preview.Report().Culled == 0);
    REQUIRE(world.Undo());
    REQUIRE(RenderScene::Load(root, world, scene, error));
    REQUIRE(preview.SetScene(scene));
    preview.Navigate(0, 0, 0, 3, false);
    REQUIRE(Frames(preview, 3));
    CHECK(preview.Report().Visible == 1);
    CHECK_FALSE(world.Dirty());
    // Object edits preserve the independent orbit anchor until explicit Frame All.
    preview.Navigate(-1, 25, 20, 3, false);
    REQUIRE(Frames(preview, preview.Frames() + 3));
    const auto eye = preview.ViewDescription().CameraPosition;
    entity = world.Entity(QStringLiteral("starter.cube"));
    components = entity.value(QStringLiteral("components")).toObject();
    transform = components.value(QStringLiteral("transform")).toObject();
    transform.insert(QStringLiteral("x"), 50);
    components.insert(QStringLiteral("transform"), transform);
    REQUIRE(
        world.Edit(QStringLiteral("starter.cube"), components, entity.value(QStringLiteral("name")).toString(), error));
    REQUIRE(RenderScene::Load(root, world, scene, error));
    REQUIRE(preview.SetScene(scene));
    preview.Navigate(-1, 25, 20, 3, false);
    REQUIRE(Frames(preview, 3));
    CHECK(preview.ViewDescription().CameraPosition == eye);
    CHECK(preview.Report().Culled == 1);
    preview.FrameAll();
    REQUIRE(Frames(preview, preview.Frames() + 3));
    CHECK(preview.Report().Visible == 1);
    REQUIRE(world.Undo());
    REQUIRE(RenderScene::Load(root, world, scene, error));
    REQUIRE(preview.SetScene(scene));
    preview.FrameAll();
    preview.Navigate(0, 0, 0, 3, false);
    REQUIRE(Frames(preview, 3));
    preview.Stop();
    CHECK_FALSE(preview.Running());
    REQUIRE(preview.Start());
    REQUIRE(Frames(preview, 3));
    preview.Stop();
    // A receiver can stop before Qt samples the candidate without losing later signal delivery.
    RenderPreview closing;
    PreviewViewport closingViewport(&closing);
    closingViewport.show();
    bool hasNotification = false;
    REQUIRE(closing.SetScene(scene));
    QObject::connect(&closing, &RenderPreview::FrameReady, &closing, [&closing]() { closing.Stop(); });
    QObject::connect(&closing, &RenderPreview::FrameReady, &closing, [&hasNotification]() { hasNotification = true; });
    REQUIRE(closing.Start());
    QElapsedTimer closingTimer;
    closingTimer.start();
    while (closing.Running() == true && closingTimer.elapsed() < 10000)
    {
        QTest::qWait(10);
    }
    REQUIRE_FALSE(closing.Running());
    REQUIRE(hasNotification);
    // A headless Metal pixel oracle complements the embedded viewport lifecycle:
    // render an extracted red cube and independently check foreground/background.
    entity = world.Entity(QStringLiteral("starter.cube"));
    components = entity.value(QStringLiteral("components")).toObject();
    auto renderable = components.value(QStringLiteral("renderable")).toObject();
    renderable.insert(QStringLiteral("texture"), QString());
    renderable.insert(QStringLiteral("g"), 0);
    renderable.insert(QStringLiteral("b"), 0);
    components.insert(QStringLiteral("renderable"), renderable);
    REQUIRE(
        world.Edit(QStringLiteral("starter.cube"), components, entity.value(QStringLiteral("name")).toString(), error));
    REQUIRE(RenderScene::Load(root, world, scene, error));
    namespace rhi = ludus::graphics::rhi;
    namespace rr = ludus::graphics::renderer;
    rhi::DeviceHandle device;
    rhi::SurfaceHandle surface;
    rhi::DeviceDescription description;
    description.Required.PortableRaster = true;
    REQUIRE(rhi::CreateDevice({}, { .Width = 96, .Height = 64 }, description, device, surface) ==
            rhi::DeviceStatus::Ready);
    struct Guard final
    {
        rhi::DeviceHandle& Device;
        ~Guard()
        {
            (void)rhi::DestroyDevice(Device);
        }
    } guard{device};
    rr::Renderer renderer;
    REQUIRE(renderer.Initialize(device,
                                ludus::shaders::renderer_flat::Vertex(),
                                ludus::shaders::renderer_flat::Fragment()) == rhi::RasterStatus::Ready);
    REQUIRE(renderer.InitializeViews(ludus::shaders::renderer_composite::Vertex(),
                                     ludus::shaders::renderer_composite::Fragment()) == rhi::RasterStatus::Ready);
    const auto& meshData = scene.Meshes[0];
    rr::Mesh mesh;
    REQUIRE(renderer.CreateMesh(
                {
                    .Positions = meshData.Positions.constData(),
                    .VertexCount = static_cast<usize>(meshData.Positions.size()),
                    .Indices = meshData.Indices.constData(),
                    .IndexCount = static_cast<usize>(meshData.Indices.size()),
                    .Texcoords = meshData.Uvs.constData(),
                },
                mesh) == rhi::RasterStatus::Ready);
    rr::SceneItem item;
    item.Geometry = mesh;
    item.Transform = scene.Objects[0].Transform;
    item.Material.Color = scene.Objects[0].Tint;
    item.SourceId = 1;
    rr::Snapshot snapshot;
    REQUIRE(renderer.CreateSnapshot(&item, 1, snapshot) == rhi::RasterStatus::Ready);
    rr::ViewDescription lens;
    REQUIRE(scene.View(
        {
            .Camera = 0,
            .OrbitDegrees = {0, 0},
            .Distance = 3,
            .Width = 96,
            .Height = 64,
            .Reference = false,
        },
        lens,
        error));
    rr::PreparedView view;
    rr::ViewReport report;
    REQUIRE(renderer.PrepareView(snapshot, lens, view, report) == rhi::RasterStatus::Ready);
    REQUIRE(rhi::BeginFrame(device, surface) == rhi::DeviceStatus::Ready);
    REQUIRE(renderer.DrawView(view, { .Clear = {0.035F, 0.045F, 0.06F, 1}, .ClearDepth = 0 }) ==
            rhi::RasterStatus::Ready);
    REQUIRE(rhi::EndFrame(device) == rhi::DeviceStatus::Ready);
    uint8 pixels[96 * 64 * 4]{};
    REQUIRE(rhi::backend::ReadHeadlessPixels(pixels));
    const usize center = (usize{32} * 96 + 48) * 4;
    CHECK(pixels[center] > 240);
    CHECK(pixels[center + 1] < 10);
    CHECK(pixels[center + 2] < 10);
    CHECK(pixels[0] < 100);
    CHECK(pixels[1] < 100);
    CHECK(pixels[2] < 100);
    if (!qEnvironmentVariable("LUDUS_RENDER_NATIVE_IMAGE").isEmpty())
    {
        const QImage image(pixels, 96, 64, QImage::Format_RGBA8888);
        REQUIRE(image.save(qEnvironmentVariable("LUDUS_RENDER_NATIVE_IMAGE")));
    }
    renderer.Reset();
}
