#include <ludus/content/identity_records/world_records.h>

#include <catch2/catch_test_macros.hpp>

#include <string_view>

namespace
{
using namespace ludus::content;
using namespace ludus::content::identity_records;

constexpr std::string_view SCHEMA = R"({"version": 1, "id": "game.v1", "label": "Game", "components": [
  {"id": "transform", "label": "T", "help": "", "required": false, "properties": [
    {"id": "x", "label": "X", "help": "", "unit": "m", "type": "number", "default": 0}]},
  {"id": "enemy", "label": "E", "help": "", "required": false, "properties": [
    {"id": "target", "label": "Target", "help": "", "unit": "", "type": "entity", "default": ""},
    {"id": "sprite", "label": "Sprite", "help": "", "unit": "", "type": "string", "default": ""}]}],
  "assets": []})";

constexpr std::string_view MANIFEST = R"({"format": "ludus.identity-migration", "version": 1, "documents": [
  {"path": "levels/room.world.json", "document": "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa", "objects": [
    {"legacy_id": "guard-a", "object": "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb"},
    {"legacy_id": "loner", "object": "dddddddd-dddd-4ddd-8ddd-dddddddddddd"},
    {"legacy_id": "player-start", "object": "cccccccc-cccc-4ccc-8ccc-cccccccccccc"}]}]})";

// Compact input, as the Editor saves it; entity order differs from manifest order.
constexpr std::string_view WORLD =
    R"({"version":1,"schema":"game.v1","entities":[{"id":"player-start","name":"Player \"one\"","components":)"
    R"({"transform":{"x":-6}}},{"id":"guard-a","name":"Guard","components":{"enemy":{"target":"player-start",)"
    R"("sprite":"guard"},"transform":{"x":0.5}}},{"id":"loner","name":"Loner","components":{"enemy":)"
    R"({"target":"","sprite":"x"}}}]})";

constexpr std::string_view EXPECTED = R"({
  "version": 2,
  "schema": "game.v1",
  "document": "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
  "entities": [
    {
      "uuid": "cccccccc-cccc-4ccc-8ccc-cccccccccccc",
      "label": "player-start",
      "name": "Player \"one\"",
      "components": {
        "transform": {
          "x": -6
        }
      }
    },
    {
      "uuid": "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb",
      "label": "guard-a",
      "name": "Guard",
      "components": {
        "enemy": {
          "target": "cccccccc-cccc-4ccc-8ccc-cccccccccccc",
          "sprite": "guard"
        },
        "transform": {
          "x": 0.5
        }
      }
    },
    {
      "uuid": "dddddddd-dddd-4ddd-8ddd-dddddddddddd",
      "label": "loner",
      "name": "Loner",
      "components": {
        "enemy": {
          "target": "",
          "sprite": "x"
        }
      }
    }
  ]
}
)";

struct Fixture final
{
    MigrationManifest Manifest;
    ludus::foundation::Array<EntityReferenceProperty> References;

    Fixture()
    {
        IdentityRecordError error;
        REQUIRE(Manifest.Read(MANIFEST, error) == IdentityRecordStatus::Success);
        REQUIRE(ReadEntityReferenceProperties(SCHEMA, References, error) == IdentityRecordStatus::Success);
    }

    [[nodiscard]] IdentityRecordStatus Apply(std::string_view world, Bytes& outJson) const
    {
        IdentityRecordError error;
        return ApplyWorldMigration(Manifest,
                                   { .Path = "levels/room.world.json", .WorldJson = world },
                                   {References.GetData(), References.GetSize()},
                                   outJson,
                                   error);
    }
};
} // namespace

TEST_CASE("Schema entity properties are collected", "[identity][world]")
{
    const Fixture fixture;
    REQUIRE(fixture.References.GetSize() == 1);
    CHECK(fixture.References[0].Component.View() == "enemy");
    CHECK(fixture.References[0].Property.View() == "target");

    ludus::foundation::Array<EntityReferenceProperty> properties;
    IdentityRecordError error;
    CHECK(ReadEntityReferenceProperties("{}", properties, error) == IdentityRecordStatus::Malformed);
}

TEST_CASE("Replaying a manifest turns a version-1 World into a deterministic version-2 record", "[identity][world]")
{
    const Fixture fixture;
    Bytes first;
    REQUIRE(fixture.Apply(WORLD, first) == IdentityRecordStatus::Success);
    CHECK(first.String() == EXPECTED);

    Bytes second;
    REQUIRE(fixture.Apply(WORLD, second) == IdentityRecordStatus::Success);
    CHECK(second.String() == first.String());
}

TEST_CASE("Replay reports a stale manifest instead of minting or dropping identities", "[identity][world]")
{
    const Fixture fixture;
    Bytes output;
    REQUIRE(output.Resize(3) == true);

    constexpr std::string_view EXTRA =
        R"({"version":1,"schema":"game.v1","entities":[{"id":"player-start","name":"P","components":{}},)"
        R"({"id":"guard-a","name":"G","components":{}},{"id":"loner","name":"L","components":{}},)"
        R"({"id":"new-one","name":"N","components":{}}]})";
    CHECK(fixture.Apply(EXTRA, output) == IdentityRecordStatus::StaleManifest);

    constexpr std::string_view MISSING =
        R"({"version":1,"schema":"game.v1","entities":[{"id":"player-start","name":"P","components":{}}]})";
    CHECK(fixture.Apply(MISSING, output) == IdentityRecordStatus::StaleManifest);

    constexpr std::string_view REPEATED =
        R"({"version":1,"schema":"game.v1","entities":[{"id":"player-start","name":"P","components":{}},)"
        R"({"id":"player-start","name":"G","components":{}},{"id":"loner","name":"L","components":{}}]})";
    CHECK(fixture.Apply(REPEATED, output) == IdentityRecordStatus::InvalidLegacyId);

    constexpr std::string_view DANGLING =
        R"({"version":1,"schema":"game.v1","entities":[{"id":"player-start","name":"P","components":{}},)"
        R"({"id":"guard-a","name":"G","components":{"enemy":{"target":"nobody","sprite":""}}},)"
        R"({"id":"loner","name":"L","components":{}}]})";
    CHECK(fixture.Apply(DANGLING, output) == IdentityRecordStatus::UnresolvedReference);

    // Failures leave the caller's output unchanged.
    CHECK(output.Data().size() == 3);
}

TEST_CASE("Replay accepts only version-1 envelopes it can migrate", "[identity][world]")
{
    const Fixture fixture;
    Bytes output;
    CHECK(fixture.Apply(EXPECTED, output) == IdentityRecordStatus::UnsupportedVersion);
    CHECK(fixture.Apply(R"({"version":1,"schema":"game.v1","entities":[],"scene":{}})", output) ==
          IdentityRecordStatus::Unsupported);
    CHECK(fixture.Apply("not json", output) == IdentityRecordStatus::Malformed);

    IdentityRecordError error;
    CHECK(ApplyWorldMigration(fixture.Manifest,
                              { .Path = "levels/other.world.json", .WorldJson = WORLD },
                              {},
                              output,
                              error) == IdentityRecordStatus::UnknownDocument);
    CHECK(error.Message.View().ends_with("levels/other.world.json") == true);
}
