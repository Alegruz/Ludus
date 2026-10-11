# Gameplay foundation Gems review

Status: research and applied refinements for the proposed
[gameplay foundation architecture](gameplay-foundations.md), October 11, 2026.

## Scope and source access

The initial design extended Ludus's existing world/storage/tick contracts with
static cells, agent actions, addressed messages, streaming and a session FSM.
It was written before this chapter selection. Selection then used the local
[game-dev-gems-toc.md](../../references/game-dev-gems-toc.md), concentrating on
gaps beyond the earlier [world review](game-world-gems-review.md) and
[scripting review](scripting-gems-review.md).

Selected passages from eleven chapters were consulted directly in the local PDFs.
The table records article bounds and the passages relevant to this design; it
does not claim an exhaustive reading of every listed article or all 86 books.
Extractable copies were read as text. The scanned Gems 3 passages were read with
page OCR, with rendered opening-page checks for titles/authors and printed
pagination. No historical source code was copied into Ludus.

Printed book pages and one-based PDF pages differ. In particular, Gems 3's
chapter 3.3 opens on printed page 256 at PDF page 250; chapter 3.5 opens on
printed page 285 at PDF page 279. The FSM title below corrects the index's
bookmark spelling using the actual heading. Local reference files are ignored
reference material and may be absent in another checkout.

## Chapters and passages consulted

