# ADR 0021: Bounded networking core and transport boundary

## Status

Accepted for the portable core. Backend adoption remains a gated spike.

## Context

Ludus needs native, headless and browser networking without violating its
exception-free module graph or coupling simulation to transport threads.
Unbounded queues, implicit serialization and a second implementation of UDP
crypto/reliability would make latency and maintenance unpredictable.

## Decision

Add `Ludus::NetworkCore` depending only on FoundationBase, available on native
and web. Use an explicit versioned, bounded byte envelope and checked bit packing,
per-channel serial history, owned bounded queues and caller-driven seeded fault
simulation. Do not serialize C++ object layout. Failures are explicit statuses;
all hot paths are noexcept and use fixed/caller-owned storage.

Use authoritative fixed-tick gameplay with separate session, transport and
replication ownership as specified in [networking architecture](../architecture/networking.md).
A future secure congestion-controlled backend supplies reliable Control/Bulk and
unreliable Input/Snapshot lanes. GNS and WebTransport are candidates, with pinning,
exception/dependency audit and real endpoint/browser interop gates before adoption.
No production transport, authentication or replication is implied by this ADR's
initial implementation.

## Consequences

The same protocol decoder can serve gameplay tests and developer inspection.
Queue saturation and obsolete snapshots remain observable and bounded. Fixed
storage has a per-instance cost; capacity increases require measurements.
Applications must validate schemas/authority before committing sequence state,
retain dequeued messages until transport acceptance, reset per-peer state on
reconnect, and provide clocks and budgets explicitly. The core does not provide
reliability, congestion control or authentication by itself.

The [reference review](../architecture/networking-reference-review.md) separates
adopted Gems principles from obsolete APIs/security recipes and deferred scaling
ideas. No third-party transport dependency is introduced in N1.
