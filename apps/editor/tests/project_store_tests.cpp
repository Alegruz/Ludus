// Offscreen tests for ProjectStore parsing, canonical serialization, atomic save
// with conflict detection, and failure preservation (design 4; E03-E05). These
// verify destination bytes and dirty/saved state after failures, not just that a
// serializer produced an expected string.

#include "internal/project_descriptor.h"
#include "internal/project_store.h"

#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

using namespace ludus::editor;

namespace
{
QByteArray ReadFixture(const char* name)
{
    QFile file(QStringLiteral(LUDUS_EDITOR_SOURCE_DIR) + QStringLiteral("/tests/fixtures/") + QString::fromUtf8(name));
    REQUIRE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

ProjectDescriptor SampleDescriptor()
{
    ProjectDescriptor descriptor;
    descriptor.Name = QStringLiteral("demo");
    descriptor.ProviderKind = Provider::Cmake;
    descriptor.SourceDir = QStringLiteral(".");
    descriptor.Preset = QStringLiteral("linux-clang-debug");
    descriptor.Target = QStringLiteral("app");
    descriptor.RunCwd = QStringLiteral(".");
    descriptor.RunArgs = QStringList{QString(), QStringLiteral("a b"), QStringLiteral("日本語")};
    return descriptor;
}
} // namespace

TEST_CASE("Shared fixtures parse with the same verdicts as the Python validator", "[editor][store]")
{
    CHECK(ParseDescriptor(ReadFixture("valid_ludus.json")).Ok());
    CHECK(ParseDescriptor(ReadFixture("valid_cmake_unicode_args.json")).Ok());
    CHECK(ParseDescriptor(ReadFixture("invalid_bad_version.json")).Code == ResultCode::UnsupportedVersion);
    CHECK(ParseDescriptor(ReadFixture("invalid_unknown_field.json")).Code == ResultCode::InvalidProject);
    CHECK(ParseDescriptor(ReadFixture("invalid_absolute_source.json")).Code == ResultCode::InvalidProject);
    CHECK(ParseDescriptor(ReadFixture("invalid_bad_target.json")).Code == ResultCode::InvalidProject);
}

TEST_CASE("Empty, quoted, Unicode and shell-looking args round-trip", "[editor][store]")
{
    const QByteArray fixture = ReadFixture("valid_cmake_unicode_args.json");
    const ParseOutcome parsed = ParseDescriptor(fixture);
    REQUIRE(parsed.Ok());
    REQUIRE(parsed.Descriptor.RunArgs.size() == 6);
    CHECK(parsed.Descriptor.RunArgs.at(0).isEmpty());
    CHECK(parsed.Descriptor.RunArgs.at(3) == QStringLiteral("$(echo hi)"));
    // Serialize then reparse: a stable round-trip (same descriptor).
    const QByteArray serialized = SerializeDescriptor(parsed.Descriptor);
    const ParseOutcome reparsed = ParseDescriptor(serialized);
    REQUIRE(reparsed.Ok());
    CHECK(reparsed.Descriptor == parsed.Descriptor);
}

TEST_CASE("Over-limit descriptor and NUL are rejected", "[editor][store]")
{
    QByteArray huge(static_cast<int>(limits::MaxFileBytes) + 1, '0');
    CHECK(ParseDescriptor(huge).Code == ResultCode::InvalidProject);
    QByteArray withNul = ReadFixture("valid_ludus.json");
    withNul.append('\0');
    CHECK_FALSE(ParseDescriptor(withNul).Ok());
}

TEST_CASE("Save then reopen round-trip survives", "[editor][store]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("ludus.project.json"));
    ProjectStore store;
    const ProjectDescriptor descriptor = SampleDescriptor();
    const SaveOutcome saved = store.Save(path, descriptor, QString());
    REQUIRE(saved.Ok());
    const LoadOutcome loaded = store.Load(path);
    REQUIRE(loaded.Ok());
    CHECK(loaded.Parse.Descriptor == descriptor);
    CHECK(loaded.Digest == saved.Digest);
}

TEST_CASE("Short write preserves the previous destination and reports failure", "[editor][store]")
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("ludus.project.json"));
    ProjectStore store;
    // First a good save to establish the destination bytes.
    const ProjectDescriptor first = SampleDescriptor();
    REQUIRE(store.Save(path, first, QString()).Ok());
    const QByteArray before = store.Load(path).Bytes;
    const QString digest = store.Load(path).Digest;

    // Now inject a short write for a different draft; destination must be intact.
    ProjectStore::FaultHooks hooks;
    hooks.TruncateWriteTo = [](const QByteArray& bytes) {
        return static_cast<ludus::foundation::isize>(bytes.size() / 2);
    };
    store.SetFaultHooks(hooks);
    ProjectDescriptor second = first;
    second.Name = QStringLiteral("changed");
    const SaveOutcome failed = store.Save(path, second, digest);
    CHECK_FALSE(failed.Ok());
    CHECK(store.Load(path).Bytes == before); // old saved file retained
}

TEST_CASE("Commit failure preserves destination", "[editor][store]")
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("ludus.project.json"));
    ProjectStore store;
    REQUIRE(store.Save(path, SampleDescriptor(), QString()).Ok());
    const QByteArray before = store.Load(path).Bytes;
    const QString digest = store.Load(path).Digest;

    ProjectStore::FaultHooks hooks;
    hooks.FailCommit = []() { return true; };
    store.SetFaultHooks(hooks);
    ProjectDescriptor changed = SampleDescriptor();
    changed.Target = QStringLiteral("other");
    CHECK_FALSE(store.Save(path, changed, digest).Ok());
    CHECK(store.Load(path).Bytes == before);
}

TEST_CASE("External change produces Conflict and keeps the draft", "[editor][store]")
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("ludus.project.json"));
    ProjectStore store;
    REQUIRE(store.Save(path, SampleDescriptor(), QString()).Ok());
    const QString staleDigest = QStringLiteral("0000000000000000000000000000000000000000000000000000000000000000");
    ProjectDescriptor changed = SampleDescriptor();
    changed.Name = QStringLiteral("conflict");
    const SaveOutcome outcome = store.Save(path, changed, staleDigest);
    CHECK(outcome.Code == ResultCode::Conflict);
}

TEST_CASE("External deletion produces Conflict", "[editor][store]")
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("ludus.project.json"));
    ProjectStore store;
    const SaveOutcome first = store.Save(path, SampleDescriptor(), QString());
    REQUIRE(first.Ok());
    REQUIRE(QFile::remove(path));
    ProjectDescriptor changed = SampleDescriptor();
    changed.Name = QStringLiteral("deleted");
    CHECK(store.Save(path, changed, first.Digest).Code == ResultCode::Conflict);
}