| Key | Exact title and author | Publication and printed article pages | Local PDF pages | Passages used |
| --- | --- | --- | --- | --- |
| D | **High-Performance Programming with Data-Oriented Design** — Noel Llopis | Game Engine Gems 2, chapter 15, pp. 251-261 | [267-277](../../references/Game%20Engine%20Gems%202.pdf#page=267) | Principles; input/output layout; conditional execution; occasional cross-object access, pp. 253-260. |
| A | **Strategies for Optimizing AI** — Steve Rabin | Game Programming Gems 2, 3.1, pp. 251-257 | [240-246](../../references/Game%20Programming%20Gems%202.pdf#page=240) | Event-driven work, shared results, cooperation, incremental bookkeeping and emergent-behavior limits. |
| P | **Designing a Realistic and Unified Agent-Sensing Model** — Steve Rabin and Michael Delp | Game Programming Gems 7, 3.2, pp. 217-228 | [250-261](../../references/Game%20Programming%20Gems%207.pdf#page=250) | Certainty, sense fusion and retained subject memory, especially pp. 226-228. |
| L | **AI Level of Detail for Really Large Worlds** — Cyril Brom, Tomáš Poch and Ondřej Šerý | Game Programming Gems 8, 3.1, pp. 213-231 | [228-246](../../references/Game%20Programming%20Gems%208.pdf#page=228) | Task expansion/compression, partial effects, retained state, relevance propagation and hysteresis, pp. 220-230. |
| M | **Deferred Function Call Invocation System** — Mark Jawad | Game Programming Gems 7, 1.8, pp. 81-85 | [114-119](../../references/Game%20Programming%20Gems%207.pdf#page=114) | Callback timing, indirect argument ownership and sample ABI/thread-safety limits. PDF 119 is a blank page. |
| R | **Creating a Multi-Threaded Actor-Based Architecture Using Intel® Threading Building Blocks** — Robert Jay Gould | Game Programming Gems 8, 4.13, pp. 473-484 | [488-499](../../references/Game%20Programming%20Gems%208.pdf#page=488) | Actor/message ownership, queue role, state queries/promises, sequence and epochs, pp. 477-483. |
| S | **Introduction to 3D Streaming Technology in Massively Multiplayer Online Games** — Kevin Kaichuan He | Game Programming Gems 8, 5.4, pp. 514-538 | [529-553](../../references/Game%20Programming%20Gems%208.pdf#page=529) | Source abstraction, asynchronous transport, priority/dependencies, predictive loading and cancellation, pp. 525-536. |
| C | **Pointer Patching Assets** — Jason Hughes | Game Engine Gems 2, chapter 20, pp. 345-357 | [361-373](../../references/Game%20Engine%20Gems%202.pdf#page=361) | Offline coherent blocks, symbolic dependencies, offsets, alignment and inspection, pp. 347-351. |
| O | **The Game State Observer Pattern** — Ron Barbosa | Game Engine Gems 1, chapter 26, pp. 415-431 | [443-460](../../references/Game%20Engine%20Gems%201.pdf#page=443) | State-driven input/render policy, subscriptions and notifications. PDF 460 is a blank page. |
| F | **Function Pointer-Based, Embedded Finite-State Machines** — Charles Farris | Game Programming Gems 3, 3.3, pp. 256-267 | [250-261](../../references/Game%20Programming%20Gems%203.pdf#page=250) | Begin/update/end hooks, deferred next-state request and implementation cost, pp. 260-264. |
| T | **An Extensible Trigger System for AI Agents, Objects, and Quests** — Steve Rabin | Game Programming Gems 3, 3.5, pp. 285-293 | [279-287](../../references/Game%20Programming%20Gems%203.pdf#page=279) | Object-owned conditions/responses; Boolean evaluation; event/poll combination; latches, rearm, counters and culling limits. |

## Changes incorporated into the design

The following changes refine the initial proposal. The referenced passages
motivate the issues; exact identity, phase, schema, failure and budget policies
are Ludus decisions.

| Initial design | Applied refinement | Source |
| --- | --- | --- |
| Sparse pools with a dense hot-population extension | Add compact conditional work lists; choose AoS/SoA by kernel access and count gather/scatter cost. | D |
| Agent cadence and wake signals | Add revision-keyed shared environmental results and explicit squad coordination. | A |
| Sensing snapshot and checked targets | Add bounded knowledge records, last-known location/tick, provenance, thresholds and expiry. | P |
| Explicit simulation tiers and promotion | Add action compression/expansion, coarse-completion revocation, conserved outcomes and bounded consistency groups. | L |
| Typed messages and owned subscriptions | Add minimal external callback capture, dispatch membership semantics and correlation-token results without blocking object promises. | M, R, O |
| Spatial streaming sources and stable priority | Add time-to-need slack, end-to-end latency estimates, reprioritization and residency reasons. | S |
| Validated cooked cells | Add coherent offline preparation, packed-section validation and source-to-package inspection; reject native memory-image loading. | C |
| Session transition table and bounded hooks | Add prepared transition plans, a flow-policy snapshot and initial-state notifications. | O, F |
| Behavior/operation integration | Add focused trigger semantics: live state versus latches, counted facts before Boolean evaluation, command-outcome-based firing and rearm. | T |

## Storage and data transformation

Llopis frames systems as transformations of explicit input/output sets. His
conditional navigation example shows why skipping randomly through a large
component collection can lose the benefit of dense access. It also acknowledges
that occasional cross-object reads can be a sensible cost.

The design therefore collects compact query/work records, keeps their owner and
revision identity, and measures preparation plus result application. Hot/cold and
AoS/SoA choices follow actual access patterns. Batched work is a suitable future
optimization behind the existing handles, rather than grounds to replace every
store with archetypes. Code readability and reserved memory remain part of the
decision. The chapter's hardware figures do not predict Ludus's current machines.

## Agent work and believable detail

Rabin's optimization chapter argues for reporting facts once, sharing repeated
calculations and maintaining useful information incrementally. This strengthens
the wake/indexed-query path. Shared paths or perception caches need revision,
capability and freshness keys; that safety policy is an adaptation. A squad owner
may assign roles while individuals execute their actions. Emergent systems remain
appropriate for optional background behavior only when the game can test their
outcomes and protect critical objectives.

Rabin and Delp combine evidence from several senses and retain identity, timestamp
and last known location. The design adds bounded knowledge records and exposes
them to the debugger. It uses authored confidence/decay/acquisition policy, rather
than copying the proposed percentages or treating additive scores as independent
probability. Unlike the chapter's simple increase-until-purge memory rule, Ludus
can support explicit decay when the gameplay needs it. Sensing does not grant
behaviors direct omniscient world access.

Brom, Poch and Šerý make simulation detail a behavioral abstraction, not only a
frequency setting. Their expansion example removes an old coarse completion,
accounts for partial progress and starts a detailed task. Compression must stop
related fine tasks consistently; lost detail requires an authored reconstruction.
Their persistent broken-window and moved-object examples also make clear why
residency changes cannot reset meaningful world state.

The revised design specifies action conversion and conservation tests for
inventory, unique ownership, deaths and objectives. It bounds consistency groups
and detail propagation instead of automatically applying the chapter's spatial
hierarchy to all worlds. Decorative positions can be reconstructed; critical
facts cannot be invented or duplicated. Full and Dormant remain the initial
policies until a game supplies useful compression rules. Automatic conversion
of arbitrary physics or scripts is deferred.

## Messages callbacks and owner boundaries

Jawad separates asynchronous notification from later work and emphasizes storing
indirect arguments beyond the callback's stack lifetime. Ludus applies that
lesson to owned typed completion records and short callbacks. The sample's
assembly caller, general-purpose-register restrictions, raw function addresses
and explicitly non-thread-safe list are unsuitable defaults for C++23, browser
targets and native module reload.

Gould's actor chapter usefully exposes the cost of querying another mutable
owner and the need to specify message order. The design keeps declared phase
queries synchronous and uses correlation tokens for genuine cross-owner work.
It does not introduce a queue/task/VM per gameplay entity, TBB dependency, blocking
promise, or arbitrary actor proxy. Its queues retain declared producer/consumer
roles. Gameplay specifies total reduction order or simultaneous resolution;
the chapter's suggestion that some damage/heal ordering can be ignored is not
an adequate general policy for authoritative outcomes.

Barbosa's subscription mechanics expose another necessary contract: a listener
can attach after the state changed or disappear while a batch is being dispatched.
The design adds initial snapshots, captured subscription tokens, immediate
invalidation of removed listeners and next-capture admission for new ones.
Those lifetime semantics are Ludus refinements, rather than behavior guaranteed
by the historical observer sample.

## Streaming and cooked data

He's streaming chapter combines source-independent asynchronous reads with
priority queues and predicted travel time. It emphasizes dependent content,
cancelling irrelevant requests and shifting their priority after a turn. The
design adds a time-to-need slack estimate and tracks all residency reasons.
The estimate covers decode/preparation/publication as well as transport and must
be calibrated from the actual pipeline. It does not copy the demo's forward-motion
assumption, bandwidth results, camera-only priority or unguarded unload policy.
Gameplay availability still requires collision/navigation and explicit entry
gating, even if some render detail is optional.

Hughes moves many tiny runtime constructions and dependency loads into coherent
offline blocks, retaining useful alignment and introspection metadata. Ludus adopts
that preprocessing boundary and explicit symbolic references. A portable package
has validated fixed-width fields, offsets and byte order. It does not deserialize
native class layouts, restore vtables or assume a mapped buffer already contains
valid C++ objects. Offset loading, compression and eventual mapping remain
individually measured extensions; the historical pointer-patching sample is not
imported.

## Flow and trigger semantics

Barbosa gives a practical reason to centralize state-driven input/render behavior:
scattered pause, menu and game-over checks become difficult to maintain. Ludus
keeps a game-owned flow-policy snapshot that core adapters apply at a boundary,
then publishes notifications to scoped optional observers. It avoids the sample's
global singleton, observer access to arbitrary mutable subjects and synchronous
transition cascades.

Farris provides begin/update/end state hooks and defers a requested transition
until the next update. This supports explicit cleanup and bounded transition
processing. Ludus uses ordinary enum IDs and shared immutable descriptors or
functions; it does not require per-instance state objects, game-object inheritance
or stored member-function pointer tables. No claim that indirect calls outperform
modern switch dispatch is justified without a benchmark. Transition preflight,
hierarchical ancestor ordering and failure rules are additional Ludus policies.

Rabin's trigger chapter supplies a small useful authoring vocabulary: scoped
conditions and responses, Boolean composition, event-driven and polled checks,
remembered events, one-shot/repeating modes and counters. Two details need care
when adapting it. A remembered visit is different from current spatial occupancy.
Also, the sample combines event processing with Boolean short-circuit: skipping
a branch can skip an event-state update. Ludus first updates latches/counters,
then evaluates pure conditions. Required counts cannot be coalesced.

A trigger firing a deferred action waits for the application outcome; command
admission alone cannot mark a key spent or a quest complete. Flags use generated
typed schema IDs rather than runtime string insertion. Spatial/event subscriptions
avoid scanning every trigger. This focused front end shares the existing behavior
operation catalog and persistence rules; it does not justify a second scripting
language or assume that constrained authoring is a security sandbox.

## Modern cross checks and implementation attribution

Primary documentation consulted for contemporary comparisons is linked in the
architecture: Epic's MassEntity, MassGameplay, StateTree and World Partition.
They support particular batch/LOD/state/streaming concepts, not the performance
of Ludus or universal optimality of this whole design. Unity's
[command-buffer playback documentation](https://docs.unity3d.com/Packages/com.unity.entities@1.4/manual/systems-entity-command-buffer-playback.html)
also illustrates using producer-assigned sort keys rather than worker arrival
order. This cross-check reinforces the existing stable-merge contract; it adds
no Unity dependency.

The review changes architecture only. It establishes neither a scripting runtime
nor streaming/agent benchmark capacity. The staged acceptance requirements in
the architecture must precede implementation claims.

When implementing an informed section, add a concise thanks/reference comment
at its relevant code boundary naming the author, exact title, volume/chapter and
printed pages, with a link to this review or the primary source. State the idea
adapted and any important departure. Preserve licenses and existing attribution.
A document bibliography supplements those comments rather than replacing them.
