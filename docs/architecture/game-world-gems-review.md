# Game world: Gems article review

Status: Research supporting the proposed [game world architecture](game-world.md).
Reviewed October 2, 2026. This review changes the proposal; it does not implement
an entity system, timer service, or job scheduler.

## Scope and source access

Selection started with `references/game-dev-gems-toc.md`. Twelve chapters across
seven local Game Programming Gems and Game Engine Gems volumes were selected for
level construction, entity composition/lifetime, update cadence, waiting AI, and
CPU concurrency. The listed chapter pages were read from the PDFs. Text extraction
was used for searchable volumes; scanned chapters in Programming Gems 3, 4, and 6
were read through page OCR with visual checks of headings and relevant diagrams.
The review is based on chapter content, not recommendations inferred from titles.

The catalog and books are local reference material and may be absent from another
checkout. Bibliographic titles, authors, chapter numbers, and page ranges below
identify the evidence independently of those files. PDF page numbers are one-based
viewer pages in these specific local copies; printed page numbers identify the
book's own pagination. Source links are optional reading aids. The architecture
contracts remain understandable without access to the books.

These are historical design articles. Their measurements, platform assumptions,
sample APIs, and synchronization code are not evidence of current performance or
portable C++23 correctness. No sample implementation or CD-ROM project was built
or adopted. The adaptations below are Ludus judgments made under its current
ownership, browser, allocation, and exception-free constraints.

## Chapters read

