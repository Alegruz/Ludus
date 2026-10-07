#pragma once
#include <ludus/foundation/base/core.h>
#include <ludus/gameplay/world/entity_registry.h>

#include "contract.h"

namespace ludus::s1
{
// Game-owned domain adapter, independent of Luau/runtime headers and libraries.
enum class Result : uint8
{
    Accepted,
    Applied,
    AlreadyOpen,
    InvalidEntity,
    InvalidPhase,
    MissingCapability,
    CapacityExceeded,
    InvalidValue,
    ScriptFault,
    AllocationFailure,
    Interrupted,
    Halted
};
enum class Phase : uint8
{
    BeginTick,
    Intent,
    Gameplay,
    Commit,
    EndTick
};
enum class Capability : uint8
{
    None,
    DoorControl
};
struct CommandResult
{
    Result Code = Result::InvalidValue;
    uint32 Token = 0;
};
struct Command
{
    EntityRef Target;
    uint32 Power = 0;
    uint32 Token = 0;
    uint64 Instance = 0;
};
struct AppliedCommand
{
    Command Request;
    Result Code = Result::InvalidEntity;
};
struct EventRecord
{
    uint64 Tick = 1;
    Phase At = Phase::Gameplay;
    uint64 ProducerSequence = 0;
    uint32 InstanceOrder = 0;
    Interact Value;
};
struct World
{
    gameplay::world::EntityRegistry Entities;
    gameplay::world::EntityId Doors[2];
    bool Open[2] = {};
    State States[2];
    Config Configuration;
    Command Commands[4];
    AppliedCommand Outcomes[4];
    usize CommandCount = 0;
    usize OutcomeCount = 0;
    usize QueueLimit = 4;
    uint32 NextToken = 1;
    // Deliberately above 2^53: identities must never travel as Luau numbers.
    uint64 Session = 0xf123456789abcdefULL;
    uint64 Execution = 0xa123456789abcdefULL;
    uint64 Tick = 1;
    Phase At = Phase::BeginTick;
    bool Faulted = false;
};
struct Transaction
{
    World* Owner = nullptr;
    Config Configuration = {};
    State CandidateState = {};
    Interact Event = {};
    Command Pending[2] = {};
    usize PendingCount = 0;
    uint64 Instance = 0;
    uint32 InstanceOrder = 0;
    Capability Allowed = Capability::DoorControl;
};
using Handler = Result (*)(Transaction&, void*) noexcept;
[[nodiscard]] bool Initialize(World& world) noexcept;
[[nodiscard]] EntityRef Reference(const World& world, uint32 door) noexcept;
[[nodiscard]] bool IsActive(const World& world, EntityRef entity) noexcept;
[[nodiscard]] const char* ResultName(Result value) noexcept;
[[nodiscard]] Result Publish(Transaction& transaction) noexcept;
[[nodiscard]] Result
Dispatch(World& world, const EventRecord* events, usize count, Handler handler, void* user) noexcept;
[[nodiscard]] Result Apply(World& world) noexcept;
[[nodiscard]] Result NativeInteraction(Transaction& transaction, void* user) noexcept;
// Generated adapter callbacks are linked only by the Luau executable.
} // namespace ludus::s1
