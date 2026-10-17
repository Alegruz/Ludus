#include "internal/preview_shading.h"
#include "internal/preview_viewport.h"
#include "internal/render_preview.h"
#include "internal/render_scene.h"
#include "viewport_fixture.h"

#include <ludus/graphics/rhi/rhi.h>

#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QTest>
#include <QVBoxLayout>
#include <QWidget>
#include <QWindow>

#include <catch2/catch_test_macros.hpp>

using namespace ludus::editor;
using namespace ludus::foundation;
namespace rhi = ludus::graphics::rhi;

namespace
{
/// Frames the shared viewport must present after Lit preparation completes.
constexpr uint64 PRESENTED_FRAMES = 3;
/// Upper bound for Lit preparation, the shadow atlas and presentation on an integrated GPU.
constexpr int32 SHARED_TIMEOUT_MILLISECONDS = 60000;
/// Qt event-loop slice between preview ticks while waiting for frames.
constexpr int32 POLL_MILLISECONDS = 20;
/// Embedded viewport size in device-independent pixels.
constexpr int32 VIEWPORT_WIDTH = 640;
constexpr int32 VIEWPORT_HEIGHT = 420;
/// Time a viewport stays hidden, long enough for the compositor to unmap its surfaces.
constexpr int32 HIDDEN_MILLISECONDS = 200;
/// Hide/show round-trips per case; repeats catch state that only one cycle restores.
constexpr int32 VISIBILITY_CYCLES = 2;
QWindow* FindWindow(const QString& name)
{
    for (QWindow* window : QGuiApplication::allWindows())
    {
        if (window->objectName() == name)
        {
            return window;
        }
    }
    return nullptr;
}
/// Waits until the viewport has presented target frames, the preview stops, or the timeout expires.
bool AwaitPresentedFrames(const PreviewViewport& viewport, const RenderPreview& preview, uint64 target)
{
    QElapsedTimer wait;
    wait.start();
    while (viewport.PresentedFrames() < target && preview.Running() == true &&
           wait.elapsed() < SHARED_TIMEOUT_MILLISECONDS)
    {
        QTest::qWait(POLL_MILLISECONDS);
    }
    return viewport.PresentedFrames() >= target;
}
/// Returns why shared presentation cannot run on this display, or an empty string when it can.
QString GetSharedDisplaySkipReason()
{
    const QString platform = QGuiApplication::platformName();
    if (platform != QStringLiteral("xcb") && platform.startsWith(QStringLiteral("wayland")) == false)
    {
        return QStringLiteral("Shared presentation needs an X11 or Wayland display (run with QT_QPA_PLATFORM=xcb or "
                              "wayland)");
    }
    if (qEnvironmentVariable("LUDUS_EDITOR_VIEWPORT") == QStringLiteral("offscreen"))
    {
        return QStringLiteral("LUDUS_EDITOR_VIEWPORT=offscreen forces readback presentation");
    }
    return {};
}
} // namespace

TEST_CASE("Linux viewport shares an editor-created Vulkan device with Qt Quick", "[editor][viewport][shared][gpu]")
{
#if defined(LUDUS_EDITOR_NATIVE_PREVIEW) && defined(Q_OS_LINUX)
    const QString skipReason = GetSharedDisplaySkipReason();
    if (skipReason.isEmpty() == false)
    {
        SKIP(skipReason.toStdString());
    }
    rhi::Shutdown();
    struct Guard final
    {
        ~Guard()
        {
            rhi::Shutdown();
        }
    } guard;
    QTemporaryDir directory;
    REQUIRE(directory.isValid() == true);
    RenderScene scene;
    tests::MakeLitScene(QFileInfo(directory.path()).canonicalFilePath(), scene);
    RenderPreview preview;
    PreviewViewport viewport(&preview);
    viewport.resize(VIEWPORT_WIDTH, VIEWPORT_HEIGHT);
    viewport.show();
    REQUIRE(QTest::qWaitForWindowExposed(&viewport) == true);
    REQUIRE(preview.SetScene(scene) == true);
    preview.SetShading(PreviewShading::Lit);
    REQUIRE(PreviewViewport::GetPresentation() == ViewportPresentation::SharedDevice);
    REQUIRE(preview.Start() == true);
    QElapsedTimer timer;
    timer.start();
    while (viewport.PresentedFrames() < PRESENTED_FRAMES && timer.elapsed() < SHARED_TIMEOUT_MILLISECONDS)
    {
        QTest::qWait(POLL_MILLISECONDS);
    }
    INFO(preview.Status().toStdString());
    // The shared path presents through Qt Quick, never through the readback canvas.
    CHECK(viewport.findChild<QWidget*>(QStringLiteral("LudusOffscreenViewport")) == nullptr);
    QWindow* window = FindWindow(QStringLiteral("LudusGpuViewport"));
    REQUIRE(window != nullptr);
    REQUIRE(preview.Running() == true);
    REQUIRE(viewport.PresentedFrames() >= PRESENTED_FRAMES);
    CHECK(preview.Status().contains(QStringLiteral("Lit: %1 lights").arg(tests::LAMP_COUNT)) == true);
    // Pixels are not asserted here: QQuickWindow::grabWindow re-renders outside
    // beforeFrameBegin/afterFrameEnd and may sample a lease the engine has already
    // reclaimed. Presentation through the shared device is the contract under test.
    const auto awaitFrames = [&viewport, &preview](uint64 target) {
        return AwaitPresentedFrames(viewport, preview, target);
    };
    // Mode changes and resizes keep presenting through the same adopted device.
    preview.SetShading(PreviewShading::Solid);
    CHECK(awaitFrames(viewport.PresentedFrames() + PRESENTED_FRAMES) == true);
    viewport.resize(VIEWPORT_WIDTH / 2, VIEWPORT_HEIGHT / 2);
    CHECK(awaitFrames(viewport.PresentedFrames() + PRESENTED_FRAMES) == true);
    // Stop retires the RHI and Qt Quick before the editor destroys its device;
    // a restart creates and shares a fresh device.
    preview.Stop();
    QTest::qWait(POLL_MILLISECONDS);
    CHECK(FindWindow(QStringLiteral("LudusGpuViewport")) == nullptr);
    REQUIRE(preview.Start() == true);
    CHECK(awaitFrames(viewport.PresentedFrames() + PRESENTED_FRAMES) == true);
    CHECK(FindWindow(QStringLiteral("LudusGpuViewport")) != nullptr);
    CHECK(viewport.findChild<QWidget*>(QStringLiteral("LudusOffscreenViewport")) == nullptr);
    preview.Stop();
#else
    SKIP("Editor-created Vulkan device sharing is Linux-only");
#endif
}

