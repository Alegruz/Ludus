#include "internal/scene_preview.h"
#include "internal/scene_store.h"
#import <Cocoa/Cocoa.h>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>
#include <catch2/catch_test_macros.hpp>

using namespace ludus::editor;
namespace
{
void NativeKey(NSWindow* window, ludus::foundation::uint16 key, bool down)
{
    auto* event = [NSEvent keyEventWithType:down ? NSEventTypeKeyDown : NSEventTypeKeyUp
                                   location:NSZeroPoint
                              modifierFlags:0
                                  timestamp:0
                               windowNumber:window.windowNumber
                                    context:nil
                                 characters:@""
                charactersIgnoringModifiers:@""
                                  isARepeat:NO
                                    keyCode:key];
    [window sendEvent:event];
}
bool Frames(ScenePreview& preview, ludus::foundation::uint64 target)
{
    QElapsedTimer timer;
    timer.start();
    while (preview.PresentedFrames() < target && timer.elapsed() < 5000)
    {
        QTest::qWait(20);
    }
    return preview.PresentedFrames() >= target;
}
} // namespace
TEST_CASE("Cocoa scene authoring presents Metal frames and accepts native keyboard transforms", "[.scene-native]")
{
    REQUIRE(QApplication::platformName() == QStringLiteral("cocoa"));
    SceneDocument document;
    const auto bytes = ludus::world_demo::ExampleLevel();
    REQUIRE(document.Load(bytes.data(), bytes.size()).Error == ludus::world_demo::LevelError::None);
    ScenePreview preview(&document);
    REQUIRE(preview.Start());
    REQUIRE(Frames(preview, 2));
    auto* window = static_cast<NSWindow*>(preview.WindowInfo().CocoaWindow);
    REQUIRE(window != nil);
    [window makeKeyAndOrderFront:nil];
    QTest::qWait(40);
    const auto original = document.Draft();
    preview.Select(original.Level.Entities[0].Id);
    NativeKey(window, 124, true);
    preview.Tick();
    REQUIRE(document.Previewing());
    CHECK(document.Draft() == original);
    NativeKey(window, 124, false);
    preview.Tick();
    REQUIRE(document.Dirty());
    CHECK_FALSE(document.Previewing());
    CHECK(document.Draft().Level.Entities[0].Position.X == original.Level.Entities[0].Position.X + 0.1F);
    REQUIRE(document.Undo());
    CHECK(document.Draft() == original);
    CHECK_FALSE(document.Dirty());
    NativeKey(window, 124, true);
    preview.Tick();
    REQUIRE(document.Previewing());
    NativeKey(window, 53, true);
    preview.Tick();
    CHECK_FALSE(document.Previewing());
    CHECK(document.Draft() == original);
    NativeKey(window, 124, false);
    preview.Tick();
    const auto previous = preview.PresentedStamp();
    [window setContentSize:NSMakeSize(720, 480)];
    REQUIRE(Frames(preview, preview.PresentedFrames() + 2));
    CHECK(preview.PresentedStamp().Surface != previous.Surface);
    // Invalidate a cancelled preview frame even though document content/revision
    // is unchanged, then prove native Enter uses the currently presented stamp.
    const auto frameBefore = preview.PresentedStamp();
    NativeKey(window, 36, true);
    preview.Tick();
    CHECK(preview.Selected().View().empty());
    CHECK(preview.PresentedStamp().Document == frameBefore.Document);
    preview.Select(original.Level.Entities[0].Id);
    NativeKey(window, 124, true);
    preview.Tick();
    NativeKey(window, 124, false);
    preview.Tick();
    REQUIRE(document.Dirty());
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const auto scenePath =
        QDir(QFileInfo(directory.path()).canonicalFilePath()).filePath(QStringLiteral("saved-scene.json"));
    QFile source(scenePath);
    REQUIRE(source.open(QIODevice::WriteOnly));
    REQUIRE(source.write(bytes.data(), static_cast<ludus::foundation::int64>(bytes.size())) ==
            static_cast<ludus::foundation::int64>(bytes.size()));
    source.close();
    const QByteArray baseline(bytes.data(), static_cast<ludus::foundation::int64>(bytes.size()));
    QByteArray saved;
    REQUIRE(SaveScene(scenePath, baseline, document, saved));
    CHECK_FALSE(document.Dirty());
    SceneDocument reopened;
    REQUIRE(reopened.Load(saved.constData(), static_cast<ludus::foundation::usize>(saved.size())).Error ==
            ludus::world_demo::LevelError::None);
    CHECK(reopened.Draft() == document.Draft());
    const auto player = qEnvironmentVariable("LUDUS_SCENE_TEST_PLAYER");
    if (!player.isEmpty())
    {
        QProcess process;
        process.start(player, {QStringLiteral("--level"), scenePath, QStringLiteral("--frames"), QStringLiteral("3")});
        REQUIRE(process.waitForStarted(5000));
        REQUIRE(process.waitForFinished(10000));
        INFO(process.readAllStandardError().constData());
        CHECK(process.exitStatus() == QProcess::NormalExit);
        CHECK(process.exitCode() == 0);
        CHECK(process.readAllStandardOutput().contains(QByteArrayLiteral("presented 3 frames")));
    }
    preview.Stop();
    CHECK(document.Loaded());
    CHECK(document.Draft() == reopened.Draft());
    REQUIRE(preview.Start());
    REQUIRE(Frames(preview, 2));
    preview.Stop();
}
