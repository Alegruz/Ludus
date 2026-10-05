# Startup and shutdown

A module is usable when it reports readiness, and safe to release when its
cleanup contract is complete. Construction success, a cancellation request or
a timeout alone proves neither condition.

The first lifecycle runner is private to the smoke application. It implements
an authored startup plan, validation, an attempted-start journal and bounded
records. Other services are not automatically managed by this runner.

## Start providers before consumers

The smoke application's plan starts **Window → RHI → Renderer**. An authored
plan states provider dependencies explicitly and validates storage, IDs, hooks
and order before any startup side effect.

A node provides begin/poll operations for start and stop. A synchronous adapter
returns readiness or completion immediately; a pending adapter yields to later
owner callbacks. All runner operations except `RequestStop` belong to the owner
thread. Recursive progress and duplicate begin operations are rejected.

## Unwind every attempted start

Record an attempt before calling its start hook. If RHI acquires part of a
session and then fails, RHI is still the first cleanup target, followed by Window.
Recording only successful starts would lose partially owned resources.

| Event | Owner action | Lifetime guarantee |
| --- | --- | --- |
| Startup succeeds | Publish readiness after checking stop requests | All required providers are usable |
| Startup fails or is cancelled | Preserve the primary cause and enter cleanup | The failed attempt is included in the journal |
| Stop begins | Close ordinary application admission | No new gameplay work enters the stopping scope |
| Consumer stop is pending | Pump required progress and keep providers alive | Consumer cleanup can still use its providers |
| Consumer reports stopped | Pop it, then stop its provider | Destruction follows reverse attempted order |
| Cleanup is unsafe or misses its deadline | Retain the affected owner/provider closure | No false clean result, restart or unsafe release |

The invariant is simple: a provider's cleanup API and borrowed storage remain
valid until its consumer reports stopped. A deadline diagnoses a liveness
problem; it cannot make unfinished callbacks, jobs or GPU work safe to destroy.

## Async completion stays with its owner

Modules own their completion records and retirement proofs. Generation checks
reject stale publication, but the callback's control record must remain alive
long enough to perform that check. Cancellation requests a disposition; late
successful resources still need disposal by their owning adapter.

The smoke adapters currently retire synchronously. The runner supports pending
and unsafe stop results, but integrating an asynchronous stop facade requires
owner-loop pumping and a stopping UI state. This runner adds no new GPU fence
or worker-join guarantees to existing backends.

## Keep reload separate

Gameplay replacement is a separate [GameHost transaction](runtime.md#gameplay-replacement).
A rejected candidate does not require stopping an otherwise healthy outer
session. Do not substitute whole-scope rollback for reload validation and
old-generation retirement.

Read the [lifecycle contract and migration plan](https://github.com/Alegruz/Ludus/blob/main/docs/architecture/module-lifecycle.md),
[private runner](https://github.com/Alegruz/Ludus/blob/main/apps/smoke/internal/lifecycle.h)
and [composition root](https://github.com/Alegruz/Ludus/blob/main/apps/smoke/application.cpp).
