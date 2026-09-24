# Historical profiling references: critical literature review

Status: **Evidence collection for PR #23; no architecture decisions changed.**

Reviewed against the independent [profiling baseline](profiling.md) at commit
`42863c25004b110b9bff02624b9524ad2979efcb` on
[PR #23](https://github.com/Alegruz/Ludus/pull/23). The baseline is a proposal,
not an implemented or benchmarked subsystem. This review does not validate its
overhead estimates, select a backend, or prescribe a replacement design.

The strongest contributions are semantic instrumentation, accessible feedback,
caller-sensitive attribution, and allocation provenance that survives scope
exit. The historical containers, clocks, and interception techniques are much
less transferable. None of these papers establishes a scalable capture
architecture for a modern job-based engine.

## 1. Sources, method, and historical context

The local articles, including their printed listings, were read in full. GPG1
and GPG3 have no usable embedded text and were inspected as rendered pages.
GPG2 and GPG8 were read through extracted text; GPG8's UI and allocation-header
listing were also checked visually. Page references below use **printed book
pages**; PDF positions are one-based and differ between these local scans.

| Key | Article | Approximate era | Printed pages | Local PDF pages |
| --- | --- | --- | --- | --- |
| R | Steve Rabin, *Real-Time In-Game Profiling*, GPG1, §1.14 | 2000 | 120–130 | 118–128 |
| E | Jeff Evertt, *A Built-in Game Profiling Module*, GPG2, §1.11 | 2001 | 74–79 | 71–76 |
| H | Greg Hjelstrom and Byon Garrabrant, *Real-Time Hierarchical Profiling*, GPG3, §1.17 | 2002 | 146–152 | 149–155 |
| L | Ricky Lung, *Design and Implementation of an In-Game Memory Profiler*, GPG8, §4.6 | 2010 | 402–408 | 417–423 |

[R]: ../../references/Game%20Programming%20Gems%201.pdf#page=118
[E]: ../../references/Game%20Programming%20Gems%202.pdf#page=71
[H]: ../../references/Game%20Programming%20Gems%203.pdf#page=149
[L]: ../../references/Game%20Programming%20Gems%208.pdf#page=417

The volume dates are also recorded in the repository's
[reference catalog](../../references/game-dev-gems-toc.md). Companion CD source
was not present among the inspected references and was not audited. A defect or
omission in a printed sketch is not evidence that the complete CD implementation
has that defect. No historical implementation was compiled or benchmarked here.

Descriptions under **Architecture** report the articles. Cost analysis,
classifications, and modern reinterpretations are this review's engineering
judgments. Claims that require modern platform/tool context are supported by
primary documentation links. Hypothetical costs are labeled as such.

R, E, and H address interactive engines around a dominant serial frame loop.
Their published mechanisms do not specify general concurrent recording, even
though actual games could use background threads. External tooling existed:
R explicitly positions his system as complementary, and E discusses commercial
and hardware tools at length. Their contribution was inexpensive access to
game-specific context during play, not the discovery of profiling itself.

L is a different generation: it explicitly addresses multicore applications,
TLS, remote display, and freeing on another thread. Its sample interception
code remains tied to x86 and Win32. Treating all four as single-threaded designs
would erase an important advance. Conversely, TLS alone does not establish
scalability for many workers, fibers, or migrated jobs.

Classification meanings used throughout:

| Classification | Meaning in this review |
| --- | --- |
| **Strong candidate** | The insight deserves explicit comparison with Ludus's baseline; this is not approval to implement it. |
| **Potentially useful** | Value depends on a measured workload or a concrete workflow. |
| **Niche** | Appropriate in a constrained environment or specialized diagnostic mode. |
| **Obsolete** | The historical mechanism or justification has been superseded for Ludus's target. |
| **Actively undesirable** | Copying it into the target runtime would undermine correctness, scalability, or engine failure policy. |

## 2. Rabin: Real-Time In-Game Profiling

### 2.1 Problem and key idea

When a particular gameplay situation becomes slow, the team needs to identify
which meaningful subsystem changed while that situation is still reproducible.
A programmer's intuition about the most recently changed function is weak
evidence. Designers and testers also need access without operating a specialist
profiler. Call counts can expose behavioral bugs even before optimization:
work may run twice or never run. **Make a few meaningful measurements cheap to
add and immediately visible to the whole team.** [R, pp. 120–122][R]

### 2.2 Architecture

- **Concepts:** named explicit intervals, repeated-call accumulation within a
  frame, child-time subtraction, parent/child presentation, and persistent
  summary history. Instrument broadly, identify a suspect, then add detail.
- **Mechanisms:** paired name-based begin/end functions search a global array.
  The example reserves 50 sample slots and a separate 50-entry history array;
  each record contains a 256-character name. End scans open samples to infer
  the immediate parent from start times. It accumulates inclusive duration and
  charges that duration to the parent's child total. Display subtracts child
  totals to produce exclusive percentages. Recursive overlapping use of one
  name is explicitly unsupported. [R, pp. 123–129][R]
- **Tooling/UI:** indented text with average/minimum/maximum percentages and
  calls per frame; a text buffer is formatted at frame end and drawn next
  frame. File output is suggested. A C++ scope object and macro are proposed
  as an improvement over manually matched names. [R, pp. 121–126][R]
- **Era assumptions:** a handful of instrumented regions, one active nesting
  chain, all intervals closed before frame reset, and an available game text
  renderer. Timing is abstracted as `GetTime()` in seconds, stored in `float`;
  no particular hardware timer is mandated. Attributing `RDTSC` to this article
  would be incorrect. [R, pp. 123–124][R]

The printed history algorithm is more specific than the prose: its average is
a recursively weighted estimate using an elapsed-time-dependent weight. Its
minimum and maximum immediately accept new extremes but otherwise drift toward
the current sample. They are **decaying extrema**, not exact all-time or fixed
window extrema. The printed `newFraction`/`newRatio` naming inconsistency also
reinforces that listings need validation before reuse. [R, pp. 129–130][R]

### 2.3 Relevance classification

| Significant idea | Classification | Reason and modern interpretation |
| --- | --- | --- |
| Semantic scopes and progressive instrumentation | **Strong candidate** | Reveal which gameplay operation changed without instrumenting every tiny function. Preserve this discipline even with an external viewer. |
| Calls per frame as behavioral evidence | **Strong candidate** | Distinguishes more work from slower work and can expose incorrect execution frequency. Counts need a defined frame/window and loss status. |
| Inclusive versus exclusive attribution | **Strong candidate** | Prevents double-counting nested synchronous work. Derive both views with explicit definitions rather than displaying ambiguous “time.” |
| Preallocated bounded storage | **Strong candidate** | Avoids allocation-induced distortion and makes diagnostics memory predictable. Capacity and overflow behavior must match the actual workload. |
| RAII scope plus compile-out boundary | **Strong candidate** | Reduces mismatched ends and works with early returns in exception-free C++23. It does not automatically handle coroutine suspension. |
| Readable live feedback for non-programmers | **Strong candidate** | Shortens reproduction and triage loops. The feedback may be a small HUD or an attached viewer; a bespoke renderer is not required. |
| Smoothed display values | **Potentially useful** | Stabilize human-readable output if clearly labeled and kept separate from retained evidence. |
| Decaying extrema presented as generic min/max | **Actively undesirable** | Can conceal an earlier hitch and implies statistics the algorithm does not compute. Use explicitly labeled decay or actual window extrema. |
| Exactly 50 slots with inline names | **Niche** | Adequate for a tiny fixed diagnostic panel, not a general engine capture. Preallocation survives; the historical capacity/layout does not generalize. |
| Global string scans and inferred parent ordering | **Obsolete** | Work scales with active names; it merges caller contexts and lacks a concurrent ownership model. Explicit invocation nesting and stable site identity supersede it. |
| Absolute timestamps in single-precision seconds | **Actively undesirable** | Subtracting large rounded values loses short-interval precision as uptime grows. Retain integer ticks and convert durations for presentation. |
| Assertions on capacity exhaustion; unbounded string copies/formatting | **Actively undesirable** | A diagnostic must not destabilize the game. The sample's overflow response and string handling do not meet Ludus's failure policy. |
| Optimized-build measurement plus external profiling | **Strong candidate** | R already warns that instrumentation of tiny frequent work can dominate it. The boundary remains sensible. |

### 2.4 Hidden costs and reliability limits

The preallocated arrays remove allocation cost, not search cost. Begin/end
lookup is linear in active names, and parent inference adds another scan.
Repeated history lookup adds frame-end work. Large inline names increase the
working set even when only the timing fields are needed. These are review
inferences from the listings, not measured overhead figures.

A name can be reached under several parents; one record and one indentation
depth cannot preserve each calling context. Timestamp ties and insufficient
precision make inferring nesting from time less reliable than recording the
nesting itself. Merely adding a lock would serialize execution and still leave
different threads incorrectly treated as one nesting chain.

Summary history loses the sequence of invocations and the scene that produced
a peak. Missing samples are not equivalent to zero samples: history updates
only for names encountered, so intermittent work can retain stale statistics.
Percentages also depend on the denominator. A subsystem can take the same
milliseconds while its percentage falls because another subsystem slowed down.

The example includes text rendering inside the graphics interval and performs
formatting after the main-loop sample. Thus the profiler's costs do not all
appear in one obvious bucket. R acknowledges display and fine-grained
instrumentation overhead; that warning is stronger than the opening claim of
negligible bookkeeping. Public-header parse time and binary metadata growth
are not analyzed. Keep any modern frontend thin under Ludus's existing build
budget rather than assuming a small macro has no total build cost.

### 2.5 Modern reinterpretation

```text
Global named samples + end-of-frame table
    -> semantic work attribution visible during reproduction
    -> per-execution-context intervals, derived aggregates, optional live summary

History array with smoothing and decaying extremes
    -> stable feedback plus awareness of variability
    -> labeled display smoothing alongside retained frame/hitch evidence
```

If designing today, the likely survivors are deliberate scope placement,
invocation counts, bounded overhead, RAII, and immediate feedback. The 50-slot
registry and its parent inference are implementation shortcuts, not prerequisites
for those benefits.

## 3. Evertt: A Built-in Game Profiling Module

### 3.1 Problem and key idea

A large engine needs coordinated instrumentation that several teams can use
independently, including on machines where a troublesome frame can be reproduced
but a specialist tool is inconvenient. Aggregating an entire gameplay run mixes
different bottlenecks. **Organize timing measurements into selectable subsystem
groups and make them an ordinary engine diagnostic service.** This paper is
primarily about operational usability and modularity. [E, pp. 74–77][E]

### 3.2 Architecture

- **Concepts:** centralized discovery/management, decentralized instrumentation
  ownership, group filtering, frame-local totals, and a small stable interface.
- **Mechanisms:** counters have a FourCC group, textual name, and integer handle
  assigned on registration. Start/stop accumulate elapsed time. `DrawCounters`
  finalizes the frame, maintains a maximum, and clears totals. Registering once
  is preferred; convenience lookup by name is allowed but identified as slower.
  Scope wrappers and compile-out macros are suggested. [E, pp. 76–78][E]
- **Tooling/UI:** debug-console group toggles, configurable refresh interval,
  colored bars, milliseconds or percentages, current/maximum display, and
  cautions about confusing autoscaling. Clock and debug drawing have platform
  abstractions. Filtered disk output for long server runs is suggested, not
  specified as a complete capture protocol. [E, pp. 77–79][E]
- **Era assumptions:** inline Win32/x86 `RDTSC` replaces QPC because the latter
  was considered expensive. Frequency is estimated once when enabled by
  sampling around a sleep of at least 500 ms. CPU frequency is treated as the
  tick conversion rate. The UI assumes a conventional scene/frame endpoint.
  No thread-safe counter-instance or reentrancy model is supplied. [E, pp. 74–77][E]

Here “counter” usually means a timed accumulator, not Ludus's separate scalar
counter/gauge subsystem. Similar terminology is not evidence of identical
data models. E also does not specify H's dynamic call-context tree.

### 3.3 Relevance classification

| Significant idea | Classification | Reason and modern interpretation |
| --- | --- | --- |
| Grouped, independently owned instrumentation | **Strong candidate** | Different teams need useful names and focused views without coordinating every probe. Categories can be metadata and capture policy. |
| Register once, use a handle repeatedly | **Strong candidate** | Moves name lookup out of frequent measurement. Identity, lifetime, and failure handling must remain explicit. |
| Separate clock and drawing implementations | **Strong candidate** | Capturing measurements should not depend on one OS clock API or renderer. |
| Frame-local analysis of changing gameplay | **Strong candidate** | A session-wide average can hide transitions between unrelated bottlenecks. Frames should be an analysis axis rather than a universal storage lifetime. |
| Sampling controls distinct from display controls | **Strong candidate** | E uses groups for both purposes; modern evaluation should preserve the distinction so hiding a group does not unexpectedly destroy capture evidence. |
| Tunable refresh, units, maxima, and stable axes | **Potentially useful** | Good summary UI reduces cognitive load; implementation belongs in a consumer and requires defined windows/denominators. |
| Exportable filtered server summaries | **Potentially useful** | Useful without a game window. Summary export alone cannot reconstruct an individual hitch. |
| A bespoke colored in-game bar chart | **Niche** | Useful for QA or restricted target hardware; external tools often cover deep investigation with less engine UI maintenance. |
| FourCC as the organizing namespace | **Obsolete** | A small manually managed namespace provides little benefit over stable descriptors/categories and has compiler/encoding pitfalls noted by E. |
| Name registration/lookup every timed use | **Actively undesirable** | Makes probe cost scale with registry/string work. E itself cautions against frequent use. |
| Bare `RDTSC` plus CPU-MHz/sleep calibration | **Obsolete** | Not a sufficient modern clock contract; ordering, frequency meaning, migration, and platform support need explicit handling. |
| Resetting measurement state in `DrawCounters` | **Actively undesirable** | Collection semantics change when rendering is hidden, skipped, or absent. Concurrent reset can also lose updates. |
| Fixed “under 5% enabled / far under 1% disabled” acceptance rule | **Obsolete** | Historical goals are not evidence of acceptable perturbation at modern event rates or frame deadlines. |
| Measure profiler cost and representative optimized workloads | **Strong candidate** | E explicitly recognizes that slowing CPU submission can hide graphics stalls. This remains a fundamental validity check. |

### 3.4 Hidden costs and reliability limits

A shared handle is an identity, not necessarily a safe place to store an active
start timestamp. Recursion, simultaneous callers, and runtime enable changes
between start and stop require invocation state and pairing rules absent from
the interface sketch. Synchronizing a central manager on every call could
turn instrumentation into the workload's bottleneck. Registration/deletion
also raises stale-handle and metadata-lifetime questions.

Drawing, formatting, group enumeration, and console integration all require
maintenance. Updating text only every 30 frames reduces presentation work but
does not make sampling cheaper or retain those 30 individual frames. If maxima
and current percentages refer to different windows, comparisons can mislead.
The proposed server export needs bounded buffering and disconnect/I/O policy;
those mechanisms are not established by a single manager interface.

The clock argument must be revisited rather than dismissed as foolish for
2001. Modern QPC can use an OS-managed TSC; Microsoft recommends QPC over
direct TSC reads for consistency and portability. A TSC tick is not a retired
instruction or necessarily a current-frequency CPU cycle. Modern measured
clock selection should replace the old unconditional bypass rationale.
[Microsoft timing guidance](https://learn.microsoft.com/en-us/windows/win32/sysinfo/acquiring-high-resolution-time-stamps)

E does not quantify include cost, inline-site code growth, or registry footprint.
An abstract module interface is useful, but putting containers, platform APIs,
or a viewer SDK in a ubiquitous header would still violate Ludus's build-time
constraints. No percentage quoted by E bounds those costs.

### 3.5 Modern reinterpretation

```text
FourCC group + counter manager + debug-console bars
    -> team-owned instrumentation and selective investigation
    -> registered descriptors/categories, capture filters, independent consumers

DrawCounters finalizes and clears measurements
    -> a defined reporting interval
    -> explicit snapshot/frame semantics independent of presentation cadence
```

The likely modern survivors are group ownership, cheap stable handles,
selective capture, portability boundaries, and the warning about changing the
CPU/GPU bottleneck. Centralized naming does not imply centralized hot-path
mutation, and integrated collection does not imply an in-game-only viewer.

## 4. Hjelstrom and Garrabrant: Real-Time Hierarchical Profiling

### 4.1 Problem and key idea

A flat subsystem total identifies an expensive area but does not explain which
caller or nested operation made it expensive. Repeatedly adding temporary
timers is slow. **Retain a browsable hierarchy of instrumented calling contexts
so investigation can move from a subsystem to its expensive descendants while
the game runs.** The authors explicitly stop short of claiming the resolution
of a sampling profiler. [H, pp. 146–147][H]

### 4.2 Architecture

- **Concepts:** a calling-context tree, inclusive node totals and call counts,
  drill-down, and progressively finer semantic scopes. The tree follows
  instrumentation nesting, not every native function call.
- **Mechanisms:** a manager maintains `CurrentNode`. Entry searches its immediate
  children by static name-pointer equality, reuses a node or creates one, and
  starts it. Exit records time and returns to the parent. Direct same-name
  recursion increments a recursion counter, timing only the outermost entry
  while counting every call. Reset clears statistics but retains topology.
  C++ RAII and a macro manage scope lifetime. [H, pp. 147, 150–152][H]
- **Tooling/UI:** an iterator hides node internals and exposes the selected
  node's children. Tables show percentage of parent and total, milliseconds
  per frame/call, calls per frame, and an unlogged remainder. Users navigate
  numerically through an in-game table. [H, pp. 148–152][H]
- **Era assumptions:** Pentium tick sampling converted by CPU clock rate,
  floating-point accumulated durations, one current node, and small child
  lists. The reported *Renegade* tree had over 1,000 nodes, about two children
  per node on average and 15 at worst. Those are workload observations, not
  bounds on all engines. [H, pp. 147, 150][H]

The clock explanation on p. 150 incorrectly describes the cycle counter as
incrementing for each executed instruction. This is a conceptual error, not
merely an old API: elapsed ticks, executed cycles, and retired instructions are
different quantities. The timing listing itself subtracts ticks. Clock
conversion must use the actual clock's frequency contract; see the
[modern timing guidance](https://learn.microsoft.com/en-us/windows/win32/sysinfo/acquiring-high-resolution-time-stamps).

### 4.3 Relevance classification

| Significant idea | Classification | Reason and modern interpretation |
| --- | --- | --- |
| Calling-context-sensitive attribution | **Strong candidate** | The same routine under loading and under a frame update can have different significance. Preserve context separately from site identity. |
| Hierarchical drill-down and parent/total views | **Strong candidate** | Allows a useful path from a symptom to a narrower hypothesis. This is a consumer capability, not a mandate for producer-side trees. |
| Time per call together with calls per frame | **Strong candidate** | Separates frequency changes from per-invocation cost, provided recursion and the reporting window are defined. |
| Explicit unlogged remainder | **Strong candidate** | Makes incomplete instrumentation visible; it is not proof of idle time or profiler overhead. |
| Private nodes and iterator/view interface | **Strong candidate** | Separates recording representation from UI navigation. An immutable snapshot can preserve that separation today. |
| RAII with compile-time removal | **Strong candidate** | Robust lexical pairing remains useful in exception-free C++23. Same-thread execution is an additional contract. |
| Reusing tree topology between measurement windows | **Potentially useful** | Avoids repeated discovery/allocation for an aggregate view, if growth and lifetime are bounded. |
| Flattening an inheritance layer by changing scope boundaries | **Niche** | Can answer a deliberate attribution question, but changes what the measurement means. Prefer explicit labels or derived views when both interpretations matter. |
| Collapsing direct recursion into one timed interval | **Niche** | Prevents repeated inclusive charging in a summary; loses depth and per-invocation timing. It should be a named analysis policy. |
| Child-list search and first-encounter allocation on scope entry | **Obsolete** | Historical low fan-out makes it plausible, but cost and cold-path disturbance depend on topology. It is a poor default for bounded producer recording. |
| String-literal address as universal semantic identity | **Actively undesirable** | Static lifetime does not guarantee unique or canonical spelling identity across sites/modules. Raw pointers also do not form persistent capture identifiers. |
| One shared current-node pointer | **Actively undesirable** | Concurrent execution interleaves unrelated stacks; locking cannot turn them into a meaningful single stack. |
| Running totals/reset as the only retained evidence | **Obsolete** | Good for live averages, inadequate for recovering an unexpected one-frame scheduling event. |
| Pentium-clock assumptions and unbounded single-precision totals | **Obsolete** | Clock semantics and long-run precision need explicit modern treatment; the underlying interval measurement remains valid. |

### 4.4 Hidden costs and reliability limits

The tree is already an aggregation. It retains where a sample occurred in
instrumented context, not the temporal order of all invocations. Two executions
with identical totals can have different frame deadlines and different overlap
with other threads. Resetting after noticing a hitch cannot recover its cause.

Entry cost grows with sibling count, not simply with tree depth or total node
count. Linked-node lookup adds pointer chasing and cache misses; first use may
allocate exactly when a new scene or feature first runs. The number of unique
paths can exceed the number of sites. Indirect recursion such as A→B→A is not
collapsed by the shown same-current-name check; deeper paths may create more
nodes. Keeping topology on reset therefore also retains memory.

Direct recursion's outermost elapsed time divided by all recursive calls is
not the average inclusive duration of individual recursive invocations. The
printed table can be valid only with that aggregation policy understood.
Literal merging can also make independent same-spelled scopes appear identical,
while separate copies can split a name. A stable site descriptor solves a
different problem from a human-readable label.

The published manager offers no reader/writer snapshot protocol. A later live
viewer must not traverse nodes being created or reset concurrently. Per-thread
trees would remove one shared cursor but would not solve logical task migration
or cross-thread causality. Likewise, a CPU scope around drawing measures CPU
elapsed time, including any wait; it does not measure asynchronous GPU execution.

The thin RAII class is a useful build-time pattern. Expanding it to include
tree management or viewer templates at every call site would add costs the
paper does not evaluate. Modern reuse should keep the API shape, not expose
the implementation graph through installed engine headers.

### 4.5 Modern reinterpretation

```text
Mutable runtime tree + CurrentNode + iterator
    -> preserve calling context and support drill-down
    -> context-aware event evidence with derived trees/tables in a consumer

Direct-recursion counter suppresses inner timing
    -> avoid misleading double counting in a summary
    -> retain invocation evidence; choose and label recursion aggregation in analysis
```

The likely surviving idea is the hierarchy as an investigation tool. For a
modern worker/job engine, it is one view of execution; the full dependency
structure is not a single tree, and summing workers' time does not establish
the frame's critical path.

## 5. Lung: Design and Implementation of an In-Game Memory Profiler

### 5.1 Problem and key idea

The question is which instrumented contexts are responsible for live memory
and allocation activity. A before/after byte difference around a scope fails
when memory outlives that scope or is freed elsewhere. **Associate each
allocation with its origin, and recover that origin at deallocation.** This
is a lifetime-accounting problem, not CPU timing with bytes substituted for
seconds. [L, pp. 402–403, 406–407][L]

### 5.2 Architecture

- **Concepts:** separate collection, attribution, and presentation; preserve
  allocation provenance; distinguish exclusive and inclusive counts/bytes;
  expose allocation activity per frame; recognize cross-thread frees.
- **Mechanisms:** patch x86 allocation-function prologues to jump to hooks;
  copied instructions and a jump back form a callable trampoline. Win32
  `VirtualProtect` and instruction decoding support the patcher. Manual scope
  annotations construct a calling-context tree instead of walking every native
  stack. Each allocation is prefixed with its originating node pointer and
  size, allowing free to charge the original node without an STL map. Inclusive
  statistics are computed when reporting. [L, pp. 404–407][L]
- **Threading:** Win32 TLS stores the root/current node for each thread. Each
  root has a critical section used for statistics updates by allocations and
  frees, including frees executed by another thread. L expressly recognizes
  that free cannot rely only on the freeing thread's current context.
  [L, p. 407][L]
- **Tooling/UI:** a call-tree table shows live inclusive/exclusive counts and
  bytes, allocation activity per frame, and calls per frame. Figure 4.6.1 shows
  a remote client; the text reports a client/server demo and a related CPU
  profiler on the CD. It does not specify a durable event-capture format.
  [L, pp. 402–403, 408][L]
- **Era assumptions:** scarce suitable C++ memory tools, x86 instruction
  patching, Win32 TLS/critical sections, approximately ten children per context,
  and ordinary heap allocation paths. The article acknowledges its source
  examples are x86-specific and its printed snippets are simplified.

Its “call stack” is a selected annotation tree. It is neither a complete native
stack trace nor proof of current resource ownership. A graphics asset loaded
under a loader scope may later belong to a cache or a world; allocation origin
and ownership are useful but different classifications.

### 5.3 Relevance classification

| Significant idea | Classification | Reason and modern interpretation |
| --- | --- | --- |
| Attribute free to allocation origin | **Strong candidate** | Required for accurate outstanding-allocation accounting across scopes and threads. |
| Separate collection, attribution, presentation | **Strong candidate** | These have different dependencies and failure/lifetime requirements. |
| Distinguish live bytes/count from allocation churn | **Strong candidate** | Stable live usage can hide heavy allocate/free traffic. Neither metric alone answers both questions. |
| Semantic scope attribution | **Potentially useful** | Can be cheaper and clearer than full stacks, but requires coverage discipline and correct task context. It complements allocator tags and sampled stacks. |
| Compute inclusive totals during reporting | **Strong candidate** | Avoids walking all ancestors on every allocation. Report consistency still requires a snapshot model. |
| TLS for the synchronous current context | **Strong candidate** | Partitions ordinary thread-local attribution. Fibers and migrated tasks need scheduler-aware context handling. |
| Allocation-associated provenance metadata | **Potentially useful** | Direct lookup at free can be valuable if designed with allocator alignment, lifetime, and coverage contracts. |
| Literal pointer plus integer header exactly as printed | **Actively undesirable** | Does not establish aligned payloads, full-size representation, failure handling, or safe metadata lifetime. |
| Generic hand-written binary patching as the engine integration seam | **Actively undesirable** | Makes allocator correctness depend on code relocation, executable-memory handling, and interception coverage. Explicit allocator hooks are a better comparison candidate. |
| Vetted platform interception for opaque third-party allocations | **Niche** | May be necessary for a particular diagnostic target; belongs behind a maintained platform/tool boundary, not a portable engine promise. |
| One critical section per originating thread tree | **Niche** | Simple and potentially adequate at low rates, but producer/consumer freeing patterns can contend on one origin. No general low-contention guarantee follows. |
| Low fan-out child search with raw literal identity | **Obsolete** | The same scalability and identity limitations as H apply; allocation hooks magnify their consequences. |
| Remote memory inspection | **Strong candidate** | Keeps substantial UI outside the measured process and supports headless targets; transport is still bounded work. |
| Shared CPU/memory scope syntax | **Potentially useful** | Reduces annotation burden if CPU capture and memory attribution can be independently enabled and have separate semantics. |
| Treating a common scope/tree as one runtime subsystem | **Actively undesirable** | Memory state can outlive threads and captures; CPU interval state has different completion and loss rules. |

### 5.4 Hidden costs and failure modes

**Allocation semantics and profiler effect.** Prefix bytes alter allocation size
classes, alignment, locality, and fragmentation. On a common 64-bit ABI, an
8-byte pointer plus a 4-byte integer is a 12-byte prefix; that arithmetic alone
does not preserve a 16-byte-aligned payload. Alignment padding can increase the
actual cost further. As an illustrative calculation, 16 bytes of metadata across
one million live allocations consumes 16 MB before tree storage or allocator
rounding. These are hypothetical costs, not measurements of Lung's library.

The printed malloc/free sketch does not supply a complete modern contract for
allocation failure, size-addition overflow, `free(nullptr)`, realloc success or
failure, aligned allocations, C++ allocation variants, or mixed heaps. Its
pointer store/load expressions are illustrative rather than a correct reusable
header implementation. L explicitly warns that snippets are simplified; the
uninspected CD code may handle additional cases. The architectural point is
that a profiler must preserve every supported allocator semantic.

**Coverage.** Patching one runtime's malloc does not prove coverage of other
runtimes, custom arenas, virtual-memory reservations, or GPU heaps. Intercepting
an arena's backing block says nothing about its individual suballocations.
Counting both layers without identifying them double-counts memory. Replacing
global C++ new/delete is not a universal interception mechanism either; linkage
and allocation routes matter more than source availability alone.

**Lifetime and recursion.** An allocation may outlive its originating thread,
profile reset, or profiler shutdown. A raw node pointer must remain valid until
the last associated free. Hooks that allocate nodes, log messages, or construct
report buffers can reenter the allocator. Separate accounting storage and a
deliberate recursion policy are needed; suppressing recursive events also
creates a coverage gap that must be disclosed.

**Synchronization.** TLS makes finding the current context local. It does not
make origin statistics local when many consumers free one producer's objects.
That workload can serialize on the producer root's critical section and move
cache lines between cores. A single lock level inside the profiler does not
prove freedom from deadlock involving allocator locks, logging, reporting, or
hook reentrancy. The article's broad deadlock claim is therefore insufficient.
Concurrent report traversal and node creation require a publication/snapshot
contract beyond the described statistics locks.

**Interception maintenance.** Copying instruction lengths is only part of
building a trampoline: relocated relative instructions and safe patch/unpatch
while threads execute need handling too. Executable-memory permissions are a
platform responsibility, not ordinary data-buffer management. Windows DEP
explicitly distinguishes executable from non-executable pages.
[Microsoft DEP documentation](https://learn.microsoft.com/en-us/windows/win32/memory/data-execution-prevention)
The historical patcher should not become a portability obligation for Ludus.

**Metric truthfulness.** Live totals do not preserve peak usage or lifetimes.
Allocating and freeing 1,000 objects during a frame can finish at zero live
objects; it still represents 1,000 allocations. L's promised per-frame activity
therefore needs distinct activity accounting beyond a live counter incremented
on alloc and decremented on free. The printed sketch does not fully specify
that accounting. An allocation still live at capture end is also not by itself
a leak; expected lifetime and retained ownership matter. Requested heap bytes
do not establish resident/committed memory or fragmentation.

Hooking, disassembly, platform headers, tree types, and remote UI dependencies
have integration and build costs absent from the paper's low-overhead claim.
They should not become transitive dependencies of a public CPU scope header.

### 5.5 Modern reinterpretation

```text
Patched malloc/free + embedded tree-node pointer
    -> retain provenance until the matching deallocation
    -> allocator-origin metadata or explicit allocation/free records,
       with defined lifetime, identity, ordering, and loss semantics

Manually annotated per-thread call tree
    -> understandable semantic responsibility
    -> tags/context IDs, optional stack evidence, task-aware context propagation

Remote tree client
    -> inspect live memory without a large in-game viewer
    -> memory-specific snapshots/analysis through shared presentation facilities
```

The strongest surviving idea is provenance, followed by separating live memory
from activity and acknowledging cross-thread frees. A modern author would
still need to solve those problems even with a completely different allocator
and capture mechanism.

## 6. Independent review of memory-profiler ownership

**Conceptual conclusion:** allocation correctness and basic accounting belong
with the allocator/memory subsystem. Rich memory capture and analysis can be a
separate memory-profiler module. It may share storage/transport facilities and
visualization with CPU profiling, while retaining a distinct data model and
lifecycle. A diagnostics framework can offer discovery/control without owning
the allocation hot path. This is a comparison conclusion, not a module change.

| Candidate home | Suitable responsibility | Why it is insufficient as the sole home |
| --- | --- | --- |
| General CPU profiler | Time correlation, presentation of memory-related counters, links from a hitch to allocation activity | Scope exit cannot close an allocation lifetime; CPU capture enable/disable and lossy event rules are not automatically valid for memory accounting. |
| Allocator/memory subsystem | Allocation identity, size/alignment, allocator/tag ownership, free/realloc semantics, foundational totals where required | Full call-tree UI, symbolization, capture export, and comparative analysis would burden a low-level allocation dependency. |
| Diagnostics framework | Optional control/discovery, capture metadata conventions, health reporting, shared viewer entry points | A central lock or mandatory dependency from all allocators would spread profiler failure/reentrancy risk into infrastructure. |
| Separate memory-profiler module | Optional lifetime histories, attribution aggregation, peaks/churn analysis, snapshots and external-tool adapters | Needs an explicit allocator event/metadata contract; it cannot infer missing suballocation or ownership information from CPU scopes alone. |

This separation follows L's actual problem. If thread A allocates under
`LoadMesh`, thread B frees after A exits, and CPU tracing is disabled in between,
memory attribution still needs the original allocation record. Turning off a
CPU zone stream cannot invalidate the metadata needed by free.

Sharing a clock, capture container, metadata encoding, or UI widgets does not
require sharing buffers, backpressure, or exactness guarantees. Losing one
allocation or free can leave a live-byte reconstruction permanently wrong.
A bounded memory stream therefore needs an explicit interpretation of gaps:
for example, mark affected analysis incomplete and reestablish a known snapshot,
or use a separately defined sampled mode. This review does not choose a loss
recovery design. It identifies why blindly reusing Trace's drop policy is unsafe.

The evidence supports comparing four memory questions separately:

1. **Capacity:** requested/live, reserved, committed, and resident bytes are
   different metrics. L's allocation totals do not answer all of them.
2. **Activity:** allocations/frees per interval, sizes, and short-lived churn
   require activity records/counters even when live totals remain unchanged.
3. **Lifetime and attribution:** origin, current owner/tag, thread/task, and
   capture boundary must be defined; pointer reuse must not conflate lifetimes.
4. **Allocator efficiency:** internal/external fragmentation requires capacity,
   layout, free-space, and allocator-specific evidence beyond live bytes.

Building memory profiling with the allocator remains well motivated. It also
lets FoundationBase remain independent of a higher-level diagnostics module,
consistent with [AGENTS.md](../../AGENTS.md). CPU/memory macro convenience is a
frontend choice, not evidence that the subsystems should share ownership.

## 7. Cross-article architectural analysis

### 7.1 Same questions, different solutions

| Theme | R | E | H | L | Enduring insight versus historical mechanism |
| --- | --- | --- | --- | --- | --- |
| Source-level instrumentation | Named begin/end; RAII enhancement | Registered timed handles; RAII option | RAII scope macro | Scope annotations plus allocation interception | Engine semantics need explicit attribution. The site API need not expose storage or rendering. |
| Hierarchical timing/context | Flat named array with child subtraction and indentation | Groups, without a specified dynamic call tree | Persistent calling-context tree | Annotated memory-origin tree | Group/category, site, invocation, and calling context are different dimensions. One identifier cannot safely stand for all four. |
| Frame analysis | Per-frame totals; history survives reset | Totals cleared by draw/finalization | Running totals divided by frame count; manual reset | Live memory plus per-frame activity views | Frames help interpretation, but not every quantity is frame-owned or frame-resettable. |
| Runtime visualization | Previous-frame text buffer | Console-selected colored bars | Navigable tree table | Remote memory table | Keep investigation close to the reproduction; screen location is secondary. |
| Rolling statistics | Weighted average and decaying extrema | Current totals and maximum since display | Totals/averages since reset | Current live totals and activity rates | These are distinct statistical windows. None is a general retained rolling event history. |
| Profiling storage | Two bounded global arrays | Manager-owned counters; disk summaries suggested | Persistent nodes, totals, current cursor | Per-thread nodes plus allocation headers | Storage follows the question: summaries trade temporal detail for footprint; allocation provenance must survive longer. |
| Capture architecture | Optional text dump | Filtered long-running server output | Runtime browsing/reset | Client/server demonstration | Export/display is not a specified bounded capture protocol with loss, ordering, metadata lifetime, and shutdown rules. |
| Concurrency | No concurrent model supplied | No concurrent model supplied | One current-node model shown | TLS plus origin-root locking | Local execution context matters, but cross-thread correlation/ownership must be modeled separately. |
| External tools | Explicit complement | Detailed discussion of commercial/hardware tools | Complement to finer sampling | Motivated by memory-tool scarcity | Own missing engine semantics; reassess what tools already provide. |

R and H solve nested attribution differently. R deducts child durations in a
flat name registry; H makes the parent path part of the retained structure.
H is more expressive for caller-sensitive totals, but pays discovery and tree
maintenance costs. Neither retains an event timeline. E solves a different
organization problem: an AI group remains an AI group regardless of its caller.
It should not be mistaken for a cheaper implementation of H's call tree.

H and L share a scope/tree idiom but measure different lifetimes. H can complete
a synchronous invocation when its scope ends. L must keep an origin reachable
until future frees. Their common UI shape is useful reuse evidence; their
different completion rules are stronger evidence for separate ownership.

### 7.2 What survives modern concurrency

Inclusive elapsed duration is the interval between scope entry and exit.
Exclusive duration subtracts the union of nested synchronous child intervals
on that execution context. Neither quantity necessarily represents scheduled
CPU execution time: preemption and blocking remain in elapsed intervals.
Across workers, adding elapsed durations can exceed frame wall time. Subtracting
asynchronous worker durations from a dispatching scope is not a valid general
exclusive-time calculation.

For example, two independent 4 ms worker intervals may overlap entirely inside
a 5 ms frame. Their sum is 8 ms of worker elapsed intervals, not an 8 ms frame.
If one interval includes 2 ms waiting for a lock, it is not 4 ms of CPU execution
either. Determining the critical path requires scheduling/dependency evidence
that the historical tree summaries do not provide.

The corresponding modern interpretation is per-context nesting plus distinct
job/flow relationships where needed. RAII remains valuable, but a coroutine can
suspend with an RAII object alive and resume elsewhere. Lexical scope alone
does not guarantee same-thread pairing. Memory attribution must likewise follow
the executing task when worker TLS no longer describes logical ownership.

Frame identity should describe the measured domain: simulation, render
submission, GPU execution, and presentation can overlap. Historical per-frame
tables remain useful views, but a global reset is not evidence of correct
cross-frame or asynchronous accounting. CPU timing around a graphics API call
also cannot substitute for GPU timestamps.

### 7.3 What survives modern hardware and memory budgets

Bounded preallocation remains valuable despite larger machines. Modern trace
volumes can consume the extra memory quickly. An illustrative 10 million
records/second at 24 bytes each produces 240 MB/second; a five-second retained
window is 1.2 GB before metadata and export overhead. These are arithmetic
examples, not measurements or proposed Ludus defaults.

Conversely, H's node totals can retain long-run summaries using memory tied to
distinct paths rather than event count. That remains a legitimate tradeoff for
a narrow aggregate-only diagnostic. Raw events provide more evidence but are
not inherently cheaper. Compare what is lost and the total retention cost,
including collector copies, exporter buffering, viewer memory, and thread churn.

Low fan-out and a small number of probes justified historical linear scans.
Modern large caches do not turn pointer chasing, shared writes, false sharing,
or a contended root lock into constant-cost operations. Nor does `thread_local`
alone promise a particular number of instructions on every binary/platform.
Preallocation, sharding, and compact recording are candidates to measure rather
than automatic proofs of low overhead.

### 7.4 Instrumentation and measurement policy

The most transferable performance lesson is to measure the observer. A scope
cost that is insignificant once per frame can dominate a tight loop. A constant
subtracted from each sample cannot undo changed scheduling, cache occupancy,
allocation size classes, or CPU/GPU overlap.

Evidence worth collecting later includes:

- Instrumentation compiled out, compiled in but idle, recording, recording with
  export, and recording with overlay: compare identical optimized workloads.
- Warm steady state and first use: registration, topology discovery, and
  capture start can have different costs from repeated probes.
- Several thread counts and producer/consumer patterns, including cross-thread
  frees; measure tail frame times, contention, drops, and retained bytes.
- End-to-end frame behavior alongside per-probe microbenchmarks. A quick scope
  loop cannot establish that the collector or UI is harmless.
- Header parse cost, object/binary metadata growth, and compile-out codegen
  under Ludus's pinned toolchain. None of the papers supplies those results.

These are proposed evidence checks, not implementation tasks performed here.
R and E already warn against profiler-induced distortion; modern scalability
extends that warning rather than invalidating it.

### 7.5 Modern external tools change the build-versus-buy boundary

Current primary documentation supports a narrower reason to build engine code:

| Existing capability | Consequence for this review |
| --- | --- |
| [Perfetto track events](https://perfetto.dev/docs/instrumentation/track-events) support nested slices, category selection, counters, and cross-track flows | An engine-specific tree viewer is no longer necessary just to navigate semantic timing. The engine still needs meaningful names and relationships. |
| [Tracy](https://github.com/wolfpld/tracy) covers CPU/GPU profiling, allocations, locks, and context switches | Evaluate integration before maintaining equivalent UI/capture features. This review does not establish backend cost or drop-in compatibility. |
| [Linux perf recording](https://www.man7.org/linux/man-pages/man1/perf-record.1.html) supports sampled events and call-graph capture | Sampling complements selected semantic scopes, especially for uninstrumented/library work. Symbol/unwind configuration and sampling limitations still matter. |
| [Heaptrack](https://github.com/KDE/heaptrack) records heap allocation evidence and exposes integration for custom allocators | L's scarcity-of-tools premise no longer justifies rebuilding all memory analysis. Engine-specific allocation domains/tags still require cooperation. |
| [RenderDoc](https://github.com/baldurk/renderdoc) supplies frame-based graphics debugging across modern APIs | E's obsolete driver/tool examples are not integration blueprints. Graphics inspection and representative execution timing remain different investigations. |

These tools reduce implementation scope; they do not eliminate integration,
schema, symbolization, platform, or profiling overhead. The table documents
capabilities, not a measured performance ranking or a backend selection. In
particular, a shared visual destination does not make memory and CPU records
interchangeable, and a Chrome/Perfetto export does not automatically constitute
a native Tracy integration.

## 8. Specific comparisons to make against PR #23

The following is an evidence agenda. “Baseline position” refers to the frozen
document's numbered sections, not verified engine behavior. No row authorizes
changing the design during this literature pass.

| Historical evidence | Classification of comparison idea | Baseline position | Specific question/evidence to compare |
| --- | --- | --- | --- |
| R/E/H: semantic scopes and lexical pairing | **Strong candidate**: low-friction attribution is repeatedly useful | §§3–4, 18 propose a light RAII frontend | Verify early-return pairing and true compile-out without heavy headers. Treat suspension/migration as a separate contract; §4's assertion that lexical RAII enforces same-thread execution needs qualification in future job/coroutine work. |
| R: child subtraction; H: context tree | **Strong candidate**: preserve meaning of time and caller context | §§2, 4, 6 derive hierarchy off-path | Can analysis distinguish inclusive/exclusive elapsed time, uninstrumented remainder, site totals, and path totals? Compare direct/indirect recursion and one site under multiple callers. |
| R calls/frame; H time/call and calls/frame | **Strong candidate**: distinguish amount of work from cost per unit | §§1, 11, 16 include counts and summary views | Preserve invocation counts as well as durations; define counts under recursion and incomplete traces. Do not equate scalar counters with timed scopes just because E calls both “counters.” |
| E: independent team groups | **Potentially useful**: capture selectivity needs a workload/workflow | §§14, 22 allow optional category metadata | Are capture enablement, grouping, and display filtering separate? What happens if a category changes while a scope is open? |
| R decaying extrema; E refresh-window max; H reset-window averages | **Strong candidate**: statistical semantics affect conclusions | §§11–12, 15–16 describe snapshots, moving median, and overlay | Name every window and denominator; keep smoothed UI separate from exact retained hitch evidence. Compare rare events and frames with no invocation. |
| E draw-time reset; H manual frame count | **Strong candidate**: reporting must survive headless/asynchronous use | §8 makes frames markers; §11 snapshots counters | Does a hidden/absent UI leave recording unchanged? Can worker updates overlap frame snapshots without silent loss? Compare begin-frame attribution with clipped-to-frame duration for scopes crossing boundaries. |
| E/H historical tick conversion | **Strong candidate**: clock correctness precedes speed | §5 selects steady-clock ticks and defers TSC | Verify resolution, ordering, conversion, and migration behavior on actual targets. The articles do not substantiate a nanosecond budget or per-thread correction strategy. |
| R bounded arrays versus H retained tree | **Strong candidate**: bound total memory, not only producer slots | §§3, 6, 13, 15 propose chunks/ring/capture storage | Measure cold registration, per-thread memory, collector retention, unique-path metadata, and export duplication at realistic rates. Compare aggregate retention with event retention honestly. |
| Historical summaries discard temporal evidence | **Strong candidate**: retain context before an unexpected hitch | §12 proposes pre/post-trigger capture | Verify that pre-trigger data remains available under overload and that the trigger's export does not induce another hitch. This is a modern extension, not an architecture demonstrated by these papers. |
| R flat-name merging; H/L pointer identity | **Strong candidate**: separate semantic identity from labels | §14 hashes names and checks collisions | Distinguish same spelling at different sites from the same site under different parents. Compare module unload/reload, capture metadata lifetime, and collision failure behavior. |
| R/E explicitly discuss observer overhead | **Strong candidate**: validate complete profiler cost | §§19, 25 provide estimates and benchmark plans | Measure recording/collector/export/UI together and apart, including cold paths and frame tails. Historical overhead claims do not validate the baseline's estimates. |
| L: provenance on free; explicit multicore handling | **Strong candidate**: memory semantics must survive CPU capture lifetime | §§2, 10 defer separate memory work to the allocator | Define cross-thread frees, originating-thread exit, realloc, capture start with already-live blocks, and requested versus reserved memory. One return address identifies a site, not a full calling context. |
| L: live totals differ from activity and allocator layout | **Strong candidate**: separate capacity, churn, and efficiency | §10 proposes totals, per-frame activity, and derived fragmentation | Which records support each metric? Alloc/free records alone do not establish free-space layout, committed/resident bytes, or every kind of fragmentation. |
| L: persistent allocation provenance versus CPU loss tolerance | **Strong candidate**: sharing transport must preserve domain correctness | §2 says buffers/configuration are separate; §10 proposes shared counter/capture infrastructure; §21 drops Trace events | Clarify what “shared” means before memory integration. Mark gaps/incomplete intervals visibly; fabricated scope closure at a chunk boundary is not measured duration, and missing frees must not silently become leak evidence. |
| H UI iterator; L remote client; R/E optional file output | **Potentially useful**: consumer reuse depends on tooling need | §§16–17 prefer external visualization and a small overlay | Evaluate whether current external tools cover required views and exports. Reuse metadata/presentation where useful without coupling recording to rendering or memory lifetime to CPU scopes. |

## 9. Review conclusions and adoption boundary

| Article | Key contribution | Useful concepts | Needs modernization | Do not adopt as the Ludus runtime default |
| --- | --- | --- | --- | --- |
| R | Immediate semantic feedback for the whole team | Progressive scopes, invocation counts, exclusive attribution, bounded storage, RAII | Context identity, clocks, retention, statistical labels, consumer separation | Global name scans, single-precision absolute timestamps, misleading extrema, failure on diagnostic saturation |
| E | A usable instrumentation service for independent engine teams | Group ownership, handles, optimized-build testing, explicit profiler-effect awareness | Capture/display controls, frame finalization, clock backend, concurrent invocation state | Draw-driven accounting, raw-tick frequency assumptions, borrowed percentage-overhead targets |
| H | Caller-sensitive real-time drill-down | Hierarchical views, time/call versus calls/frame, unlogged remainder, private representation | Per-context execution, recursion policy, identity, bounded growth, retained chronology | One shared current node, hot-path topology allocation/search, universal pointer identity |
| L | Allocation provenance across scope and thread boundaries | Live versus activity metrics, origin-at-free accounting, deferred inclusive aggregation, remote inspection | Allocator integration, aligned metadata, lifetime/coverage contracts, scalable updates and snapshots | Generic binary patching, unqualified low-contention/deadlock claims, merging memory lifetime with CPU Trace |

The articles justify comparing **measurement semantics and developer workflows**
against the baseline. They do not justify replacing its independent design
with historical code, and they do not establish that its present mechanisms
are already correct or fast. The most valuable follow-up evidence concerns
context/identity, exact statistical windows, incomplete captures, total observer
cost, and memory-provenance lifetime. Those are the places where the historical
reasoning still has practical force in a heavily multithreaded C++23 engine.

Only this literature-review document was added. The profiling baseline and
engine implementation were left unchanged. Validation for this pass consists
of source/page checks, baseline-section cross-checks, local link checks, and
document whitespace/structure checks. No build, sanitizer, or runtime benchmark
result is claimed for this documentation-only analysis.
