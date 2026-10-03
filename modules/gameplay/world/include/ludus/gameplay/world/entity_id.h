#pragma once
#include <ludus/foundation/base/types.h>
namespace ludus::gameplay::world
{
struct EntityId final
{
    foundation::uint64 World = 0;
    foundation::uint32 Slot = 0;
    foundation::uint32 Generation = 0;
    [[nodiscard]] constexpr bool operator==(const EntityId&) const noexcept = default;
};
enum class Status : foundation::uint8
{
    Success,
    InvalidEntity,
    DuplicateComponent,
    MissingComponent,
    InvalidPhase,
    InvalidConfiguration,
    CapacityExceeded,
    AllocationFailure,
    IdentityExhausted
};
} // namespace ludus::gameplay::world
