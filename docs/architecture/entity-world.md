# Entities and component storage

Status: Baseline implemented. Part of the [game world architecture](game-world.md).
See [the reference guide](../../apps/world_demo/README.md) for the concrete API,
bounds and policies; conditional extensions in this document remain proposed.

An entity is identity. Its state lives in explicitly named component pools owned
by a game world. Systems are ordinary functions over that state. The first
implementation uses one small registry and typed sparse component pools, with
structural changes committed at known boundaries.

## Identity and validity

`EntityId` contains `World` (`uint64`), `Slot` (`uint32`), and
`Generation` (`uint32`). It is a trivially copyable value. World zero and generation
zero are reserved as invalid. Do not pack it or expose bit arithmetic before a
measurement justifies changing the representation.

Each constructed world receives a process-local, monotonically issued nonzero
identity. Restart and level replacement never reuse one. Within a world, the
registry owns slots with live state and a nonzero generation. Destruction advances
the generation before making a slot reusable. A slot whose generation would wrap
is permanently retired; world-identity exhaustion returns an error rather than
wrapping. New process runs may reuse numbers because runtime handles are never
serialized or accepted from outside that process.

`IsAlive` checks the world identity, slot range, live bit, and exact generation.
Release builds perform these checks for externally held handles too; assertions
alone cannot protect delayed results. A handle copied into another world's API
is rejected. Pools record their owning world identity and apply the same checks.