TEST_CASE("Linux shared viewport keeps presenting after it is hidden and shown", "[editor][viewport][shared][gpu]")
{
#if defined(LUDUS_EDITOR_NATIVE_PREVIEW) && defined(Q_OS_LINUX)
    const QString skipReason = GetSharedDisplaySkipReason();
    if (skipReason.isEmpty() == false)
    {
        SKIP(skipReason.toStdString());
    }
    rhi::Shutdown();
    struct Guard final
    {
        ~Guard()
        {
            rhi::Shutdown();
        }
    } guard;
    QTemporaryDir directory;
    REQUIRE(directory.isValid() == true);
    RenderScene scene;
    tests::MakeLitScene(QFileInfo(directory.path()).canonicalFilePath(), scene);
    RenderPreview preview;
    // The host stands in for the editor window: hiding only the viewport is a tab
    // or page switch, hiding the host also unmaps the parent of the Qt Quick window.
    QWidget host;
    QVBoxLayout* layout = new QVBoxLayout(&host);
    PreviewViewport* viewport = new PreviewViewport(&preview, &host);
    layout->addWidget(viewport);
    host.resize(VIEWPORT_WIDTH, VIEWPORT_HEIGHT);
    host.show();
    REQUIRE(QTest::qWaitForWindowExposed(&host) == true);
    REQUIRE(preview.SetScene(scene) == true);
    preview.SetShading(PreviewShading::Solid);
    REQUIRE(PreviewViewport::GetPresentation() == ViewportPresentation::SharedDevice);
    REQUIRE(preview.Start() == true);
    REQUIRE(AwaitPresentedFrames(*viewport, preview, PRESENTED_FRAMES) == true);
    QWindow* window = FindWindow(QStringLiteral("LudusGpuViewport"));
    REQUIRE(window != nullptr);
    const auto checkResumes = [&](QWidget& hidden) {
        for (int32 cycle = 0; cycle < VISIBILITY_CYCLES; ++cycle)
        {
            hidden.hide();
            QTest::qWait(HIDDEN_MILLISECONDS);
            hidden.show();
            REQUIRE(QTest::qWaitForWindowExposed(&host) == true);
            INFO("cycle " << cycle << ": " << preview.Status().toStdString());
            REQUIRE(preview.Running() == true);
            // Qt Quick renders only an exposed window; without exposure the view freezes.
            CHECK(AwaitPresentedFrames(*viewport, preview, viewport->PresentedFrames() + PRESENTED_FRAMES) == true);
            CHECK(window->isVisible() == true);
            CHECK(window->isExposed() == true);
        }
    };
    SECTION("viewport page switch")
    {
        checkResumes(*viewport);
    }
    SECTION("editor window hide")
    {
        checkResumes(host);
    }
    // Shared presentation stays on the adopted device, never the readback canvas.
    CHECK(FindWindow(QStringLiteral("LudusGpuViewport")) == window);
    CHECK(viewport->findChild<QWidget*>(QStringLiteral("LudusOffscreenViewport")) == nullptr);
    preview.Stop();
#else
    SKIP("Editor-created Vulkan device sharing is Linux-only");
#endif
}
