# ADR 0020: Bounded CPU task graphs with an explicit serial executor

## Status

Accepted for the initial kernel. See [threading architecture](../architecture/threading.md)
for the initial proposal, chapter review, synchronization proof and staged extensions.

## Decision

Add FoundationThreading with reusable, preallocated JobGraph and explicitly owned
JobSystem. Freeze and validate dependency edges before submission; admit one graph
per system, reject capacity/lifecycle errors, and retain inputs until Wait. Native
workers and a helping caller execute ready callbacks outside a short scheduler
gate. Zero workers runs the same graph serially, including browser builds.

Callbacks run to completion and report success/failure/cancellation. Unsuccessful
prerequisites block descendants. Generation-checked handles reject stale identity.
Idle workers sleep on a protected predicate; initialization is fallible, and
shutdown drains and joins. The native thread boundary reports errors without
C++ exceptions. Engine objects remain with their existing owning thread.

## Consequences

The baseline is small, portable and testable with explicit backpressure and no
steady scheduler allocation. A shared queue/gate and broadcast wakes limit
fine-grained scaling. Parallel callback order is unspecified; callers must use
immutable inputs, disjoint outputs and stable result merge order. A blocked user
callback can block shutdown. Work stealing, multiple concurrent graphs, trace
flows, priorities, fibers and render ownership are separate measured changes.
