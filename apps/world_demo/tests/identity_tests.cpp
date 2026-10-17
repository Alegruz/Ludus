#include "identity_fixtures.h"
#include "internal/world.h"
#include "levels.h"

#include <ludus/content/identity_records/migration_manifest.h>
#include <ludus/content/identity_records/world_records.h>

#include <catch2/catch_test_macros.hpp>

#include <string_view>

namespace
{
using namespace ludus::world_demo;
namespace identity_records = ludus::content::identity_records;
using ludus::foundation::identity::SourceObjectId;

struct Migration final
{
    identity_records::MigrationManifest Manifest;
    ludus::foundation::Array<identity_records::EntityReferenceProperty> References;

    Migration()
    {
        identity_records::IdentityRecordError error;
        REQUIRE(Manifest.Read(tests::IDENTITY_MIGRATION, error) == identity_records::IdentityRecordStatus::Success);
        REQUIRE(identity_records::ReadEntityReferenceProperties(tests::WORLD_SCHEMA, References, error) ==
                identity_records::IdentityRecordStatus::Success);
    }

    [[nodiscard]] ludus::content::Bytes Apply(std::string_view path, std::string_view world) const
    {
        identity_records::IdentityRecordError error;
        ludus::content::Bytes output;
        REQUIRE(identity_records::ApplyWorldMigration(Manifest,
                                                      { .Path = path, .WorldJson = world },
                                                      {References.GetData(), References.GetSize()},
                                                      output,
                                                      error) == identity_records::IdentityRecordStatus::Success);
        return output;
    }
};

struct Room final
{
    std::string_view Path;
    std::string_view World;
};

constexpr Room ROOMS[] = {
    { .Path = "levels/first-room.world.json", .World = content::FirstRoom },
    { .Path = "levels/second-room.world.json", .World = content::SecondRoom },
};

[[nodiscard]] uint64 GetBuiltReplayHash(const Level& level)
{
    GameWorld world;
    REQUIRE(world.Prepare(level) == Status::Success);
    while (world.IsBuilt() == false)
    {
        REQUIRE(world.BuildNext() == Status::Success);
    }
    return world.GetReplayHash();
}

/// Replace `size` bytes of `text` at `at` with `to`.
[[nodiscard]] std::string_view
ReplaceAt(std::string_view text, usize at, usize size, std::string_view to, ludus::foundation::Array<char>& outStorage)
{
    outStorage.Clear();
    for (const std::string_view part : {text.substr(0, at), to, text.substr(at + size)})
    {
        for (const char item : part)
        {
            REQUIRE(outStorage.TryAdd(item) == true);
        }
    }
    return {outStorage.GetData(), outStorage.GetSize()};
}

/// Replace the first occurrence of `from` in `text` with `to`.
[[nodiscard]] std::string_view
Replace(std::string_view text, std::string_view from, std::string_view to, ludus::foundation::Array<char>& outStorage)
{
    const usize at = text.find(from);
    REQUIRE(at != std::string_view::npos);
    return ReplaceAt(text, at, from.size(), to, outStorage);
}
} // namespace

TEST_CASE("The committed manifest covers every committed World level exactly", "[world][identity]")
{
    const Migration migration;
    REQUIRE(migration.Manifest.GetDocuments().size() == std::size(ROOMS));
    REQUIRE(migration.References.GetSize() == 1);
    CHECK(migration.References[0].Component.View() == "enemy");
    CHECK(migration.References[0].Property.View() == "target");
    for (const Room& room : ROOMS)
    {
        const ludus::content::Bytes first = migration.Apply(room.Path, room.World);
        const ludus::content::Bytes second = migration.Apply(room.Path, room.World);
        // Replay is deterministic and never mints: identical bytes every time.
        CHECK(first.String() == second.String());
    }
}

