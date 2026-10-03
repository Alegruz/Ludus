#include <catch2/catch_test_macros.hpp>
#include <limits>
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
        registry.mSlots[slot].Generation = std::numeric_limits<uint32>::max();
        return {registry.mWorld, slot, std::numeric_limits<uint32>::max()};
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
