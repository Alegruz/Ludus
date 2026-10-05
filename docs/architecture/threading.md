# Threading architecture

Status: the bounded graph kernel is implemented. Work stealing and engine
consumer migration are later, measured slices. This design builds on
[frame ownership](frame-update.md#background-work-and-eventual-jobs) and
[profiling](profiling-final.md). It does not claim universal optimality or AAA
scale performance from a small synthetic benchmark.

## Initial proposal, before the reference review

Use tasks over explicit inputs instead of a thread for every engine subsystem.
Keep world mutation, Platform and the current RHI on their owning application
thread. A CPU pool runs bounded computations against immutable snapshots or
exclusive output ranges. The owner merges results in stable entity/request order
at an explicit tick boundary, validating world identity and data revision.

Build a graph before publishing it. A dependency describes both ordering and
visibility, and an entire graph acts as a lifetime fence for its input/output
storage. Only ready tasks run. Split a computation that needs a wait into two
callbacks and an edge. Reserve bounded storage up front, expose admission errors,
and provide an executor with zero workers to debug the same callbacks serially.
Keep OS threading private and report startup failure without C++ exceptions.

The default optimization strategy is to reduce shared state and schedule coarse
ranges, then measure. A shared ready queue is the auditable baseline; worker-local
queues and work stealing are a compatible next backend if lock contention or
queue latency is material. Avoid hidden global pools, recursive waits, fibers,
per-job heap closures, and a dedicated scheduler thread.

## Reference review and resulting changes

The index is the local [game-dev-gems-toc.md](../../references/game-dev-gems-toc.md).
The following are chapter-text reviews, rather than conclusions from titles.
Printed page numbers and one-based PDF pages differ.

| Source reviewed | Useful idea | Change to the initial proposal | Limits of the historical sample |
| --- | --- | --- | --- |
| Julien Hamaide, Game Programming Gems 7, 1.9, Multithread Job and Dependency System, pp. 87-96 ([PDF 120-129](../../references/Game%20Programming%20Gems%207.pdf#page=120)) | Persistent pool, dependency counters, generation-aware entry identity | Add graph identity plus reset generation to handles; validate and freeze the DAG; publish only when incoming edges resolve | An absent prerequisite cannot imply success. Keep failure, cancellation and blocked descendants distinct. Avoid its scheduler thread and OS event per group. |
| Brad Werth, Game Engine Gems 1, 22, Holistic Task Parallelism for Common Game Architecture Patterns, pp. 381-390 ([PDF 409-418](../../references/Game%20Engine%20Gems%201.pdf#page=409)) | Continuations, range decomposition, helping waits, local queues | Add an explicit helping option to Wait; make blocking callback waits a rejected operation; require coarse range benchmarks before consumer migration | Its synchronized spinning callbacks can deadlock an exhausted pool. Worker initialization belongs at startup, rather than in barriers disguised as jobs. |
| Jon Parise, Game Engine Gems 1, 21, Multithreaded Object Models, pp. 371-379 ([PDF 399-407](../../references/Game%20Engine%20Gems%201.pdf#page=399)) | Owner messages and buffered state make synchronization explicit | Require snapshot lifetime through Wait and stable owner-side application; retain current render ownership | A command queue does not protect concurrent readers of mutable objects. Extra frames cost latency and memory and need explicit event delivery rules. |
| Julien Hamaide, Game Engine Gems 2, 29, Thread Communication Techniques, pp. 459-467 ([PDF 475-483](../../references/Game%20Engine%20Gems%202.pdf#page=475)) | Bound communication, establish producer/consumer roles, aggregate locally | Reserve the whole graph including ready capacity; expose synchronized state snapshots; keep worker outputs disjoint | Do not reproduce volatile indices and architecture-specific barriers as portable C++ synchronization. FIFO arrival is not deterministic world update order. |
| Jean-François Dubé, Game Programming Gems 8, 4.3, Efficient and Scalable Multi-Core Programming, pp. 373-384 ([PDF 388-399](../../references/Game%20Programming%20Gems%208.pdf#page=388)) | False sharing, allocator contention, sleeping idle threads, cooperative scheduling | No allocation or busy polling in steady scheduling; wake from a protected predicate; benchmark tiny and substantial jobs separately | Core-count and spinlock assumptions must be remeasured on modern SMT/hybrid CPUs. The historical volatile examples are not C++23 memory-order proofs. |

Modern cross-checks: [enkiTS](https://github.com/dougbinks/enkiTS) demonstrates
upfront allocation, dependencies, range work and helping; the primary
[Taskflow scheduler paper](https://taskflow.github.io/taskflow/icpads20.pdf)
describes work stealing for irregular task graphs. These inform the direction,
not a dependency import or a claim that this baseline beats them. The portable
memory model is [C++ data races](https://eel.is/c++draft/intro.races), not the
older books' volatile examples. [Emscripten's pthread restrictions](https://emscripten.org/docs/porting/pthreads.html)
require a serial default without main-browser-thread blocking or COOP/COEP.

## Kernel and public contract

`Ludus::FoundationThreading` depends only on FoundationBase and private native
OS primitives (Threads is an exported system link dependency on POSIX). Public
`jobs.hpp` contains types and function declarations, with no OS, STL concurrency,
logging, profiling or container headers. The implementation uses C++23 atomics
and native pthread/Windows operations; thread creation never uses std::thread's
throwing constructor. Browser builds use the same graph and a serial backend.

A `JobGraph` is initialized with job/edge capacities. Build on one owner thread,
add callbacks/context pointers, declare `DependsOn(dependent, prerequisite)`, and
`Seal`. Seal performs O(V+E) Kahn validation using preallocated scratch space.
Self edges, duplicate edges, foreign/stale handles, exhaustion and cycles have
explicit statuses. A handle includes a graph ID unique within its FoundationThreading runtime, a
reset generation and an index. Identity exhaustion returns an error rather than wrapping.

A `JobSystem` is explicitly initialized with 0-64 workers. Worker count is a
caller budget excluding any helping caller. The engine owner must reserve CPU
budget for simulation, driver, audio and I/O threads; logical CPU count is not
an instruction to use every hardware thread. Initialization creates all workers
once; a failed startup joins every worker already started before returning.

Submit reserves one graph until Wait releases it. A second graph returns Busy;
there is no hidden backlog or unbounded retry. Sealed graphs can be submitted
repeatedly with updated contexts after Wait. Reset invalidates handles and permits
rebuilding. An empty sealed graph succeeds. The serial Submit executes all work
synchronously and retains the graph until Wait, keeping the same lifetime API.

```mermaid
flowchart LR
    O[Owner: build and seal] --> S[Submit bounded graph]
    S --> Q[Ready indices]
    Q --> W[CPU workers or helping caller]
    W --> C[Publish outcome and release edges]
    C --> Q
    C --> F[Wait: lifetime fence]
    F --> M[Owner: validate and merge results]
```

Only callbacks whose prerequisites succeeded run. A callback returns Succeeded,
Failed or Cancelled. Invalid callback outcomes become Failed. A failed/cancelled
prerequisite blocks descendants without calling their functions, while unrelated
branches finish. Failure dominates cancellation in the graph outcome. Explicit
Cancel skips tasks that have not started; running callbacks retain their actual
outcomes. It cannot stop an executing stack or roll back side effects. Cancel
after the last task completed does not change successful completion into failure.

Wait optionally executes ready CPU callbacks on its calling thread; choose
`help=false` when thread identity/TLS or responsiveness makes that inappropriate.
Callbacks are free to run on any worker or a helping caller and have no affinity
guarantee. Platform/RHI/audio callback work does not enter this pool. The serial
executor executes on the caller. A TLS guard rejects recursive Submit, Wait,
Initialize and Shutdown from *any* pool callback with InsideJob, including calls
to another pool. Dependencies must be declared before publication.

Callback code itself must remain loaded through Wait, including gameplay DLL
unload/live reload; completion cannot validate a pointer into unloaded code.
The host owns the runtime, systems, graphs and handles across reloads. IDs and
handles belong to that runtime instance; separately linked copies of the static
SDK in gameplay DLLs do not share its identity counter. Do not exchange or retain
handles between independent runtime copies or across their unload/reload.

Shutdown drains accepted work, sets stop under the gate, wakes sleepers, joins
all started threads and frees state. System destruction does the same. Destroying
a submitted graph drains it through its system. Destruction from a callback of
its active graph or a system is a fatal lifetime-contract violation: that stack
cannot wait for itself. Application shutdown must first stop admissions, retain
contexts, drain/cancel, then destroy inputs and owning subsystems.

Build/reset, Submit/Wait, initialization, shutdown and destruction are serialized
by the application owner. Cancel and Snapshot may run concurrently with execution
and owner Wait, while lifecycle is stable. GetOutcome may read a published node
concurrently with execution, but never race graph reset/rebuild/destruction. A
caller cannot race the system's destruction with any operation. An arbitrary
callback that blocks indefinitely can prevent drain; the kernel cannot enforce
an execution time bound or make unsafe user code race-free.

## Synchronization proof and debugging

One system gate protects ready indices, dependency counters, cancellation,
active graph, unfinished count and stop. Callback code runs outside the gate.
Completing a callback takes that gate before publishing outcomes and releasing
edges; a dependent acquires the same gate before dispatch. Therefore predecessor
writes happen before dependent reads. Wait acquires the gate and observes zero
unfinished tasks before returning; all completed callback outputs are visible.
Per-node outcomes also use release stores/acquire loads for GetOutcome readers.

Workers sleep only after checking the ready/stop predicate while holding the gate.
Wait checks unfinished under that same gate. Native condition waiting atomically
releases the gate; wakes and completion transitions occur with it held. Broadcast only when
ready work transitions from empty to nonempty, all jobs complete, or control
changes require it. Every
wake rechecks predicates, including spurious wakes. There is no spin loop, lost
wake gap, second lock order, or executing user callback under a scheduler lock.
Each node enters ready once, so the graph's MaxJobs ring can never overflow.

Snapshot gives pending/running/succeeded/failed/cancelled/blocked counts while
holding the gate. GetOutcome identifies the exact node. Debug a suspect graph
with WorkerCount=0, then one worker, then a configured worker budget. Serial order
is repeatable for fixed construction/edge order; parallel sibling completion is
unspecified. Stable output merge order and deterministic per-job random streams
remain the caller's responsibility. A serial run is an oracle, not a proof of
absence of races. Use TSan, ASan/UBSan, bounded CTest timeouts and repeated graph
reuse/fan-in/failure/cancellation/shutdown tests.

## Performance and staged growth

The implemented ready ring is O(1) push/pop, with O(out-degree) completion work.
All scheduler heap storage and native threads are created at initialization.
No scheduler allocation occurs in add/seal/submit/execute/wait/reset/shutdown
(except OS/runtime activity during thread teardown). One short gate remains a
scaling limit; wide, very small graphs should batch work. Snapshot is an O(V)
diagnostic operation and must not be polled every task. WakeAll is deliberately
easy to audit and may create a wake herd; measure before splitting work and
completion conditions or introducing targeted wake policies.

Build the optional `ludus_threading_benchmark` with
`-DLUDUS_BUILD_THREADING_BENCHMARK=ON`. It compares complete submit-to-Wait batches
with identical outputs in serial and parallel. Report hardware, worker counts,
task count/grain, median timings, checksum, idle/background load and build flavor.
Recorded [validation and timing evidence](../development/threading-evidence.md)
includes the hardware, shared-machine load and output checks.
Tiny callbacks measure dispatch overhead; useful arithmetic measures a possible
break-even. Synthetic speedup does not establish game-frame improvement.

Subsequent reviewable slices, in order:

1. Integrate a profiled immutable-range engine workload and owner merge. Measure
   frame p50/p95/p99, critical path and dispatch overhead against the serial oracle.
   Add range helpers only around that real need, preserving bounded admission.
2. Add optional per-task name/site IDs and profiler dispatch-to-start/completion
   flows, queue depth and waiting duration. Keep callbacks/trace allocation outside
   scheduler locks; tracing must degrade without disturbing execution. Use explicit
   worker initialization hooks for TLS/trace registration if needed.
3. If contention is material, replace the ready ring with bounded worker-local
   deques plus an external injection queue. Specify owner/thief rules, release /
   acquire publication, reclamation, overflow and wake protocol before code. Compare
   with enkiTS under Ludus's no-exception/lifetime constraints. Validate ARM64 and
   TSan and measure cache/queue overhead; keep the graph API and serial executor.
4. Add concurrent graph admission/priority lanes only when frame-critical and
   background work must overlap. Reserve completion capacity and preserve fairness;
   long I/O stays on an independent service with continuation publication.
5. Browser pthreads require a separately deployable build with isolation headers,
   off-main execution and nonblocking UI polling. Render-thread ownership, GPU
   resource retirement, NUMA affinity and fibers each need their own measured ADR.

The first kernel provides a maintainable correctness baseline and a controlled
migration path. Those later capabilities are deliberately not advertised as
implemented by this PR.
