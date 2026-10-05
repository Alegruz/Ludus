#include <ludus/runtime/game_api/api.h>
#include <ludus/runtime/game_api/properties.h>

#include <cstring>
#include <limits>

#include <catch2/catch_test_macros.hpp>

using namespace ludus::foundation;
using namespace ludus::runtime::game_api;
namespace
{
struct Game final
{
    GameApiTable Api{ .StructSize = sizeof(GameApiTable) };
    GameInstance* Instance = nullptr;
    Game()
    {
        REQUIRE(LudusGetGameApi(kAbiMajor, kAbiMinor, &Api) == Status::Ok);
        CreateInfo info;
        info.GameId = 1;
        REQUIRE(Api.Create(&info, &Instance) == Status::Ok);
    }
    ~Game()
    {
        Api.Destroy(Instance);
    }
    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;
    PropertyValue Speed()
    {
        uint8 bytes[sizeof(PropertyValue) * 2]{};
        usize written = 0;
        REQUIRE(Api.ReadProperties(Instance, {bytes, sizeof(bytes)}, &written) == Status::Ok);
        PropertyValue result;
        std::memcpy(&result, bytes, sizeof(result));
        return result;
    }
    Status Prepare(float32 speed, GameEditPlan** plan, bool duplicate = false)
    {
        const auto current = Speed();
        EditBatchHeader header
        {
            .StructSize = sizeof(EditBatchHeader),
            .Count = duplicate ? 2U : 1U,
            .SchemaVersion = 1,
        };
        PropertyEdit edit;
        edit.ObjectId = current.ObjectId;
        edit.PropertyId = current.PropertyId;
        edit.ExpectedRevision = current.ObjectRevision;
        edit.Kind = current.Kind;
        edit.Float = speed;
        uint8 bytes[1 + sizeof(EditBatchHeader) + sizeof(PropertyEdit) * 2]{};
        std::memcpy(bytes + 1, &header, sizeof(header));
        std::memcpy(bytes + 1 + sizeof(header), &edit, sizeof(edit));
        std::memcpy(bytes + 1 + sizeof(header) + sizeof(edit), &edit, sizeof(edit));
        return Api.PrepareEdits(Instance, {bytes + 1, sizeof(header) + sizeof(edit) * header.Count}, plan);
    }
};
} // namespace
TEST_CASE("The live editor consumes generated descriptors with unchanged IDs", "[reflection][editor-binding]")
{
    Game game;
    uint8 bytes[1 + sizeof(PropertyDescriptor) * 2]{};
    usize written = 0;
    REQUIRE(game.Api.DescribeProperties(game.Instance, {bytes + 1, sizeof(bytes) - 1}, &written) == Status::Ok);
    PropertyDescriptor descriptor;
    std::memcpy(&descriptor, bytes + 1, sizeof(descriptor));
    CHECK(descriptor.PropertyId == 0xB001);
    CHECK(descriptor.MaxFloat == 8);
    CHECK(descriptor.Writable == 1);
    GameEditPlan* plan = nullptr;
    REQUIRE(game.Prepare(4, &plan) == Status::Ok);
    CHECK(game.Speed().Float == 1);
    REQUIRE(game.Api.CommitEdits(game.Instance, plan) == Status::Ok);
    CHECK(game.Speed().Float == 4);
    CHECK(game.Speed().ObjectRevision == 2);
    plan = nullptr;
    REQUIRE(game.Prepare(5, &plan) == Status::Ok);
    game.Api.DiscardEdits(game.Instance, plan);
    CHECK(game.Speed().Float == 4);
    CHECK(game.Prepare(std::numeric_limits<float32>::quiet_NaN(), &plan) == Status::OutOfRange);
    CHECK(game.Prepare(3, &plan, true) == Status::InvalidArgument);
    CHECK(game.Speed().Float == 4);
}
TEST_CASE("Prepared editor plans reject changed revisions and foreign instances", "[reflection][editor-binding]")
{
    Game game;
    Game other;
    GameEditPlan* stale = nullptr;
    GameEditPlan* newer = nullptr;
    REQUIRE(game.Prepare(4, &stale) == Status::Ok);
    REQUIRE(game.Prepare(6, &newer) == Status::Ok);
    CHECK(game.Api.CommitEdits(other.Instance, stale) == Status::InvalidArgument);
    REQUIRE(game.Api.CommitEdits(game.Instance, newer) == Status::Ok);
    CHECK(game.Api.CommitEdits(game.Instance, stale) == Status::StaleRevision);
    game.Api.DiscardEdits(game.Instance, stale);
    CHECK(game.Speed().Float == 6);
}

TEST_CASE("Generated constraints also validate the owner's existing checkpoint codec", "[reflection][reload]")
{
    Game game;
    GameEditPlan* plan = nullptr;
    REQUIRE(game.Prepare(4, &plan) == Status::Ok);
    REQUIRE(game.Api.CommitEdits(game.Instance, plan) == Status::Ok);
    REQUIRE(game.Api.Quiesce(game.Instance) == Status::Ok);
    uint8 bytes[4096]{};
    usize written = 0;
    CheckpointHeader header;
    REQUIRE(game.Api.WriteCheckpoint(game.Instance, &header, {bytes, sizeof(bytes)}, &written) == Status::Ok);
    CreateInfo info;
    info.GameId = 1;
    GameCandidate* candidate = nullptr;
    REQUIRE(game.Api.CreateCandidate(&info, &header, {bytes, written}, &candidate) == Status::Ok);
    REQUIRE(game.Api.ValidateCandidate(candidate) == Status::Ok);
    GameInstance* restored = nullptr;
    REQUIRE(game.Api.CommitCandidate(candidate, &restored) == Status::Ok);
    game.Api.Destroy(game.Instance);
    game.Instance = restored;
    CHECK(game.Speed().Float == 4);
    CHECK(game.Speed().ObjectRevision == 2);
}
