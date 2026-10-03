# Gameplay world storage

`Ludus::GameplayWorld` supplies identity and typed sparse pools for a game-owned
world. It depends on FoundationBase and FoundationContainers and is exported in
the installed SDK. Gameplay, input, serialization, scheduling and rendering remain
in the consuming game.

Initialize an `EntityRegistry` with a fixed entity capacity, then initialize each
`ComponentPool<T>` against it during construction. Both operations return explicit
allocation/configuration errors. Pool components must be trivially copyable and
trivially destructible plain state. Initialization reserves dense values, dense
owners and the sparse slot mapping for the whole registry limit. Insertion and
removal subsequently allocate nothing.

`TryCreate` changes its output only on success. `EntityId` contains a nonzero world
identity, slot and generation. `IsAlive` validates all three; `IsActive` also excludes
pending destruction. Release advances the generation and retires an exhausted
slot. World identities and creation sequences fail on exhaustion. Moves transfer
registry ownership and leave the source registry without a world identity.

The game's construction/commit facade owns mutable registry and pool access.
Systems receive a const registry, component values and command recording APIs.
`SetStructuralPhase(false)` prevents registry creation/release and pool membership
changes. These checks remain enabled in release builds. Remove an entity from
**every pool before releasing its registry slot**. Registry release cannot discover
a game's component roster.

`Find` validates pool ownership and the complete dense owner. Its pointer and the
matching owner/value spans are borrowed until structural mutation, pool move or
world replacement. Always check `IsActive` before updating an entity. Pending
entities deliberately retain their values until commit. Persist handles across
phases and asynchronous work. Dense order changes after swap removal; use creation
sequence or authored identity when gameplay priority depends on order.

See [the architecture](../../../docs/architecture/game-world.md) and
[the reference game](../../../apps/world_demo/README.md) for composition,
transactional candidate loading and phased tick examples.
