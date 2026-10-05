# Foundation and jobs

Foundation supplies the vocabulary and bounded services used by higher modules.
An application still owns their setup, storage and teardown; linking a library
does not silently create a global scheduler or an engine session.

## Choose the smallest dependency

| Facility | Responsibility | Architectural boundary |
| --- | --- | --- |
| FoundationBase | Fixed-width types, build/platform macros, assertions | Emergency diagnostics work independently of logging |
| FoundationMemory | Allocation-domain identity and system backend | Original domain/context must outlive all allocations |
| FoundationStrings and FoundationContainers | Owned text and explicit storage | Reserve/reuse capacity; handle fallible operations at boundaries |
| FoundationMath and FoundationTime | Math vocabulary and time facilities | Portable APIs; game timing policy remains application-owned |
| FoundationParsing / FoundationParsingJson | Bounded parsing and JSON adapter | Parsing errors are explicit; parser internals remain private |
| FoundationLogging and FoundationProfiling | Diagnostics and observation | Observation failure degrades gracefully rather than failing gameplay |
| FoundationThreading | Bounded CPU task graphs | One owner controls graph/pool lifecycle; jobs operate on explicit inputs |

Include the header declaring what you use. `core.h` does not imply strings,
containers or these services. New engine code uses Ludus aliases such as `uint32`,
`usize` and `float32`, with explicit errors and no C++ exceptions. See the
[contributor rules](https://github.com/Alegruz/Ludus/blob/main/AGENTS.md).

## Allocation identity is also lifetime

`AllocationDomain` holds a stable context plus allocation/free callbacks.
Allocation failure returns `nullptr`; freeing uses the original domain, byte
count and alignment. The system domain has process lifetime, including static
and thread-local teardown. No global replacement of C++ allocation is implied.

The implemented seam is narrower than the full memory proposal. Allocation
tracking, specialized arenas and alternative allocator evaluation must not be
inferred from that older design document. Start with the
[current memory API](https://github.com/Alegruz/Ludus/blob/main/modules/foundation/memory/include/ludus/foundation/memory/allocation_domain.hpp),
then read the [memory design and its historical scope](https://github.com/Alegruz/Ludus/blob/main/docs/architecture/memory-management.md).

## Background computation has an explicit fence

The job kernel separates graph construction from execution:

1. Initialize bounded job/edge storage and the desired worker budget.
2. Add callbacks with context pointers and explicit prerequisite edges.
3. Seal the graph, validating handles and the DAG before publication.
4. Submit it while retaining immutable inputs and exclusive output storage.
5. Wait for retirement, inspect the outcome, then apply results on the owner.

The default worker budget is zero. Native applications can explicitly request a
pool; browser builds use the serial executor and reject a nonzero worker count.
One graph remains admitted until `Wait`, even after callbacks finish. `Reset`
invalidates old graph handles. Cancellation skips queued work; running callbacks
must finish before their contexts can be released.

Jobs do not wait on other jobs or perform synchronous I/O. The owner can help
execute jobs during `Wait`, so that call can run callbacks on its own thread.
Select the documented helping policy deliberately. Completing CPU work does not
prove GPU retirement or make concurrent world mutation safe.

Read the [module API and example](https://github.com/Alegruz/Ludus/blob/main/modules/foundation/threading/README.md)
and [task graph design](https://github.com/Alegruz/Ludus/blob/main/docs/architecture/threading.md).
Work stealing and engine consumer migration are later measured slices.

## Find the specialized guides

For algorithms and authoring details, use [deterministic randomness](../guides/randomness.md)
and [reference reading paths](../learn/references.md). For service ownership at
application boundaries, continue with [startup and shutdown](lifecycle.md).