TEST_CASE("Version-2 records load the same level as their version-1 source", "[world][identity]")
{
    const Migration migration;
    for (const Room& room : ROOMS)
    {
        Level scoped;
        REQUIRE(ReadWorldLevel(room.World, scoped).Error == LevelError::None);
        // Loading version 1 never mints identities.
        CHECK(scoped.Document.IsNil() == true);
        CHECK(scoped.SettingsObject.IsNil() == true);

        const ludus::content::Bytes migrated = migration.Apply(room.Path, room.World);
        Level recorded;
        REQUIRE(ReadWorldLevel(migrated.String(), recorded).Error == LevelError::None);
        REQUIRE(ReadLevel(migrated.String(), recorded).Error == LevelError::None);

        const identity_records::MigrationDocument* const documentOrNull = migration.Manifest.FindDocument(room.Path);
        REQUIRE(documentOrNull != nullptr);
        CHECK(recorded.Document == documentOrNull->Document);
        CHECK(recorded.SettingsObject == migration.Manifest.FindObject(*documentOrNull, "level-settings")->Object);

        CHECK(recorded.Id == scoped.Id);
        REQUIRE(recorded.EntityCount == scoped.EntityCount);
        for (usize i = 0; i < recorded.EntityCount; ++i)
        {
            const Recipe& recipe = recorded.Entities[i];
            CHECK(recipe.Id == scoped.Entities[i].Id);
            CHECK(recipe.Target == scoped.Entities[i].Target);
            CHECK(scoped.Entities[i].Object.IsNil() == true);
            const identity_records::MigrationObject* const objectOrNull =
                migration.Manifest.FindObject(*documentOrNull, recipe.Id.View());
            REQUIRE(objectOrNull != nullptr);
            CHECK(recipe.Object == objectOrNull->Object);
        }
        REQUIRE(recorded.CollisionCount == scoped.CollisionCount);
        for (usize i = 0; i < recorded.CollisionCount; ++i)
        {
            CHECK(recorded.Collision[i].Id == scoped.Collision[i].Id);
            CHECK(recorded.Collision[i].Object.IsNil() == false);
        }
        // Labels keep runtime lookup and the deterministic replay hash unchanged.
        CHECK(GetBuiltReplayHash(recorded) == GetBuiltReplayHash(scoped));
    }
}

TEST_CASE("Version-2 World records reject broken identities", "[world][identity]")
{
    const Migration migration;
    const ludus::content::Bytes migrated = migration.Apply(ROOMS[0].Path, ROOMS[0].World);
    const std::string_view valid = migrated.String();
    const identity_records::MigrationDocument& document = *migration.Manifest.FindDocument(ROOMS[0].Path);
    char playerText[ludus::foundation::identity::UUID_TEXT_BUFFER_BYTES];
    char guardText[ludus::foundation::identity::UUID_TEXT_BUFFER_BYTES];
    char documentText[ludus::foundation::identity::UUID_TEXT_BUFFER_BYTES];
    REQUIRE(ludus::foundation::identity::FormatUuid(
                migration.Manifest.FindObject(document, "player-start")->Object.Value,
                playerText,
                sizeof(playerText)) == ludus::foundation::identity::UuidStatus::Success);
    REQUIRE(ludus::foundation::identity::FormatUuid(migration.Manifest.FindObject(document, "guard-a")->Object.Value,
                                                    guardText,
                                                    sizeof(guardText)) ==
            ludus::foundation::identity::UuidStatus::Success);
    REQUIRE(ludus::foundation::identity::FormatUuid(document.Document.Value, documentText, sizeof(documentText)) ==
            ludus::foundation::identity::UuidStatus::Success);
    const std::string_view player(playerText, ludus::foundation::identity::UUID_TEXT_LENGTH);
    const std::string_view guard(guardText, ludus::foundation::identity::UUID_TEXT_LENGTH);
    const std::string_view documentUuid(documentText, ludus::foundation::identity::UUID_TEXT_LENGTH);

    ludus::foundation::Array<char> storage;
    Level level;
    REQUIRE(ReadWorldLevel(valid, level).Error == LevelError::None);
    const Level before = level;

    // The enemy target is now a UUID; one that names no entity is an invalid reference.
    const std::string_view targetMember = "\"target\": \"";
    const usize targetAt = valid.find(targetMember);
    REQUIRE(targetAt != std::string_view::npos);
    const usize targetValueAt = targetAt + targetMember.size();
    REQUIRE(valid.substr(targetValueAt, player.size()) == player);
    CHECK(
        ReadWorldLevel(ReplaceAt(valid, targetValueAt, player.size(), "eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee", storage),
                       level)
            .Error == LevelError::InvalidReference);
    // A label is not a version-2 reference.
    CHECK(ReadWorldLevel(ReplaceAt(valid, targetValueAt, player.size(), "player-start", storage), level).Error ==
          LevelError::InvalidValue);

    // Two entities with one UUID, a nil or non-v4 document, and version-1 member names are rejected.
    CHECK(ReadWorldLevel(Replace(valid, guard, player, storage), level).Error == LevelError::DuplicateField);
    CHECK(ReadWorldLevel(Replace(valid, documentUuid, "00000000-0000-0000-0000-000000000000", storage), level).Error ==
          LevelError::InvalidValue);
    CHECK(ReadWorldLevel(Replace(valid, documentUuid, "aaaaaaaa-aaaa-1aaa-8aaa-aaaaaaaaaaaa", storage), level).Error ==
          LevelError::InvalidValue);
    CHECK(ReadWorldLevel(Replace(valid, "\"label\"", "\"id\"", storage), level).Error == LevelError::InvalidValue);
    CHECK(ReadWorldLevel(Replace(valid, "\"version\": 2", "\"version\": 3", storage), level).Error ==
          LevelError::InvalidValue);

    // Failures preserve the previous output.
    CHECK(level.Document == before.Document);
    CHECK(level.Entities[0].Object == before.Entities[0].Object);
}