| Key | Volume and chapter | Author | Printed pages | Local PDF pages |
| --- | --- | --- | --- | --- |
| F | Game Programming Gems 2, 1.8, A Game Entity Factory | François Dominic Laramée | 51-61 | [48-58](../../references/Game%20Programming%20Gems%202.pdf#page=48) |
| M1 | Game Programming Gems 2, 3.2, Micro-Threads for Game Object AI | Bruce Dawson | 258-264 | [247-253](../../references/Game%20Programming%20Gems%202.pdf#page=247) |
| M2 | Game Programming Gems 2, 3.3, Managing AI with Micro-Threads | Simon Carter | 265-272 | [254-261](../../references/Game%20Programming%20Gems%202.pdf#page=254) |
| S1 | Game Programming Gems 3, 1.1, Scheduling Game Events | Michael Harvey and Carl S. Marshall | 5-14 | [7-16](../../references/Game%20Programming%20Gems%203.pdf#page=7) |
| C1 | Game Programming Gems 3, 1.2, An Object-Composition Game Framework | Scott Patterson | 15-25 | [17-27](../../references/Game%20Programming%20Gems%203.pdf#page=17) |
| E | Game Programming Gems 4, 1.8, A System for Managing Game Entities | Matthew Harmon | 69-83 | [83-97](../../references/Game%20Programming%20Gems%204.pdf#page=83) |
| C2 | Game Programming Gems 6, 4.6, Game Object Component System | Chris Stoy | 393-403 | [371-381](../../references/Game%20Programming%20Gems%206.pdf#page=371) |
| O | Game Engine Gems 1, 21, Multithreaded Object Models | Jon Parise | 371-379 | [399-407](../../references/Game%20Engine%20Gems%201.pdf#page=399) |
| P | Game Engine Gems 1, 22, Holistic Task Parallelism for Common Game Architecture Patterns | Brad Werth | 381-390 | [409-418](../../references/Game%20Engine%20Gems%201.pdf#page=409) |
| S2 | Game Engine Gems 1, 25, A Basic Scheduler | John Bolton | 409-414 | [437-442](../../references/Game%20Engine%20Gems%201.pdf#page=437) |
| Q | Game Engine Gems 2, 29, Thread Communication Techniques | Julien Hamaide | 459-467 | [475-483](../../references/Game%20Engine%20Gems%202.pdf#page=475) |
| J | Game Programming Gems 7, 1.9, Multithread Job and Dependency System | Julien Hamaide | 87-96 | [120-129](../../references/Game%20Programming%20Gems%207.pdf#page=120) |

## Composition and level construction

**F: A Game Entity Factory.** The chapter separates shared media/configuration,
instance behavior/state, and the identity an object presents to interactions.
This improves the proposal in two ways: shared immutable definitions should not
duplicate current health/action state, and changing appearance should not decide
gameplay eligibility. Ludus expresses interactions through typed capabilities
and explicit state, and construction through game-owned recipes. Its exported
class mechanism and behavioral inheritance hierarchy are not required. Shared
definitions initially live in code or flat tables; a scripting factory is deferred.

**C2: Game Object Component System.** The chapter moves distinct functionality
out of an expanding object hierarchy and separates component templates from
changing component instances. This supports the selected composition model and
adds a concrete definition/instance ownership contract. Its component families,
virtual methods, string IDs, shared pointers, and global template managers are
alternative implementation choices, not prerequisites. Ludus keeps typed pools,
explicit system calls, and complete recipe bundles. Composition is selected for
clear ownership and reuse; these chapters do not establish that sparse pools
outperform every other storage scheme.

**C1: An Object-Composition Game Framework.** The useful distinction is between
application modes, work, and audiovisual/logic layers with explicit ownership.
Figure 1.2.1 shows how a menu can coexist with a game's presentation while changing
which logic receives input. The proposal now states Playing/Paused/Faulted tick
policy and treats loading as an independent transition. The initial game does not
need the article's full system/task factory or general layer framework. Ordinary
session state and named calls provide the needed mode behavior.

**E: A System for Managing Game Entities.** Startup/shutdown, post-update work,
death notifications, and debug drawing are useful lifecycle distinctions. The
proposal now separates prepared/active/pending-destroy/absent visibility, cleanup
from gameplay consequences, and presentation effects from gameplay entity lifetime.
All-entity update followed by post-update work reinforces explicit phases. Ludus
keeps typed queues and handle validation rather than a universal entity tree or
integer message protocol; the sample's pointer-to-integer payload casts must not
be copied. A notification cannot replace stale-handle checks on delayed work.

Applied to [level definitions/loading](level-data.md#definitions-and-ownership),
[entity lifecycle](entity-world.md#entity-lifecycle-and-presentation-tails), and
[session policy](frame-update.md#time-and-frame-ownership).

## Waiting and update cadence

**S1: Scheduling Game Events.** The chapter treats time and delivery ordering as
explicit parts of scheduling, including events at different update frequencies.
Its scheduling abstraction is useful, but unrestricted fine-grained scheduling
would obscure this game's phase order. Ludus keeps the fixed tick and adds tick
deadlines, stable due-event ordering, a bounded due-set snapshot, and next-tick
deferral for timers created during dispatch. The typed timer queue is conditional
on actual gameplay needs; most cooldowns remain component fields.

**S2: A Basic Scheduler.** The sample runs tasks cooperatively in one context and
separates discovering due work from running and removing tasks. This strengthens
the case for owner-thread timers without fibers or worker threads. A linear scan
is acceptable for a small timer set. Ludus replaces elapsed-frame timers with tick
deadlines for authoritative gameplay and defines cancellation/overflow explicitly.
It does not adopt unspecified execution order as a gameplay tie policy.

**M1: Micro-Threads for Game Object AI.** The janitor example demonstrates why a
multi-step action can become harder to read when every continuation is manually
spread across per-frame branches. This is a sequencing/readability issue even on
one CPU thread. Ludus initially uses named action phases and inspectable state;
long scripted actions may justify coroutines later. The article's stack/context
switching implementation is not a portable C++23 engine foundation and does not
provide CPU parallelism simply by giving each entity a micro-thread.

**M2: Managing AI with Micro-Threads.** The chapter adds management, interruption,
and reference/lifetime issues to cooperative AI. Ludus makes action cancellation,
target revalidation, pending request IDs/revisions, and wake ticks explicit. AI
decisions may use a fixed tick cadence while movement and urgent checks continue
each tick. Platform fiber/context APIs and exception-based termination cannot be
copied into Ludus's normal engine path. A suspended behavior never borrows component
storage, regardless of how its control flow is expressed.

Applied to [timers and cadence](frame-update.md#timers-and-decision-cadence) and
[inspectable actions](entity-world.md#inspectable-waiting-behavior). The exact timer
ordering, failure, and replay policies are Ludus adaptations, not guarantees made
by the historical samples.

## Parallel execution and communication

**O: Multithreaded Object Models.** The four approaches expose the cost of sharing
mutable object state: locks, owner-thread messages, per-thread contexts, or buffered
state. A queue alone does not protect readers that still access changing objects.
This reinforces main-thread world ownership, immutable job inputs, and extracted
render values. Buffered state introduces lifetime, memory, and presentation latency
costs. The proposal now also identifies discrete effect records by world/tick/event
sequence and requires future delayed snapshots to preserve event delivery. It does
not add multiple world buffers or a render thread to the first implementation.

**P: Holistic Task Parallelism for Common Game Architecture Patterns.** Tasks
are run-to-completion units, and a computation that waits on another computation
can be split into dependent continuations. Independent range work and iterative
chunks offer growth paths without suspended worker stacks. Ludus now states these
future job contracts, retains a serial implementation, and measures dispatch cost
before parallelizing. The historical TBB examples are not a dependency selection;
task granularity and useful speedup require profiling this game on its targets.

**Q: Thread Communication Techniques.** Known producer/consumer roles, bounded
queues, per-thread aggregation, and ownership checks can simplify communication.
Ludus starts with a short mutex-protected result transfer and reserves completion
capacity at request admission. Specialized queues require measured need. The
chapter's `volatile` index/barrier examples must not be transcribed as portable
C++ synchronization: unsynchronized conflicting non-atomic accesses constitute
a data race, and `volatile` does not establish the needed happens-before relation.
Use standard locks or a reviewed atomic publication protocol under the
[C++ memory model](https://eel.is/c++draft/intro.races) and
[atomic ordering rules](https://eel.is/c++draft/atomics.order). Aggregating queues
also does not make completion arrival order deterministic.

**J: Multithread Job and Dependency System.** Dependency edges/counters and groups
give ready work a concrete ordering mechanism; recycled entry IDs need versioned
identity. Ludus adds conditions for any future graph: validate/freeze edges, reject
cycles, retain inputs through all readers, and propagate failure/cancellation
explicitly. The sample's convention that an absent dependency is met is inadequate
for fallible or cancelled work. A scheduler thread, general waitable groups, and
OS preemption/fibers remain unselected. These mechanisms address overlapping CPU
work, not ordinary entity cooldowns or action sequencing.

Applied to [background work and eventual jobs](frame-update.md#background-work-and-eventual-jobs),
[presentation delivery](frame-update.md#events-and-same-tick-consequences), and
[debugging](game-world.md#debugging-contract).

## Resulting direction

The initial design remains a game-owned world, typed component pools, fixed ticks,
explicit phases, and main-thread rendering. The review improves its contracts
rather than requiring a larger framework. Deadline fields, action phases, and
definition sharing are ordinary data/functions. A timer queue, coroutines, worker
pool, task graph, and buffered render thread are separate additions with stated
triggers and lifetime costs.

Implementation verification now includes independent mutable instances under a
shared definition, candidate activation without premature effects, stale action
targets, deterministic timer/cadence behavior, discarded effects from a failed
tick, detached effect lifetimes, and completion admission/reclamation. Those checks
belong to future implementation slices; this review has not run nonexistent engine
features or produced performance measurements.
