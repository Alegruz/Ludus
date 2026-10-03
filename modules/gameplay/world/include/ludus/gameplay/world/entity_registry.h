#pragma once
#include <ludus/foundation/base/core.h>
#include <ludus/foundation/containers/array.hpp>
#include <ludus/gameplay/world/entity_id.h>
namespace ludus::gameplay::world
{
namespace internal
{
struct RegistryTestAccess;
}
// The game owns the structural facade. Systems receive only const registry
// access, component values and command recording. All borrows end at commit.
class EntityRegistry final
{
public:
    EntityRegistry() noexcept = default;
    EntityRegistry(const EntityRegistry&) = delete;
    EntityRegistry& operator=(const EntityRegistry&) = delete;
    EntityRegistry(EntityRegistry&& other) noexcept;
    EntityRegistry& operator=(EntityRegistry&& other) noexcept;
    // Initialization is transactional; initialized registries cannot be reset.
    [[nodiscard]] Status TryInitialize(foundation::uint32 capacity) noexcept;
    [[nodiscard]] Status TryCreate(EntityId& output) noexcept;
    [[nodiscard]] Status CheckStructuralBudget(foundation::usize creates, foundation::usize releases) const noexcept;
    [[nodiscard]] Status TryRelease(EntityId entity) noexcept;
    [[nodiscard]] Status MarkPendingDestroy(EntityId entity) noexcept;
    [[nodiscard]] bool IsAlive(EntityId entity) const noexcept;
    [[nodiscard]] bool IsActive(EntityId entity) const noexcept;
    [[nodiscard]] foundation::uint64 GetCreationSequence(EntityId entity) const noexcept;
    [[nodiscard]] foundation::uint64 GetWorld() const noexcept
    {
        return mWorld;
    }
    [[nodiscard]] foundation::uint32 GetCapacity() const noexcept
    {
        return static_cast<foundation::uint32>(mSlots.GetSize());
    }
    [[nodiscard]] foundation::usize GetAvailable() const noexcept
    {
        return mFree.GetSize();
    }
    [[nodiscard]] foundation::uint64 GetStructuralRevision() const noexcept
    {
        return mRevision;
    }
    [[nodiscard]] bool CanMutateStructure() const noexcept
    {
        return mStructural && mWorld != 0;
    }
    void SetStructuralPhase(bool structural) noexcept
    {
        mStructural = structural;
    }

private:
    struct Slot final
    {
        foundation::uint64 Sequence = 0;
        foundation::uint32 Generation = 1;
        bool Live = false;
        bool Pending = false;
    };
    foundation::core::Array<Slot> mSlots;
    foundation::core::Array<foundation::uint32> mFree;
    foundation::uint64 mWorld = 0;
    foundation::uint64 mNextSequence = 1;
    foundation::uint64 mRevision = 0;
    bool mStructural = true;
    friend struct internal::RegistryTestAccess;
};
} // namespace ludus::gameplay::world