Authored IDs, debug names, entity handles, asset IDs, and network/save identities
are different types with different lifetimes. Only authored IDs are stored in
the level file. The [Flecs generation mechanism](https://www.flecs.dev/flecs/EntitiesComponents.html)
is evidence for detecting recycled identities; the world field and exhaustion
policy here are Ludus choices.

## Registry and component pools

The registry owns slot metadata and a reusable-slot list. It does not own GPU
objects, physics objects, or game state. Slot metadata also includes a creation
sequence issued monotonically within the world, and a pending-destroy bit.
Creation-sequence exhaustion returns an error rather than wrapping. The game owns
the component roster:

```text
GameWorld
    EntityRegistry
    ComponentPool<Transform2>
    ComponentPool<Motion2>
    ComponentPool<Collider2>
    ComponentPool<SpriteVisual>
    ComponentPool<PlayerState>
    ComponentPool<EnemyState>
    ComponentPool<ExitState>
    Random streams
    Tick commands and events
    Asset leases and authored identity table
```

This example is one game's aggregate. There is no type-erased component registry,
runtime reflection, or automatic discovery. The destruction function explicitly
removes an entity from every owned pool. Adding a pool requires adding that removal
and its invariant tests in the same change. Reviewable repetition is acceptable
for the initial small roster.

Each `ComponentPool<T>` stores three arrays:

| Storage | Purpose |
| --- | --- |
| Dense owners | Full entity IDs corresponding to values. |
| Dense values | Contiguous `T` values for iteration. |
| Sparse indices | Registry slot to dense index, with an explicit missing sentinel. |

Lookup checks pool/world identity and slot bounds, then the sparse index, dense
bounds, and exact dense owner. A stale generation cannot return another entity's
component. Only a live registry entity may receive a component. Each entity has
at most one component of a given type.

Initialization reserves storage for the full registry capacity with fallible
operations. Insertion then publishes the owner, value, and sparse entry together. A failed insertion leaves
membership and values unchanged, without growing storage.
Components in the initial pool facility must be trivially copyable and trivially
destructible plain state, without hidden allocation or external resource creation. Component
construction must not invoke gameplay or arbitrary user callbacks.

Removal swaps the last dense value/owner into the hole, updates the moved owner's
sparse entry, clears the removed slot's sparse entry, and shrinks the dense arrays.
Moving a component changes its address and iteration position, not its entity ID.
Sparse-array memory covers the configured registry capacity for each pool;
record that cost. This is a chosen simplicity tradeoff, not an unmeasured claim of
the best possible storage density.

Systems iterate one relevant pool and look up additional components explicitly.
For example, motion iterates `Motion2` owners and accesses their `Transform2`.
Recipe validation ensures required bundles exist. Missing optional components
are ordinary branches. Do not introduce a query DSL, dynamic dispatch, or a
multicomponent cache before profiling establishes a useful need.

## Initial API vocabulary

The identity/storage operations below are implemented by `Ludus::GameplayWorld`.
The reference application supplies the game-owned command operations. Other games
may adjust those signatures while preserving the visibility contracts.

| Operation | Contract |
| --- | --- |
| `EntityRegistry::TryCreate` | Create one live identity during construction/commit. On failure leave the output handle unchanged. |
| `EntityRegistry::IsAlive` | Validate world, slot, live state, and generation without allocation. |
| `EntityRegistry::IsActive` | Require a live identity whose pending-destroy bit is clear. |
| `ComponentPool<T>::Find` | Return a phase-local pointer or null; provide mutable and const overloads. |
| `ComponentPool<T>::TryInsert` | Require a live identity from the owning registry and no existing component; publish a complete value or report failure. |
| `ComponentPool<T>::TryRemove` | Remove an existing component only during construction/commit; invalid identity and absent component have distinct statuses. |
| `GameWorld::QueueSpawn` | Own a complete recipe payload and return a request ID or recording failure. |
| `GameWorld::QueueDestroy` | Validate queue space, record once, then mark pending destruction. |
| `GameWorld::Commit` | Preflight the batch and publish its structural changes with reserved storage. |

Fallible mutating operations return `[[nodiscard]]` statuses and are `noexcept`.
The initial status vocabulary includes success, invalid entity, duplicate/missing
component, invalid phase, capacity exceeded, and allocation failure. Borrowed
iteration exposes read-only owner spans and matching value spans within the phase;
value mutability follows the system's write contract. The registry's
slot release and pool structural primitives remain inaccessible to ordinary
gameplay systems; a world construction/commit facade supplies that access.

## Borrowing and ordering

Component pointers and spans are borrowed within one phase. They must not survive
a commit, pool growth, destruction, world replacement, asynchronous dispatch, or
an API call that can perform structural mutation. Persist handles instead. Debug
inspection can read the registry's structural revision. Structural APIs return
`InvalidPhase` in every build when mutation is disabled. An optional view wrapper can capture that revision; raw pointers
cannot be made safe after escaping their documented lifetime.

Dense iteration order is an implementation detail. Independent per-entity updates
may use it. Rules whose outcome depends on ordering must state a policy: collect
intent first, reduce explicitly, and use a stable key such as authored identity
or a documented creation sequence for ties. Do not use addresses, hash iteration,
or worker arrival order as gameplay priority.

A `Transform2` initially stores world position, rotation, and scale. There is no
mandatory parent relationship. Previous transforms needed for rendering are
presentation history, not another authoritative movement state. Adding local
transforms later requires cycle validation and an explicit propagation phase.

## Value writes and structural writes

Updating an existing velocity, timer, health value, or AI state is a value write.
The owning phase may perform it directly. Spawning/destroying an entity or
adding/removing a component is a structural write and goes through the world
command buffer during play. Direct structural APIs are restricted to candidate
construction and commit code.

Each system documents the state it reads, writes, and produces. `UpdatePlayer`
reads tick input and player state and writes movement intent. `MoveBodies` consumes
intent and writes transforms and collision results. `ResolveGameplay` consumes
collision results and writes health, game outcomes, and structural commands.
These descriptions remain useful if jobs are introduced; initially the call
sequence enforces them without runtime dependency analysis.

Components describe specific capabilities and state. Keep gameplay eligibility
explicit: health/team data and `InvulnerableUntilTick`, for example, describe
damage rules; a sprite's animation frame does not. The authored spawn kind is a
construction choice, not a required switch in every interaction. Common immutable
configuration can be referenced by a typed definition ID, while each component
keeps its own mutable values. See [definition ownership](level-data.md#definitions-and-ownership).

## Entity lifecycle and presentation tails

The observable lifecycle is prepared, active, pending destruction, then absent.
Prepared entities are private to candidate construction or commit; systems cannot
see incomplete bundles. Activation publishes a complete world or successful spawn.
Pending destruction suppresses further gameplay immediately, and commit removes
storage. These are visibility rules, not a mandatory virtual lifecycle interface.

Resource cleanup is owner-local and cannot execute gameplay callbacks. Entity
death consequences are explicit gameplay work before removal; activation effects
are explicit work after successful publication. A cancelled candidate or failed
spawn cannot play a birth sound. Systems may retain target handles, but validate
activity when using them; a death notification is a useful typed fact, not a
replacement for handle checks on delayed work.

A death animation or smoke trail may outlive the gameplay entity. Capture its
position, appearance, and required asset leases into bounded presentation-owned
state with a finite lifetime. That state does not borrow the removed component
or retain a gameplay entity solely to keep a visual effect alive. If the dying
entity must still affect gameplay, represent that as an explicit active gameplay
state with documented collision and update behavior instead.

## Inspectable waiting behavior

Start AI sequences as small, named state machines. An illustrative attack action
moves through `SelectTarget`, `Approach`, `Attack`, and `Recover`. State records
the action kind/phase, full target handle, wake/deadline tick, pending request ID,
and action revision where required. Step functions return `Running`, `Succeeded`,
`Failed`, or `Cancelled`; they do bounded work and return to the normal tick flow.
Revalidate the target and pending request on each resumption. Cancelling an action
revokes its request/revision and clears its owned intent once; it does not leave
a suspended stack pointing into the world.

This applies the readability and interruption lessons from the
[micro-thread chapters](game-world-gems-review.md#waiting-and-update-cadence).
It does not require one OS thread or fiber per entity. If a long scripted sequence
later warrants a coroutine, its frame ownership, fallible creation, cancellation,
and debugging behavior need a separate contract. It still cannot carry borrowed
component pointers across suspension. Tick deadlines and decision cadence are
specified in [Frame updates](frame-update.md#timers-and-decision-cadence).

## Command semantics

The initial command vocabulary is intentionally small: spawn a complete recipe,
destroy an entity, and any concrete component-bundle change the game actually
needs. There is no arbitrary closure command or string-based method dispatch.
Payloads own their values; they never borrow caller stack data.

All gameplay structural commands recorded in tick N commit at its end. A spawned
entity can appear in render extraction after that commit and receives its first
simulation update in tick N+1. If a projectile must move/collide immediately,
implement an explicit same-tick attack query; add a named earlier spawn boundary
only when that game's requirements justify changing the baseline.

Spawn recording returns a `SpawnRequestId`, not a usable entity handle. A complete
recipe supplies initial state, including any existing-entity references. Commit
publishes a bounded outcome record mapping request ID to new entity ID for next
tick's BeginTick phase. Version 1 does not support references between several
uncommitted spawns; assemble such a group as one validated recipe later if needed.

Destroy recording validates the handle and queue space before marking the entity
pending destruction. From that point, `IsAlive` remains true until commit, but
`IsActive` is false; subsequent gameplay phases and render extraction skip it.
Recorded collision/event payloads remain values and may describe that entity's
past action. Consumers check activity before applying new effects to it. This
distinction prevents duplicate rewards and accidental further updates while
storage is still present.

Record commands in sequence order. Duplicate destroy is idempotent and consumes
no extra command. Invalid/stale targets return an explicit status. Commands after
an accepted destroy cannot modify that entity. Destruction clears all components
and authored mappings before releasing the slot. Ordinary removal does not call
gameplay; death rewards and sounds belong to `ResolveGameplay` before commit.

## Capacity and failure behavior

Set world limits during session configuration: entity slots, each component pool,
commands, events, spawn outcomes, and pending background requests. Preallocate at
load and reuse during ticks. Entity creation checks the declared limits even if
a container happens to have extra capacity. Use `usize` for counts/indices and
checked conversion for the bounded `uint32` slot representation.

Before command playback, preflight the entire batch against identities, complete
recipes, entity/pool capacities, asset readiness, and outcome capacity. The first
implementation conservatively counts spawns against currently free slots; it
does not rely on same-batch destruction to make a spawn fit. After successful
preflight, playback uses reserved storage and operations that cannot fail under
the validated invariants. This avoids publishing partial component bundles.

If recording or preflight fails for authoritative gameplay, enter a reported
simulation fault and stop further ticks. Leave the world inspectable and offer
restart in the game UI. Do not silently omit a required spawn, damage event, or
destroy. The tick may already have performed value writes: this is not full tick
rollback. Rejected external commands leave state unchanged; expected capacity
limits may be surfaced as a gameplay choice before a tick emits a command.

Optional presentation effects can be dropped with a counter. Reserve required
presentation capacity separately so particle pressure cannot erase the player.
No hot-path operation invokes a fatal container growth method on ordinary resource
exhaustion. Debug assertions detect programming invariant violations; runtime
statuses report content, handle, and capacity failures.

## Minimum inspection and tests

Inspect by full handle or authored ID, never dense array offset. Report present
components, active/pending-destroy state, creation sequence, and storage occupancy.
An invariant check verifies that every dense entry belongs to a live entity,
sparse/dense mappings agree, no retired slot is reused, and required recipe
components exist.

Test stale reuse, wrong-world IDs, zero/out-of-range IDs, generation exhaustion
through a test seam, swap removal, duplicate components, failure at each insertion
reserve, destruction across all pools, duplicate destruction, and queued spawn
visibility. Test that a restart invalidates previous IDs even if it allocates the
same slot/generation pattern. Run lifetime cases under ASan/UBSan.

Verify shared definitions never share current health or action state. Cancel an
AI action while its target is destroyed or its result is pending. Let a death
effect continue after slot reuse and prove it contains no borrowed entity data.
