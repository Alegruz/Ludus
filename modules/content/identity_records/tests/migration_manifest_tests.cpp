#include "entropy_fixture.h"

#include <ludus/content/identity_records/migration_manifest.h>

#include <catch2/catch_test_macros.hpp>

#include <string_view>

namespace
{
using namespace ludus::content;
using namespace ludus::content::identity_records;
using namespace ludus::content::identity_records::tests;

constexpr std::string_view ROOM_A = R"({"version": 1, "schema": "game.v1", "entities": [
  {"id": "player-start", "name": "Player", "components": {}},
  {"id": "guard-a", "name": "Guard", "components": {}},
  {"id": "entity-0123456789abcdef01234567", "name": "Added", "components": {}}]})";
constexpr std::string_view ROOM_B = R"({"version": 1, "schema": "game.v1", "entities": [
  {"id": "level-settings", "name": "Settings", "components": {}}]})";

[[nodiscard]] std::string_view GetText(const Bytes& bytes) noexcept
{
    return bytes.String();
}

[[nodiscard]] MigrationManifest Generate(CountingEntropy& inoutEntropy)
{
    const MigrationSource sources[] = {{"levels/room-b.world.json", ROOM_B}, {"levels/room-a.world.json", ROOM_A}};
    MigrationManifest manifest;
    IdentityRecordError error;
    REQUIRE(GenerateMigrationManifest(sources, inoutEntropy.GetSource(), manifest, error) ==
            IdentityRecordStatus::Success);
    REQUIRE(error.Status == IdentityRecordStatus::Success);
    return manifest;
}

[[nodiscard]] std::string_view ReadStatusName(std::string_view json)
{
    MigrationManifest manifest;
    IdentityRecordError error;
    return GetIdentityRecordStatusName(manifest.Read(json, error));
}
} // namespace

TEST_CASE("Migration manifests sort documents and legacy IDs and mint version-4 UUIDs", "[identity][manifest]")
{
    CountingEntropy entropy;
    const MigrationManifest manifest = Generate(entropy);
    const std::span<const MigrationDocument> documents = manifest.GetDocuments();
    REQUIRE(documents.size() == 2);
    CHECK(documents[0].Path.View() == "levels/room-a.world.json");
    CHECK(documents[1].Path.View() == "levels/room-b.world.json");
    // One document UUID and one UUID per entity, each a single entropy call.
    CHECK(entropy.CallCount == 6);

    const std::span<const MigrationObject> objects = manifest.GetObjects(documents[0]);
    REQUIRE(objects.size() == 3);
    CHECK(objects[0].LegacyId.View() == "entity-0123456789abcdef01234567");
    CHECK(objects[1].LegacyId.View() == "guard-a");
    CHECK(objects[2].LegacyId.View() == "player-start");
    for (const MigrationObject& object : objects)
    {
        CHECK(ludus::foundation::identity::ValidateUuid(object.Object.Value,
                                                        ludus::foundation::identity::UuidPolicy::RequireVersion4) ==
              ludus::foundation::identity::UuidStatus::Success);
    }
    const MigrationObject* const guardOrNull = manifest.FindObject(documents[0], "guard-a");
    REQUIRE(guardOrNull != nullptr);
    CHECK(guardOrNull == &objects[1]);
    CHECK(manifest.FindObject(documents[0], "level-settings") == nullptr);
    CHECK(manifest.FindDocument("levels/room-c.world.json") == nullptr);
}

