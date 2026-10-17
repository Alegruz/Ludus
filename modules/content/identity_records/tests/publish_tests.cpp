#include "entropy_fixture.h"

#include <ludus/content/identity_records/migration_manifest.h>

#include <catch2/catch_test_macros.hpp>

#include <dirent.h>
#include <stdlib.h>
#include <string_view>
#include <unistd.h>

namespace
{
using namespace ludus::content;
using namespace ludus::content::identity_records;
using namespace ludus::content::identity_records::tests;

constexpr std::string_view ROOM = R"({"version": 1, "schema": "game.v1", "entities": [
  {"id": "player-start", "name": "Player", "components": {}}]})";
constexpr std::string_view MANIFEST_PATH = "identity-migration.json";

/// Temporary directory removed with its regular files at scope exit.
struct TemporaryRoot final
{
    char Path[64] = "/tmp/ludus-identity-XXXXXX";
    bool IsCreated = false;

    TemporaryRoot()
    {
        IsCreated = mkdtemp(Path) != nullptr;
    }
    ~TemporaryRoot()
    {
        DIR* directoryOrNull = opendir(Path);
        if (directoryOrNull != nullptr)
        {
            for (dirent* entryOrNull = readdir(directoryOrNull); entryOrNull != nullptr;
                 entryOrNull = readdir(directoryOrNull))
            {
                const std::string_view name = entryOrNull->d_name;
                if (name != "." && name != "..")
                {
                    unlinkat(dirfd(directoryOrNull), entryOrNull->d_name, 0);
                }
            }
            closedir(directoryOrNull);
        }
        rmdir(Path);
    }

    [[nodiscard]] ludus::foundation::usize CountEntries() const
    {
        ludus::foundation::usize count = 0;
        DIR* directoryOrNull = opendir(Path);
        REQUIRE(directoryOrNull != nullptr);
        for (dirent* entryOrNull = readdir(directoryOrNull); entryOrNull != nullptr;
             entryOrNull = readdir(directoryOrNull))
        {
            const std::string_view name = entryOrNull->d_name;
            count += name != "." && name != ".." ? 1 : 0;
        }
        closedir(directoryOrNull);
        return count;
    }
};

[[nodiscard]] MigrationManifest Generate(ludus::foundation::uint64 firstValue)
{
    CountingEntropy entropy;
    entropy.NextValue = firstValue;
    const MigrationSource sources[] = {{"room.world.json", ROOM}};
    MigrationManifest manifest;
    IdentityRecordError error;
    REQUIRE(GenerateMigrationManifest(sources, entropy.GetSource(), manifest, error) == IdentityRecordStatus::Success);
    return manifest;
}
} // namespace

TEST_CASE("A migration manifest is published once and never replaced", "[identity][publish]")
{
    const TemporaryRoot root;
    REQUIRE(root.IsCreated == true);
    const MigrationManifest manifest = Generate(1);
    IdentityRecordError error;
    REQUIRE(PublishMigrationManifest(root.Path, MANIFEST_PATH, manifest, error) == IdentityRecordStatus::Success);

    Bytes expected;
    REQUIRE(manifest.Write(expected) == IdentityRecordStatus::Success);
    Bytes published;
    REQUIRE(ReadFile(root.Path, MANIFEST_PATH, MAX_MANIFEST_BYTES, published) == Status::Ok);
    CHECK(published.String() == expected.String());

    // A second generation with different identities must not overwrite the committed one.
    const MigrationManifest regenerated = Generate(1000);
    CHECK(PublishMigrationManifest(root.Path, MANIFEST_PATH, regenerated, error) ==
          IdentityRecordStatus::AlreadyExists);
    CHECK(error.Status == IdentityRecordStatus::AlreadyExists);
    Bytes unchanged;
    REQUIRE(ReadFile(root.Path, MANIFEST_PATH, MAX_MANIFEST_BYTES, unchanged) == Status::Ok);
    CHECK(unchanged.String() == expected.String());

    // Publication leaves only the manifest: no temporary or partial file remains.
    CHECK(root.CountEntries() == 1);
}

TEST_CASE("Publishing below a missing directory writes nothing", "[identity][publish]")
{
    const TemporaryRoot root;
    REQUIRE(root.IsCreated == true);
    const MigrationManifest manifest = Generate(1);
    IdentityRecordError error;
    CHECK(PublishMigrationManifest(root.Path, "missing/identity-migration.json", manifest, error) ==
          IdentityRecordStatus::IoError);
    CHECK(root.CountEntries() == 0);
}
