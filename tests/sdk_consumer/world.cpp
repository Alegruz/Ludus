#include <ludus/gameplay/world/component_pool.hpp>
#include <ludus/gameplay/world/entity_registry.h>
int ExerciseInstalledWorld() noexcept
{
    using namespace ludus::gameplay::world;
    EntityRegistry registry;
    ComponentPool<ludus::foundation::uint32> health;
    if (registry.TryInitialize(4) != Status::Success || health.TryInitialize(registry) != Status::Success)
    {
        return 7;
    }
    EntityId entity;
    if (registry.TryCreate(entity) != Status::Success || health.TryInsert(registry, entity, 3) != Status::Success)
    {
        return 7;
    }
    if (health.Find(entity) == nullptr || *health.Find(entity) != 3)
    {
        return 7;
    }
    const auto old = entity;
    if (health.TryRemove(registry, entity) != Status::Success || registry.TryRelease(entity) != Status::Success ||
        registry.TryCreate(entity) != Status::Success)
    {
        return 7;
    }
    return registry.IsAlive(old) || health.Find(old) != nullptr ? 7 : 0;
}
