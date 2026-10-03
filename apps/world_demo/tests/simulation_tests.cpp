#include "internal/world.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <string>
using namespace ludus::world_demo;
namespace
{
GameWorld Build(uint32 capacity = 48)
{
    Level level;
    REQUIRE(ReadLevel(ExampleLevel(), level).Error == LevelError::None);
    GameWorld world;
    REQUIRE(world.Prepare(level, capacity) == Status::Success);
    while (!world.IsBuilt())
    {
        REQUIRE(world.BuildNext() == Status::Success);
    }
    return world;
}
void Load(Session& session)
{
    REQUIRE(session.RequestLoad(ExampleLevel()).Error == LevelError::None);
    while (session.GetLoadStage() != LoadStage::Ready)
    {
        REQUIRE(session.PollLoad() == Status::Success);
    }
    REQUIRE(session.Activate());
}
Recipe ExtraGuard()
{
    Level level;
    REQUIRE(ReadLevel(ExampleLevel(), level).Error == LevelError::None);
    Recipe recipe = level.Entities[1];
    recipe.Id = { .Text = "extra-guard" };
    return recipe;
}
} // namespace
TEST_CASE("Level codec round-trips canonically and owns forward references", "[world-demo]")
{
    Level level;
    REQUIRE(ReadLevel(ExampleLevel(), level).Error == LevelError::None);
    ludus::foundation::core::Array<char> json;
    REQUIRE(WriteLevel(level, json));
    REQUIRE(std::string_view(json.GetData(), json.GetSize()).find("\n  \"version\": 1") != std::string_view::npos);
    Level copy;
    REQUIRE(ReadLevel({json.GetData(), json.GetSize()}, copy).Error == LevelError::None);
    ludus::foundation::core::Array<char> second;
    REQUIRE(WriteLevel(copy, second));
    REQUIRE(std::string_view(json.GetData(), json.GetSize()) == std::string_view(second.GetData(), second.GetSize()));
    auto world = Build();
    REQUIRE(world.Enemies().GetValues()[0].Target == world.FindAuthored("player-start"));
    REQUIRE(world.Registry().GetCreationSequence(world.FindAuthored("exit-a")) == 1);
}
TEST_CASE("Malformed content leaves the supplied definition unchanged", "[world-demo]")
{
    Level level;
    REQUIRE(ReadLevel(ExampleLevel(), level).Error == LevelError::None);
    const auto seed = level.Seed;
    for (auto source : {"{}", "{\"version\":2}", "{\"version\":1,\"version\":1}", "{\"unknown\":0}"})
    {
        REQUIRE(ReadLevel(source, level).Error != LevelError::None);
        REQUIRE(level.Seed == seed);
    }
    std::string invalid(ExampleLevel());
    invalid.replace(invalid.find("player-start", invalid.find("target")), 12, "missing");
    REQUIRE(ReadLevel(invalid, level).Error == LevelError::InvalidReference);
    invalid = ExampleLevel();
    invalid.replace(invalid.find("\"speed\": 4"), 10, "\"speed\":1e99");
    REQUIRE(ReadLevel(invalid, level).Error != LevelError::None);
    level.Entities[0].Position.X = std::numeric_limits<float32>::quiet_NaN();
    REQUIRE(ValidateLevel(level) != LevelError::None);
}
TEST_CASE("Candidate cancellation, failure, supersession and restart preserve ownership", "[world-demo]")
{
    Session session;
    Load(session);
    const auto old = session.World().FindAuthored("player-start");
    const auto hash = session.World().GetReplayHash();
    REQUIRE(session.RequestLoad("{}").Error != LevelError::None);
    REQUIRE(session.World().Registry().IsAlive(old));
    REQUIRE(session.World().GetReplayHash() == hash);
    REQUIRE(session.RequestLoad(ExampleLevel()).Error == LevelError::None);
    REQUIRE(session.PollLoad() == Status::Success);
    const auto transition = session.GetTransition();
    session.CancelLoad();
    REQUIRE_FALSE(session.Activate());
    REQUIRE(session.World().Registry().IsAlive(old));
    Load(session);
    REQUIRE(session.GetTransition() > transition);
    REQUIRE_FALSE(session.World().Registry().IsAlive(old));
    REQUIRE(session.World().GetReplayHash() == hash);
}
TEST_CASE("Input edges survive zero ticks, catch-up consumes once, and suspension gates held controls", "[world-demo]")
{
    InputBridge input;
    input.Sample({ .Held = true, .Pressed = true });
    input.Sample({ .Released = true });
    const auto tap = input.Consume();
    REQUIRE(tap.Pressed);
    REQUIRE(tap.Released);
    REQUIRE_FALSE(tap.Held);
    REQUIRE_FALSE(input.Consume().Pressed);
    input.Sample({ .Axis = {1, 0}, .Held = true });
    input.Cancel();
    input.Sample({ .Axis = {1, 0}, .Held = true, .Pressed = true });
    REQUIRE(input.Consume().Cancelled);
    REQUIRE_FALSE(input.Consume().Pressed);
    input.Sample({});
    input.Sample({ .Pressed = true });
    REQUIRE(input.Consume().Pressed);
    auto world = Build();
    TickDriver driver;
    REQUIRE(driver.Advance(world, 0, { .Pressed = true }) == Status::Success);
    REQUIRE(world.GetTick() == 0);
    REQUIRE(driver.Advance(world, 0.1, {}) == Status::Success);
    REQUIRE(world.GetTick() == 4);
    REQUIRE(driver.GetDiscardedTime() > 0.03);
    REQUIRE(driver.GetAlpha() < 1);
    driver.SetMode(Mode::Paused);
    REQUIRE(driver.Advance(world, 8, {}) == Status::Success);
    REQUIRE(world.GetTick() == 4);
    REQUIRE(driver.Step(world) == Status::Success);
    REQUIRE(world.GetTick() == 5);
    driver.SetMode(Mode::Playing);
    REQUIRE(driver.Advance(world, 8, {}, false) == Status::Success);
    REQUIRE(world.GetTick() == 5);
}
TEST_CASE("Deferred spawn and destruction publish complete bundles and stale handles fail", "[world-demo]")
{
    auto world = Build();
    const auto old = world.FindAuthored("guard-a");
    REQUIRE(world.QueueDestroy(old) == Status::Success);
    REQUIRE(world.QueueDestroy(old) == Status::Success);
    REQUIRE(world.Registry().IsAlive(old));
    REQUIRE_FALSE(world.Registry().IsActive(old));
    uint64 request = 0;
    REQUIRE(world.QueueSpawn(ExtraGuard(), request) == Status::Success);
    REQUIRE(world.RunTick({}) == Status::Success);
    REQUIRE_FALSE(world.Registry().IsAlive(old));
    REQUIRE(world.Transforms().Find(old) == nullptr);
    REQUIRE(world.GetSpawnOutcomes().size() == 1);
    const auto spawned = world.GetSpawnOutcomes()[0].Entity;
    REQUIRE(world.Registry().IsActive(spawned));
    REQUIRE(world.HealthValues().Find(spawned)->Current == 2);
    REQUIRE(world.Transforms().Find(spawned)->Previous.X == world.Transforms().Find(spawned)->Current.X);
    REQUIRE(world.GetOutbox().size() == 1);
    REQUIRE(world.GetOutbox()[0].Kind == EventKind::Spawn);
    REQUIRE(world.RunTick({}) == Status::Success);
    REQUIRE(world.GetOutbox().size() == 1);
    world.ConsumeOutbox();
    REQUIRE(world.GetOutbox().empty());
}
TEST_CASE("Commit preflight faults without publishing spawns or presentation", "[world-demo]")
{
    auto world = Build(3);
    RenderFrame frame;
    REQUIRE(world.Extract(1, frame));
    const auto old = world.FindAuthored("guard-a");
    REQUIRE(world.QueueDestroy(old) == Status::Success);
    uint64 request = 0;
    REQUIRE(world.QueueSpawn(ExtraGuard(), request) == Status::Success);
    REQUIRE(world.RunTick({}) == Status::CapacityExceeded);
    REQUIRE(world.GetTick() == 0);
    REQUIRE(world.GetPhase() == Phase::Faulted);
    REQUIRE(world.GetTraceRing().back().LastPhase == Phase::Commit);
    REQUIRE(world.GetTraceRing().back().Result == Status::CapacityExceeded);
    REQUIRE(world.Registry().IsAlive(old));
    REQUIRE(world.GetOutbox().empty());
    REQUIRE_FALSE(world.Extract(1, frame));
    REQUIRE(frame.Count == 4);
}
TEST_CASE("Gameplay command overflow retains failure context without publishing", "[world-demo]")
{
    Level level;
    REQUIRE(ReadLevel(ExampleLevel(), level).Error == LevelError::None);
    level.Entities[1].Position = {-5.5F, 0};
    level.Entities[1].Health = 1;
    GameWorld world;
    REQUIRE(world.Prepare(level) == Status::Success);
    while (!world.IsBuilt())
    {
        REQUIRE(world.BuildNext() == Status::Success);
    }
    uint64 request = 0;
    for (usize index = 0; index < 32; ++index)
    {
        REQUIRE(world.QueueSpawn(ExtraGuard(), request) == Status::Success);
    }
    REQUIRE(world.RunTick({ .Pressed = true }) == Status::CapacityExceeded);
    REQUIRE(world.GetPhase() == Phase::Faulted);
    REQUIRE(world.GetTraceRing()[0].LastPhase == Phase::Gameplay);
    REQUIRE(world.GetTraceRing()[0].Commands == 32);
    REQUIRE(world.GetTraceRing()[0].Result == Status::CapacityExceeded);
    REQUIRE(world.GetOutbox().empty());
    REQUIRE(world.Transforms().GetOwners().size() == 3);
}

