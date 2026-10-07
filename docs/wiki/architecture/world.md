# Worlds and frame updates

A game owns its world and simulation sequence. `Ludus::GameplayWorld` supplies
the identity/storage baseline; the reference world demo composes game-specific
components, level transitions, fixed ticks and rendering. It is not a universal
reflection-driven ECS or a promise that all games share one component roster.

## Entity identity outlives component movement

An `EntityId` contains world identity, slot and generation. Validation checks
all three plus liveness. A handle from another world or a recycled slot cannot
resolve to a new entity. Runtime entity handles are not serialized authored IDs,
save identities or network identities.

The registry owns slot metadata. Typed sparse component pools keep dense owners,
dense values and slot-to-dense indices. Removal may move the last component into
a hole, changing its address and iteration position without changing entity
identity. Keep handles across boundaries rather than borrowed component pointers.

The baseline reserves bounded storage and accepts plain trivially copyable and
trivially destructible components. The game explicitly removes destroyed entities
from every owned pool. Adding a component type also adds its removal/invariants.

See [entity storage](../../architecture/entity-world.md)
for exhaustion policies, lookup checks and structural-change rules.

## Publish a level at a boundary

Authored level data describes intent. Build and validate a candidate world before
replacing the active one. A failed candidate preserves the current world; a
successful replacement publishes at the session/frame boundary with a fresh
world identity. The reference application demonstrates transactional loading;
the broader shared content-loading design has its own future phases.

This separates source edits, candidate preparation and live state. A background
result must validate both its target world and relevant revision before the
owner applies it. See [level data](../../architecture/level-data.md).

## Simulation ticks and presentation frames differ

| Phase | Owner responsibility |
| --- | --- |
| Platform/input | Pump events and route UI consumption before gameplay input |
| Session transitions | Poll loading and publish ready replacements at a legal boundary |
| Fixed simulation | Run an explicit, bounded sequence of game phases |
| Presentation | Extract/render current completed state without exposing mutable world storage |

The reference policy is 60 simulation ticks per second, at most 250 ms accepted
frame delta and four catch-up ticks per callback. Those are application policies,
not engine performance guarantees. After exhausting the budget, discard excess
whole-tick debt and retain the fractional remainder. Count discarded time.

Pause, session transitions and browser suspension reset debt; resume does not
simulate time spent hidden. A browser callback must return without busy-waiting
for the next tick, worker or GPU readiness. Games select their phase sequence as
ordinary calls and can choose different recorded timing policies.

## Parallelism requires a publication contract

The world baseline runs on its owner thread. [Jobs](foundation.md) can compute
against immutable inputs or disjoint output ranges, but they do not authorize
concurrent structural world mutation. Retain input/output storage through
completion, then merge valid results in stable order at an explicit tick boundary.

Read [frame/tick policy](../../architecture/frame-update.md),
[the reference application](../../examples/world-demo.md)
and [the game-world design](../../architecture/game-world.md).
