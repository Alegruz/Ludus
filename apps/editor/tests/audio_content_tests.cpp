#include "internal/audio_preview.h"
#include "internal/audio_workspace.h"
#include "wav_fixture.h"
#include <QApplication>
#include <QDoubleSpinBox>
#include <QListWidget>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <ludus/audio/content/definitions.h>

namespace
{
using namespace ludus::foundation;
namespace content = ludus::content;
namespace audio = ludus::audio;
void Save(std::string_view root, std::string_view path, const content::Bytes& bytes)
{
    REQUIRE(content::SaveFile(root, path, bytes.Data(), nullptr) == content::Status::Ok);
}
void Fixture(const QString& path)
{
    const auto root = path.toUtf8();
    content::Catalog catalog;
    content::Resource source, soundResource;
    REQUIRE(source.Id.Set("source/hit"));
    REQUIRE(source.Path.Set("hit.wav"));
    REQUIRE(catalog.Put(source) == content::Status::Ok);
    REQUIRE(soundResource.Id.Set("sound/hit"));
    REQUIRE(soundResource.Path.Set("hit.json"));
    soundResource.Type = content::Kind::Sound;
    REQUIRE(catalog.Put(soundResource) == content::Status::Ok);
    content::Bytes bytes;
    REQUIRE(catalog.Write(bytes) == content::Status::Ok);
    Save(root.constData(), "catalog.json", bytes);
    audio::content::Sound sound;
    REQUIRE(sound.Id.Set("sound/hit"));
    REQUIRE(sound.Bus.Set("sfx"));
    REQUIRE(sound.Group.Set("default"));
    REQUIRE(sound.Variations[0].Set("source/hit"));
    sound.VariationCount = 1;
    REQUIRE(audio::content::WriteSound(sound, bytes) == content::Status::Ok);
    Save(root.constData(), "hit.json", bytes);
    const auto wav = audio::test::MakeSineWav(44100, 1, 4410);
    REQUIRE(content::SaveFile(root.constData(), "hit.wav", wav, nullptr) == content::Status::Ok);
}
void Activate(ludus::editor::AudioWorkspace& workspace)
{
    auto* list = workspace.findChild<QListWidget*>(QStringLiteral("audio-resources"));
    REQUIRE(list != nullptr);
    for (int i = 0; i < list->count(); ++i)
    {
        if (list->item(i)->text() == QStringLiteral("sound/hit"))
        {
            list->setCurrentRow(i);
            Q_EMIT list->itemActivated(list->item(i));
            return;
        }
    }
    FAIL("sound resource missing");
}
void ClickSave(ludus::editor::AudioWorkspace& workspace)
{
    for (auto* button : workspace.findChildren<QPushButton*>())
    {
        if (button->text() == QStringLiteral("Save audio"))
        {
            button->click();
            return;
        }
    }
    FAIL("save action missing");
}
} // namespace
TEST_CASE("Audio editor saves drafts, reopens canonical data and preserves external conflicts", "[editor][audio]")
{
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    Fixture(directory.path());
    ludus::editor::AudioWorkspace workspace;
    workspace.SetRoot(directory.path());
    Activate(workspace);
    auto* gain = workspace.findChild<QDoubleSpinBox*>(QStringLiteral("audio-gain"));
    REQUIRE(gain != nullptr);
    gain->setValue(0.25);
    ClickSave(workspace);
    ludus::editor::AudioWorkspace reopened;
    reopened.SetRoot(directory.path());
    Activate(reopened);
    auto* saved = reopened.findChild<QDoubleSpinBox*>(QStringLiteral("audio-gain"));
    REQUIRE(saved != nullptr);
    REQUIRE(saved->value() == 0.25);
    const auto root = directory.path().toUtf8();
    content::Bytes original;
    REQUIRE(content::ReadFile(root.constData(), "hit.json", content::MAX_DOCUMENT_BYTES, original) ==
            content::Status::Ok);
    const auto digest = content::Hash(original.Data());
    const uint8 invalid[] = {'{', '}'};
    REQUIRE(content::SaveFile(root.constData(), "hit.json", invalid, &digest) == content::Status::Ok);
    gain->setValue(0.75);
    ClickSave(workspace);
    REQUIRE(gain->value() == 0.75);
    content::Bytes current;
    REQUIRE(content::ReadFile(root.constData(), "hit.json", 100, current) == content::Status::Ok);
    REQUIRE(current.String() == "{}");
}
TEST_CASE("Audio import runs away from GUI, preserves ID on reimport and quiesces on close",
          "[editor][audio][concurrency]")
{
    QTemporaryDir directory, exported;
    REQUIRE(directory.isValid());
    REQUIRE(exported.isValid());
    const auto wav = audio::test::MakeSineWav(44100, 1, 44100);
    const auto exportRoot = exported.path().toUtf8();
    REQUIRE(content::SaveFile(exportRoot.constData(), "export.wav", wav, nullptr) == content::Status::Ok);
    std::atomic<uint32> imported{0};
    std::atomic<bool> importedOnGui{false};
    ludus::editor::AudioPreview preview;
    const auto* guiThread = QThread::currentThread();
    // Observe the emitting thread directly: a fast import need not outlast a GUI timer.
    QObject::connect(
        &preview,
        &ludus::editor::AudioPreview::Imported,
        &preview,
        [&imported, &importedOnGui, guiThread]() {
            if (QThread::currentThread() == guiThread)
            {
                importedOnGui.store(true, std::memory_order_relaxed);
            }
            imported.fetch_add(1, std::memory_order_release);
        },
        Qt::DirectConnection);
    preview.Import(directory.path(), exported.path() + QStringLiteral("/export.wav"), QStringLiteral("source/hit"));
    for (int i = 0; i < 500 && imported.load(std::memory_order_acquire) == 0; ++i)
    {
        QTest::qWait(10);
    }
    REQUIRE(imported.load(std::memory_order_acquire) == 1);
    REQUIRE_FALSE(importedOnGui.load(std::memory_order_relaxed));
    preview.Import(directory.path(), exported.path() + QStringLiteral("/export.wav"), QStringLiteral("source/hit"));
    for (int i = 0; i < 500 && imported.load(std::memory_order_acquire) < 2; ++i)
    {
        QTest::qWait(10);
    }
    REQUIRE(imported.load(std::memory_order_acquire) == 2);
    REQUIRE_FALSE(importedOnGui.load(std::memory_order_relaxed));
    const auto root = directory.path().toUtf8();
    content::Bytes bytes;
    content::Catalog catalog;
    content::Diagnostic diagnostic;
    REQUIRE(content::ReadFile(root.constData(), "catalog.json", content::MAX_DOCUMENT_BYTES, bytes) ==
            content::Status::Ok);
    REQUIRE(catalog.Read(bytes.String(), diagnostic) == content::Status::Ok);
    REQUIRE(catalog.Entries().size() == 1);
    REQUIRE(catalog.Find("source/hit") != nullptr);
    preview.Stop();
    preview.Shutdown();
    for (int i = 0; i < 500 && !preview.Finished(); ++i)
    {
        QTest::qWait(10);
    }
    REQUIRE(preview.Finished());
}