TEST_CASE("A migration manifest replays to identical bytes", "[identity][manifest]")
{
    CountingEntropy entropy;
    const MigrationManifest generated = Generate(entropy);
    Bytes first;
    REQUIRE(generated.Write(first) == IdentityRecordStatus::Success);

    MigrationManifest replayed;
    IdentityRecordError error;
    REQUIRE(replayed.Read(GetText(first), error) == IdentityRecordStatus::Success);
    Bytes second;
    REQUIRE(replayed.Write(second) == IdentityRecordStatus::Success);
    CHECK(GetText(second) == GetText(first));
    CHECK(GetText(first).starts_with("{\n  \"format\": \"ludus.identity-migration\",\n  \"version\": 1,\n"));
    CHECK(GetText(first).ends_with("}\n"));

    // Reading never mints: the identities are exactly the generated ones.
    const MigrationDocument* const roomOrNull = replayed.FindDocument("levels/room-a.world.json");
    REQUIRE(roomOrNull != nullptr);
    CHECK(roomOrNull->Document == generated.GetDocuments()[0].Document);
    CHECK(replayed.FindObject(*roomOrNull, "player-start")->Object ==
          generated.FindObject(generated.GetDocuments()[0], "player-start")->Object);
}

TEST_CASE("Unavailable entropy mints nothing and preserves the output manifest", "[identity][manifest][entropy]")
{
    CountingEntropy original;
    MigrationManifest manifest = Generate(original);
    Bytes before;
    REQUIRE(manifest.Write(before) == IdentityRecordStatus::Success);

    const MigrationSource sources[] = {{"levels/room-a.world.json", ROOM_A}};
    IdentityRecordError error;
    CHECK(GenerateMigrationManifest(sources, MakeUnavailableEntropy(), manifest, error) ==
          IdentityRecordStatus::EntropyUnavailable);
    CHECK(error.Status == IdentityRecordStatus::EntropyUnavailable);

    // Entropy that fails partway through also publishes no partial manifest.
    CountingEntropy failing;
    failing.CallsBeforeFailure = 2;
    CHECK(GenerateMigrationManifest(sources, failing.GetSource(), manifest, error) ==
          IdentityRecordStatus::EntropyUnavailable);
    CHECK(failing.CallCount == 2);

    Bytes after;
    REQUIRE(manifest.Write(after) == IdentityRecordStatus::Success);
    CHECK(GetText(after) == GetText(before));
}

TEST_CASE("Colliding candidates are retried and then reported as a conflict", "[identity][manifest][entropy]")
{
    const MigrationSource sources[] = {{"levels/room-b.world.json", ROOM_B}};
    CountingEntropy constant;
    constant.IsConstant = true;
    MigrationManifest manifest;
    IdentityRecordError error;
    CHECK(GenerateMigrationManifest(sources, constant.GetSource(), manifest, error) ==
          IdentityRecordStatus::IdentityConflict);
    // The document UUID took one call; the object tried every candidate.
    CHECK(constant.CallCount == 1 + MAX_UUID_GENERATION_ATTEMPTS);
    CHECK(manifest.GetDocuments().empty() == true);
}

TEST_CASE("Generation rejects documents it cannot migrate", "[identity][manifest]")
{
    CountingEntropy entropy;
    MigrationManifest manifest;
    IdentityRecordError error;
    const auto generate = [&](std::string_view path, std::string_view json) {
        const MigrationSource sources[] = {{path, json}};
        return GenerateMigrationManifest(sources, entropy.GetSource(), manifest, error);
    };
    CHECK(generate("a.world.json", R"({"version": 2, "schema": "s", "document": "x", "entities": []})") ==
          IdentityRecordStatus::UnsupportedVersion);
    CHECK(generate("a.world.json", R"({"version": 1, "schema": "s", "entities": [], "scene": {}})") ==
          IdentityRecordStatus::Unsupported);
    CHECK(generate("a.world.json",
                   R"({"version": 1, "schema": "s", "entities": [{"id": "a", "name": "A", "components": {},
                       "scene": {}}]})") == IdentityRecordStatus::Unsupported);
    CHECK(generate("a.world.json", R"({"version": 1, "schema": "s", "entities": [
          {"id": "a", "name": "A", "components": {}}, {"id": "a", "name": "B", "components": {}}]})") ==
          IdentityRecordStatus::InvalidLegacyId);
    CHECK(generate("a.world.json", R"({"version": 1, "schema": "s", "entities": [
          {"id": "has space", "name": "A", "components": {}}]})") == IdentityRecordStatus::InvalidLegacyId);
    CHECK(generate("../outside.world.json", ROOM_B) == IdentityRecordStatus::InvalidLegacyId);
    CHECK(generate("a.world.json", "{") == IdentityRecordStatus::Malformed);

    const MigrationSource repeated[] = {{"a.world.json", ROOM_B}, {"a.world.json", ROOM_A}};
    CHECK(GenerateMigrationManifest(repeated, entropy.GetSource(), manifest, error) ==
          IdentityRecordStatus::InvalidLegacyId);
    CHECK(entropy.CallCount == 0);
    CHECK(manifest.GetDocuments().empty() == true);
}