TEST_CASE("Owned completions reject old world, request and revision values", "[world-demo]")
{
    auto world = Build();
    const auto player = world.FindAuthored("player-start");
    Completion old;
    REQUIRE(world.BeginRequest(player, old) == Status::Success);
    Completion foreign;
    REQUIRE(world.BeginRequest(player, foreign) == Status::Success);
    ++foreign.Entity.World;
    Completion current;
    REQUIRE(world.BeginRequest(player, current) == Status::Success);
    REQUIRE(world.SubmitCompletion(old) == Status::Success);
    REQUIRE(world.SubmitCompletion(foreign) == Status::InvalidEntity);
    --foreign.Entity.World;
    foreign.State = CompletionState::Cancelled;
    REQUIRE(world.SubmitCompletion(foreign) == Status::Success);
    REQUIRE(world.RunTick({}) == Status::Success);
    REQUIRE(world.GetStaleCompletions() == 3);
    current.Velocity = {6, 0};
    REQUIRE(world.SubmitCompletion(current) == Status::Success);
    const auto before = world.Transforms().Find(player)->Current.X;
    REQUIRE(world.RunTick({}) == Status::Success);
    REQUIRE(world.Transforms().Find(player)->Current.X > before);
    REQUIRE(world.SubmitCompletion(current) == Status::InvalidConfiguration);
    Completion leases[16];
    for (auto& ticket : leases)
    {
        REQUIRE(world.BeginRequest(player, ticket) == Status::Success);
    }
    Completion rejected;
    REQUIRE(world.BeginRequest(player, rejected) == Status::CapacityExceeded);
    for (auto& ticket : leases)
    {
        ticket.State = CompletionState::Cancelled;
        REQUIRE(world.SubmitCompletion(ticket) == Status::Success);
    }
    REQUIRE(world.RunTick({}) == Status::Success);
    REQUIRE(world.BeginRequest(player, rejected) == Status::Success);
}
TEST_CASE("Same-build replay hashes exclude world identity and rendering", "[world-demo]")
{
    auto first = Build();
    auto second = Build();
    REQUIRE(first.Registry().GetWorld() != second.Registry().GetWorld());
    for (usize tick = 0; tick < 120; ++tick)
    {
        const TickInput input{ .Axis = {tick < 60 ? 1.0F : -1.0F, 0}, .Pressed = tick % 30 == 0 };
        REQUIRE(first.RunTick(input) == Status::Success);
        REQUIRE(second.RunTick(input) == Status::Success);
        REQUIRE(first.GetReplayHash() == second.GetReplayHash());
        RenderFrame frame;
        REQUIRE(first.Extract(0.5F, frame));
        REQUIRE(first.GetReplayHash() == second.GetReplayHash());
    }
}

