#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <ludus/foundation/identity/handle.hpp>
#include <ludus/foundation/identity/lifetime.hpp>
#include <ludus/gameplay/world/component_pool.hpp>
#include <ludus/gameplay/world/entity_registry.h>
#include <utility>
using namespace ludus::gameplay::world;
using namespace ludus::foundation;
namespace ludus::gameplay::world::internal
{
struct RegistryTestAccess final
{
    static EntityId RetireNext(EntityRegistry& registry) noexcept
    {
        const auto slot = registry.mFree.GetLast();
        registry.mSlots[slot].Generation = identity::TERMINAL_GENERATION;
        return {registry.mWorld, slot, identity::TERMINAL_GENERATION};
    }
    static void ExhaustSequence(EntityRegistry& registry) noexcept
    {
        registry.mNextSequence = std::numeric_limits<uint64>::max();
    }
};
} // namespace ludus::gameplay::world::internal
TEST_CASE("Registry rejects recycled and foreign handles, including after a move", "[world]")
{
    EntityRegistry first;
    EntityRegistry second;
    REQUIRE(first.TryInitialize(2) == Status::Success);
    REQUIRE(second.TryInitialize(2) == Status::Success);
    EntityId old;
    REQUIRE(first.TryCreate(old) == Status::Success);
    REQUIRE_FALSE(second.IsAlive(old));
    REQUIRE(first.GetCreationSequence(old) == 1);
    REQUIRE(first.MarkPendingDestroy(old) == Status::Success);
    REQUIRE(first.IsAlive(old));
    REQUIRE_FALSE(first.IsActive(old));
    REQUIRE(first.TryRelease(old) == Status::Success);
    EntityId replacement;
    REQUIRE(first.TryCreate(replacement) == Status::Success);
    REQUIRE(replacement.Slot == old.Slot);
    REQUIRE(replacement.Generation != old.Generation);
    REQUIRE_FALSE(first.IsAlive(old));
    EntityRegistry moved(std::move(first));
    REQUIRE(moved.IsAlive(replacement));
    // EntityRegistry explicitly leaves a moved-from source without identity.
    // NOLINTNEXTLINE(bugprone-use-after-move): verify the documented source state.
    REQUIRE_FALSE(first.IsAlive(replacement));
}
TEST_CASE("A moved-from registry can initialize a fresh structural world", "[world]")
{
    EntityRegistry source;
    REQUIRE(source.TryInitialize(2) == Status::Success);
    EntityId previous;
    REQUIRE(source.TryCreate(previous) == Status::Success);
    source.SetStructuralPhase(false);
    EntityRegistry destination(std::move(source));
    // NOLINTNEXTLINE(bugprone-use-after-move): initialization reuses the empty source.
    REQUIRE(source.TryInitialize(2) == Status::Success);
    EntityId fresh;
    REQUIRE(source.TryCreate(fresh) == Status::Success);
    REQUIRE(source.GetCreationSequence(fresh) == 1);
    REQUIRE(source.GetStructuralRevision() == 1);
    REQUIRE_FALSE(source.IsAlive(previous));
    REQUIRE(destination.IsAlive(previous));
}
TEST_CASE("Generation retirement and sequence exhaustion never reuse identity", "[world]")
{
    EntityRegistry registry;
    REQUIRE(registry.TryInitialize(1) == Status::Success);
    const auto retired = internal::RegistryTestAccess::RetireNext(registry);
    EntityId entity;
    REQUIRE(registry.TryCreate(entity) == Status::Success);
    REQUIRE(entity == retired);
    REQUIRE(registry.TryRelease(entity) == Status::Success);
    REQUIRE(registry.TryCreate(entity) == Status::CapacityExceeded);
    REQUIRE(entity == retired);
    REQUIRE_FALSE(registry.IsAlive(retired));
    EntityRegistry exhausted;
    REQUIRE(exhausted.TryInitialize(1) == Status::Success);
    internal::RegistryTestAccess::ExhaustSequence(exhausted);
    REQUIRE(exhausted.TryCreate(entity) == Status::IdentityExhausted);
    REQUIRE(entity == retired);
}
TEST_CASE("Registries take world tokens from the shared process lifetime source", "[world]")
{
    uint64 before = 0;
    REQUIRE(identity::TryIssueLifetimeId(before) == identity::LifetimeIdStatus::Success);
    EntityRegistry registry;
    REQUIRE(registry.TryInitialize(1) == Status::Success);
    uint64 after = 0;
    REQUIRE(identity::TryIssueLifetimeId(after) == identity::LifetimeIdStatus::Success);
    // Nothing else issues tokens in this process, so the steps are exact. This also
    // proves the registry and this test link one copy of the counter.
    REQUIRE(registry.GetWorld() == before + 1);
    REQUIRE(after == registry.GetWorld() + 1);

    // Rejected initialization neither consumes a token nor replaces the current one.
    EntityRegistry rejected;
    REQUIRE(rejected.TryInitialize(0) == Status::InvalidConfiguration);
    REQUIRE(rejected.GetWorld() == 0);
    const uint64 world = registry.GetWorld();
    REQUIRE(registry.TryInitialize(1) == Status::InvalidConfiguration);
    REQUIRE(registry.GetWorld() == world);
    uint64 next = 0;
    REQUIRE(identity::TryIssueLifetimeId(next) == identity::LifetimeIdStatus::Success);
    REQUIRE(next == after + 1);
}
TEST_CASE("Handles from an old world are rejected after the world is rebuilt", "[world]")
{
    EntityRegistry registry;
    REQUIRE(registry.TryInitialize(2) == Status::Success);
    ComponentPool<uint32> oldPool;
    REQUIRE(oldPool.TryInitialize(registry) == Status::Success);
    EntityId old;
    REQUIRE(registry.TryCreate(old) == Status::Success);
    REQUIRE(oldPool.TryInsert(registry, old, 7) == Status::Success);
    const uint64 oldWorld = registry.GetWorld();

    // Clearing and rebuilding takes a fresh lifetime; it never resets generations
    // under the old world token.
    registry = EntityRegistry{};
    REQUIRE(registry.TryInitialize(2) == Status::Success);
    ComponentPool<uint32> pool;
    REQUIRE(pool.TryInitialize(registry) == Status::Success);
    EntityId fresh;
    REQUIRE(registry.TryCreate(fresh) == Status::Success);
    REQUIRE(pool.TryInsert(registry, fresh, 11) == Status::Success);

    // Same slot, generation and creation sequence: only the world token differs.
    REQUIRE(fresh.Slot == old.Slot);
    REQUIRE(fresh.Generation == old.Generation);
    REQUIRE(registry.GetCreationSequence(fresh) == 1);
    REQUIRE(fresh.World != oldWorld);
    REQUIRE(fresh.World == registry.GetWorld());

    REQUIRE_FALSE(registry.IsAlive(old));
    REQUIRE_FALSE(registry.IsActive(old));
    REQUIRE(registry.GetCreationSequence(old) == 0);
    REQUIRE(registry.MarkPendingDestroy(old) == Status::InvalidEntity);
    REQUIRE(registry.TryRelease(old) == Status::InvalidEntity);
    REQUIRE(pool.Find(old) == nullptr);
    REQUIRE(pool.TryInsert(registry, old, 9) == Status::InvalidEntity);
    REQUIRE(pool.TryRemove(registry, old) == Status::InvalidEntity);

    // A pool bound to the old world rejects the new world and keeps its old value.
    REQUIRE(oldPool.Find(fresh) == nullptr);
    REQUIRE(oldPool.TryInsert(registry, fresh, 3) == Status::InvalidEntity);
    REQUIRE(oldPool.TryRemove(registry, fresh) == Status::InvalidEntity);

    // None of the rejected calls disturbed the live incarnation in the same slot.
    REQUIRE(registry.IsActive(fresh));
    REQUIRE(*pool.Find(fresh) == 11);
    REQUIRE(registry.GetStructuralRevision() == 1);
}
TEST_CASE("Sparse pool swap removal preserves exact owners and enforces phases", "[world]")
{
    EntityRegistry registry;
    REQUIRE(registry.TryInitialize(3) == Status::Success);
    ComponentPool<uint32> pool;
    REQUIRE(pool.TryInitialize(registry) == Status::Success);
    EntityId entities[3];
    for (usize index = 0; index < 3; ++index)
    {
        REQUIRE(registry.TryCreate(entities[index]) == Status::Success);
        REQUIRE(pool.TryInsert(registry, entities[index], static_cast<uint32>(index + 10)) == Status::Success);
    }
    REQUIRE(pool.TryInsert(registry, entities[1], 99) == Status::DuplicateComponent);
    REQUIRE(pool.TryRemove(registry, entities[1]) == Status::Success);
    REQUIRE(*pool.Find(entities[2]) == 12);
    REQUIRE(pool.GetOwners()[1] == entities[2]);
    REQUIRE(pool.Find(entities[1]) == nullptr);
    REQUIRE(registry.TryRelease(entities[1]) == Status::Success);
    EntityId fresh;
    REQUIRE(registry.TryCreate(fresh) == Status::Success);
    REQUIRE(pool.Find(fresh) == nullptr);
    REQUIRE(pool.TryInsert(registry, fresh, 22) == Status::Success);
    REQUIRE(pool.Find(entities[1]) == nullptr);
    registry.SetStructuralPhase(false);
    REQUIRE(pool.TryRemove(registry, fresh) == Status::InvalidPhase);
    REQUIRE(registry.TryCreate(fresh) == Status::InvalidPhase);
    REQUIRE(pool.GetOwners().size() == pool.GetValues().size());
}

TEST_CASE("Pool moves transfer ownership and reject insertion into the empty source", "[world]")
{
    EntityRegistry registry;
    REQUIRE(registry.TryInitialize(2) == Status::Success);
    ComponentPool<uint32> source;
    REQUIRE(source.TryInitialize(registry) == Status::Success);
    EntityId entity;
    REQUIRE(registry.TryCreate(entity) == Status::Success);
    REQUIRE(source.TryInsert(registry, entity, 4) == Status::Success);
    ComponentPool<uint32> moved(std::move(source));
    REQUIRE(*moved.Find(entity) == 4);
    // NOLINTNEXTLINE(bugprone-use-after-move): moved-from pools reject mutations.
    REQUIRE(source.TryInsert(registry, entity, 5) == Status::InvalidEntity);
}