TEST_CASE("Manifest reading enforces the versioned schema and identity rules", "[identity][manifest]")
{
    constexpr std::string_view VALID = R"({"format": "ludus.identity-migration", "version": 1, "documents": [
      {"path": "a.world.json", "document": "11111111-1111-4111-8111-111111111111", "objects": [
        {"legacy_id": "alpha", "object": "22222222-2222-4222-8222-222222222222"},
        {"legacy_id": "beta", "object": "33333333-3333-4333-8333-333333333333"}]}]})";
    CHECK(ReadStatusName(VALID) == "success");

    const auto replace = [&](std::string_view from, std::string_view to) {
        char text[1024] = {};
        const usize at = VALID.find(from);
        REQUIRE(at != std::string_view::npos);
        usize size = 0;
        for (const std::string_view part : {VALID.substr(0, at), to, VALID.substr(at + from.size())})
        {
            for (const char item : part)
            {
                text[size++] = item;
            }
        }
        return ReadStatusName(std::string_view(text, size));
    };
    CHECK(replace("\"version\": 1", "\"version\": 2") == "unsupported-version");
    CHECK(replace("ludus.identity-migration", "ludus.other") == "unsupported-version");
    CHECK(replace("\"version\": 1,", "\"version\": 1, \"extra\": 0,") == "malformed");
    CHECK(replace("11111111-1111-4111", "00000000-0000-0000") == "invalid-identity");
    CHECK(replace("11111111-1111-4111-8111-111111111111", "00000000-0000-0000-0000-000000000000") ==
          "invalid-identity");
    CHECK(replace("22222222-2222-4222", "22222222-2222-1222") == "invalid-identity");
    CHECK(replace("33333333-3333-4333-8333-333333333333", "11111111-1111-4111-8111-111111111111") ==
          "duplicate-identity");
    CHECK(replace("\"beta\"", "\"aaa\"") == "invalid-legacy-id");
    CHECK(replace("\"beta\"", "\"alpha\"") == "invalid-legacy-id");
    CHECK(replace("\"beta\"", "\"be ta\"") == "invalid-legacy-id");
    CHECK(replace("a.world.json", "/a.world.json") == "invalid-legacy-id");
    CHECK(ReadStatusName("[]") == "malformed");

    // A failed read keeps the previous manifest.
    MigrationManifest manifest;
    IdentityRecordError error;
    REQUIRE(manifest.Read(VALID, error) == IdentityRecordStatus::Success);
    CHECK(manifest.Read("{}", error) == IdentityRecordStatus::Malformed);
    CHECK(error.Message.View().empty() == false);
    CHECK(manifest.GetDocuments().size() == 1);
}

TEST_CASE("Legacy IDs follow the Editor World ID rule", "[identity][manifest]")
{
    CHECK(IsLegacyId("player-start") == true);
    CHECK(IsLegacyId("starter.cube") == true);
    CHECK(IsLegacyId("Ns:Item_2") == true);
    CHECK(IsLegacyId("") == false);
    CHECK(IsLegacyId("a/b") == false);
    CHECK(IsLegacyId(std::string_view("0123456789012345678901234567890123456789012345678901234567890123", 64)) == true);
    CHECK(IsLegacyId("01234567890123456789012345678901234567890123456789012345678901234") == false);
}