TEST_CASE("Replay boundaries include runtime colliders and outstanding tickets", "[world-demo]")
{
    auto first = Build();
    auto second = Build();
    auto recipe = ExtraGuard();
    uint64 request = 0;
    REQUIRE(first.QueueSpawn(recipe, request) == Status::Success);
    REQUIRE(first.GetReplayHash() == 0);
    recipe.HalfExtent.X *= 2;
    REQUIRE(second.QueueSpawn(recipe, request) == Status::Success);
    REQUIRE(first.RunTick({}) == Status::Success);
    REQUIRE(second.RunTick({}) == Status::Success);
    REQUIRE(first.GetReplayHash() != second.GetReplayHash());
    auto third = Build();
    auto fourth = Build();
    Completion ticket;
    REQUIRE(third.BeginRequest(third.FindAuthored("player-start"), ticket) == Status::Success);
    REQUIRE(fourth.BeginRequest(fourth.FindAuthored("player-start"), ticket) == Status::Success);
    REQUIRE(third.GetReplayHash() == fourth.GetReplayHash());
    REQUIRE(fourth.SubmitCompletion(ticket) == Status::Success);
    REQUIRE(fourth.GetReplayHash() == 0);
}

TEST_CASE("Compiled definitions run the same gameplay with enter/stay/exit triggers", "[world-demo]")
{
    Level level
    {
        .Id = { .Text = "first-room" },
        .Seed = 42,
        .Minimum = {-12, -7},
        .Maximum = {12, 7},
        .Camera = {},
        .VerticalExtent = 14,
        .Entities =
            {
                {
                    .Id = { .Text = "player-start" },
                    .Type = Kind::Player,
                    .Position = {0, 0},
                    .Speed = 4,
                    .Health = 3,
                    .Sprite = { .Text = "player" },
                },
                {
                    .Id = { .Text = "guard-a" },
                    .Type = Kind::Enemy,
                    .Position = {0.8F, 0},
                    .Speed = 2,
                    .Health = 1,
                    .Sprite = { .Text = "guard" },
                    .Target = { .Text = "player-start" },
                },
                {
                    .Id = { .Text = "exit-a" },
                    .Type = Kind::Exit,
                    .Position = {0, 0},
                    .HalfExtent = {.5F, 1},
                    .NextLevel = { .Text = "second-room" },
                },
            },
        .EntityCount = 3,
    };
    GameWorld world;
    REQUIRE(world.Prepare(level) == Status::Success);
    while (!world.IsBuilt())
    {
        REQUIRE(world.BuildNext() == Status::Success);
    }
    const auto guard = world.FindAuthored("guard-a");
    const auto player = world.FindAuthored("player-start");
    REQUIRE(world.RunTick({ .Pressed = true }) == Status::Success);
    REQUIRE_FALSE(world.Registry().IsAlive(guard));
    REQUIRE(world.HealthValues().Find(player)->Current == 3);
    REQUIRE(world.GetContacts().size() == 1);
    REQUIRE(world.GetContacts()[0].Kind == ContactPhase::Enter);
    REQUIRE(world.GetNextLevel().View() == "second-room");
    REQUIRE(world.GetOutbox().size() == 3);
    world.ConsumeOutbox();
    REQUIRE(world.RunTick({}) == Status::Success);
    REQUIRE(world.GetContacts()[0].Kind == ContactPhase::Stay);
    REQUIRE(world.GetOutbox().empty());
    for (usize tick = 0; tick < 20; ++tick)
    {
        REQUIRE(world.RunTick({ .Axis = {1, 0} }) == Status::Success);
    }
    REQUIRE(world.Transforms().Find(player)->Current.X > 1);
    REQUIRE(world.GetOutbox().empty());
}
TEST_CASE("Swept static collision stops crossing a thin floor", "[world-demo]")
{
    auto world = Build();
    const auto player = world.FindAuthored("player-start");
    for (usize tick = 0; tick < 100; ++tick)
    {
        REQUIRE(world.RunTick({ .Axis = {0, -1} }) == Status::Success);
    }
    REQUIRE(world.Transforms().Find(player)->Current.Y == Catch::Approx(-3.1F));
}
