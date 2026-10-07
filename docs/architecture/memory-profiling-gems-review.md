# Memory Profiling: Game Development Reference Review

**Status:** Design evidence, reviewed 2026-10-04. The
[memory profiling architecture](memory-profiling.md) was drafted and saved
before opening the reference map. This review then refined that design; it did
not implement a profiler or measure a performance improvement.

Thanks to the authors and tool teams identified below for the ideas informing
this design. The review distinguishes adopted ideas, Ludus-specific inferences,
and rejected techniques; no chapter implementation was copied.

## Reading scope and evidence

The selection came from
[the game development article map](../../references/game-dev-gems-toc.md).
The map identifies candidates; the decisions below follow actual chapter text.
Eleven complete relevant chapters/sections and the two selected middleware
sections were read. Scanned Gems 1 and Gems 4 chapters were rendered and read
with OCR assistance. Other chapters used the local PDFs' embedded text.

Links below use **one-based PDF pages** in this repository's copies; printed
page numbers are supplied separately. The reference PDFs are local, ignored
research material, so these links require that reference collection. Graphics
Gems III is available locally now; older Ludus memory audits describe their
earlier inventory and are not a substitute for this reading.

| Source actually read | Printed pages | PDF pages | Relevant evidence |
| --- | --- | --- | --- |
| James Boer, GPG 1 §1.7, [Resource and Memory Management](../../references/Game%20Programming%20Gems%201.pdf#page=78) | 80–87 | 78–85 | Resource size, residency, eviction/recreation, admission before loading |
| Steven Ranck, GPG 1 §1.9, [Frame-Based Memory Allocation](../../references/Game%20Programming%20Gems%201.pdf#page=90) | 92–100 | 90–98 | Nested scratch scopes, retained backing, bulk retirement |
| John Olsen, GPG 1 §1.13, [Stats: Real-Time Statistics and In-Game Debugging](../../references/Game%20Programming%20Gems%201.pdf#page=113) | 115–119 | 113–117 | Small visible statistics pages; display cost and lifetime constraints |
| Peter Dalton, GPG 2 §1.10, [A Drop-in Debug Memory Manager](../../references/Game%20Programming%20Gems%202.pdf#page=63) | 66–73 | 63–70 | Sites, live metadata, allocation-family diagnostics, selected allocation/free breaks |
| Paul Glinker, GPG 4 §1.5, [Fight Memory Fragmentation with Templated Freelists](../../references/Game%20Programming%20Gems%204.pdf#page=59) | 43–49 | 59–65 | Fixed typed pool capacity, occupancy, backing and access locality |
| Dimitar Lazarov, GPG 7 §1.2, [High Performance Heap Allocator](../../references/Game%20Programming%20Gems%207.pdf#page=48) | 15–23 | 48–56 | Size classes/pages, metadata cost, debug support, concurrent allocator concerns |
| Steve Rabin, GPG 8 §4.4, [Game Optimization through the Lens of Memory and Data Access](../../references/Game%20Programming%20Gems%208.pdf#page=400) | 385–392 | 400–407 | Hardware counters, hot/cold data, locality, measurement over allocation counts alone |
| Ricky Lung, GPG 8 §4.6, [Design and Implementation of an In-Game Memory Profiler](../../references/Game%20Programming%20Gems%208.pdf#page=417) | 402–408 | 417–423 | Origin retained through free, live versus accumulated traffic, contextual attribution |
| Jason Hughes, GEG 1 chapter 1, [§1.3 Memory Management](../../references/Game%20Engine%20Gems%201.pdf#page=32) and [§1.8 Custom Profiling Tools](../../references/Game%20Engine%20Gems%201.pdf#page=37) | 4–5, 9 | 32–33, 37 | Middleware ownership, allocation adaptation, budgets, monitoring APIs; only these sections read |
| Jason Hughes, GEG 2 chapter 26, [A Highly Optimized Portable Memory Manager](../../references/Game%20Engine%20Gems%202.pdf#page=431) | 415–432 | 431–448 | Bookkeeping/rounding cost, trace replay, allocator-dependent fragmentation and page management |
| Jason Gregory, GEA 3 §10.9, [In-Game Memory Stats and Leak Detection](../../references/Game%20Engine%20Architecture%203rd%20Edition.pdf#page=634) | 615–618 | 634–637 | Subsystem budgets, min-spec limits, allocations within backing heaps, outside allocator coverage |
| Steve Hill, Graphics Gems III §II.2, [A Simple Fast Memory Allocator](../../references/Graphics%20Gems%203.pdf#page=81) | 49–50 | 81–82 | Pool reset retains storage; allocator gains depend on whole-program work |

Other mapped CPU profiling, cache replacement, stack allocation, Vulkan memory,
and general optimization chapters remain candidates for their own implementation
tasks. This review does not claim to have read the whole map, every memory
chapter, companion code, or accompanying CDs.

## Improvements to the initial design

These are Ludus design inferences from the named sources. The chapters do not
prove the proposed concurrent recording protocol or its performance.

### Keep origin, activity, and lifetime separate

Lung retains allocation origin so a later free updates the right context; his
display separates outstanding bytes/counts from accumulated allocations/frees.
Adopt those distinctions. An interval with no net live-byte change can still
have heavy allocation traffic. Build contextual summaries offline from stable
IDs rather than maintaining a mutable call tree in allocation hooks. Preserve
the allocating thread/site even if a different thread releases the block.

Gregory's leak/statistics section strengthens the coverage requirement:
allocations inside a larger backing heap need their owner's statistics, and
foreign allocations can escape engine instrumentation. The final design adds
owner-labelled checkpoints with generation and explicit expected survivors.
New, released, and surviving identities are separate report categories. Calling
a survivor a leak requires a workload-specific lifetime expectation; this is
our additional inference, not a conclusion that every live block is wrong.

### Make resource and arena observations useful

Boer's manager reasons about resident resource size and makes room before a new
load. Adopt resource residency, load/eviction/reload traffic, and temporary
storage as owner-provided metrics. Resource admission/budget policy stays with
the manager; a profiler must not evict resources or change allocation success.
His resource size is not automatically physical process residency.

Ranck's frames are nested allocation scopes, not permission to reset at every
rendered frame. Hill explicitly distinguishes reset from returning the pool's
backing to the system. The refined design reports backing, logical usage,
padding, high-water, and reset epoch separately. Retirement follows the owner's
completion proof. GPU/CPU retirement contracts are Ludus requirements, not
claims that either article solves asynchronous GPU ownership.

Glinker's fixed typed freelists motivate pool capacity, live slots, slot size,
overflow, and backing statistics. Locality depends on actual access patterns.
Expose a demonstrated pool's facts; do not implement a new generic pool merely
to make profiling possible. Do not add logical slot bytes to already counted
backing blocks.

### Improve debugging without making allocation hooks complex

Dalton's selected allocation/reallocation/free breakpoints are useful. Add a
development trigger for domain/site/size/failure/new lifetime ordinal, or release
of a selected lifetime. Matching is bounded; the break occurs after releasing
tracking locks. Reproducibility depends on allocation order, so an ordinal is
not a universal stable identity across runs.

Dalton also scans payload poison patterns to estimate usage. Do not adopt that
metric: unchanged bytes do not prove that memory was unused or unread. Requested
storage and provider-known payload/slack are observable; actual read/write use
needs separate instrumentation. Sanitizers remain the default corruption tools.

Lazarov and Hughes make bookkeeping part of the allocator cost, rather than
invisible overhead. The artifact therefore records table, definitions,
snapshots, journal, scratch, and tooling storage alongside useful payload.
Debugger stepping remains straightforward with one reference mutex and one
authoritative table. Their heap implementations do not establish that this
mutex will scale for Ludus; contention must be measured.

### Measure effects and keep the display small

Hughes discusses allocation-trace replay as a backend comparison tool. Add
replay to benchmarks, together with real scene/load/unload workloads. Rabin
connects performance to cache behavior and data layout: fewer allocation bytes
or calls alone do not demonstrate a frame-time improvement. Include latency,
frame time, metadata ratio, and available cache/page-fault evidence. Hill's
reported modest whole-program gain reinforces this distinction.

Olsen demonstrates the usefulness of a small visible statistics surface and
acknowledges its rendering cost. Permit a bounded read-only memory/health summary
through an existing console/editor later. Do not make a new display, editable
statistics framework, or static-registration scheme a prerequisite for captures.
Historical timings and platform constants from these books are not Ludus budgets.

The selected Hughes middleware sections support asking who allocates/releases
storage and what monitoring/adaptation a library provides. Use explicit library
allocation callbacks when their alignment, error, thread, and release contracts
fit Ludus. Private heaps are not required for domain attribution; callback cost
is measured rather than dismissed categorically.

## Modern primary-source cross-check

The classic articles contribute engine-specific questions. Current tools were
also checked to avoid treating a historical implementation as today's default.
These links were read on 2026-10-04; no tool was installed or benchmarked.

| Primary source | Verified feature or constraint | Ludus inference |
| --- | --- | --- |
| [Perfetto native heap profiler](https://perfetto.dev/docs/data-sources/native-heap-profiler) | Separates unreleased size/count from total allocation size/count; supports sampled native profiles and Linux host profiling; buffer exhaustion can end profiling early | Keep activity and live-set views distinct; an external investigative profile complements explicit domains; record loss rather than implying a complete run |
| [heapprofd sampling](https://perfetto.dev/docs/design-docs/heapprofd-sampling) | Random byte-oriented sampling avoids periodic allocation-pattern aliasing; estimates still have substantial possible error | Keep optional stack sampling distinct from exact block records; export its rule/probability and mark estimates; do not copy an unmeasured interval |
| [heapprofd design](https://perfetto.dev/docs/design-docs/heapprofd-design) | Uses operation sequencing to handle alloc/free ordering and moves costly unwinding work outside the application | Use a lifetime ID and authoritative operation order, not timestamp sorting; defer offline raw-stack transport until its complexity is justified |
| [Perfetto memory counters](https://perfetto.dev/docs/data-sources/memory-counters) | Process polling and kernel events are distinct sources; polling may miss short bursts | Label process metrics, timestamps, capability and cadence separately; do not claim sampled RSS is an exact allocation peak |
| [heaptrack README](https://github.com/KDE/heaptrack) | Linux allocation analysis exposes allocation stacks, temporary allocations and leak investigations; launch and attach modes have different coverage | Use external whole-process investigations for foreign allocations and comparison; do not treat a late attachment as a proven Ludus ownership baseline |
| [Tracy README](https://github.com/wolfpld/tracy) and [instrumentation header](https://raw.githubusercontent.com/wolfpld/tracy/master/public/tracy/Tracy.hpp) | Exposes memory profiling and named allocation/free instrumentation, with optional stack variants | Keep a future adapter behind the existing profiler; verify adapter loss/baseline/domain semantics before exact reports and retain the dedicated artifact when necessary |

The one-mutex reference recorder, exact-size plus optional sampled-stack split,
bounded artifacts, and provider protocol are Ludus choices. They are not a claim
that these tools use that same architecture or that Ludus already supports them.
No inspected source provides a transferable universal overhead guarantee.

## Alternatives deliberately left out

| Historical or tempting technique | Reason to exclude from the initial system |
| --- | --- |
| Global language-operator macros, binary patching, or trampoline interception | Ludus owns explicit allocation seams; external tools provide broader investigations without this engine/SDK policy |
| A mandatory pointer-plus-integer prefix and producer-maintained origin tree | ADR 0008 retains ownership in domains/owners; optional side metadata avoids normal-build per-block cost and borrowed-node lifetimes |
| Shutdown-only leak dumps through global destructor/`atexit` ordering | Owner checkpoints and explicit sealing give reviewable boundaries; process teardown is not the only expected lifetime boundary |
| Replacing malloc with historical small/medium/large heaps | A profiler needs an observable allocation boundary; new backends need representative measurements and their own decision |
| Guessing corruption/fragmentation/utilization from magic bytes or RSS differences | Those facts require sanitizer support, a known allocator layout, or a provider that measures the metric |
| An arena reset per displayed frame | CPU jobs and GPU use can outlive that marker; retained backing is not freed storage |
| A custom hierarchical in-game heap viewer | Sealed data, existing external viewers, readable reports and a small optional summary answer the initial questions with less maintenance |
| Lock-free queues, live attachment, streaming, or copied historical constants as defaults | Each adds lifetime/protocol complexity or unsupported assumptions; add it only for a measured consumer |

## Resulting architecture and remaining evidence

Retain the initial system allocator, stable origin domains, measured-cost
counters, optional fixed ledger/journal, explicit safe points, and external
presentation. The reading improved the report model and integration contracts
without adding a new allocator algorithm or runtime framework.

The final architecture incorporates each adopted refinement. Its implementation
phases still require adversarial ownership/loss/lifecycle tests and measured
normal/investigative overhead. Table capacity, capture duration, stack capability,
provider cadence and backend choice remain measurements to make during delivery.
The proposal is suitable for implementation review; it is not a claim of shipped
features, complete process coverage, or verified performance.

## Research follow-up after the initial implementation

**Status:** Deferred research shortlist, recorded 2026-10-06. Implement and
validate the initial [delivery phases](memory-profiling.md#validation-and-delivery)
first, including the provider integrations needed by demonstrated consumers.
Retain the initial correctness and performance gates during that implementation.
Then use its artifacts and workload measurements to identify improvements worth
investigating through the sources below.

Primary publication pages, abstracts, selected paper excerpts and the LLVM talk
abstract were inspected to verify this shortlist. Full-paper reviews, recordings,
implementation audits and reproductions remain future work. These candidates
are separate from the completed chapter reading above; their techniques have
not been adopted into the initial architecture or measured in Ludus.

### Conferences and journals to revisit

Prioritize [ISMM](https://www.sigplan.org/Conferences/ISMM/) for allocation and
access behavior, PLDI/OOPSLA for dynamic analysis and profiling implementation,
and ASPLOS for architecture effects and performance evaluation. USENIX OSDI/ATC
and the LLVM Developers' Meeting supply complementary systems and tooling
experience. The papers and talk below anchor those recommendations.

For journals, prioritize **Proceedings of the ACM on Programming Languages
(PACMPL)**, especially its OOPSLA issues, starting with PROMPT below. Also revisit
**ACM Transactions on Architecture and Code Optimization (TACO)** for profiling,
locality and hardware/software measurement work. David Kaeli's
[description of TACO's scope](https://www.sigarch.org/acm-taco-a-high-quality-venue-targeting-computer-architecture-and-compiler-research/)
is a venue-selection reference; individual papers still need review before any
design change.

### Candidate reading and investigation questions

Thanks to the authors below for making these research directions available.
The questions are proposed Ludus investigations, not conclusions established
by the preliminary source inspection.

| Candidate and primary source | Venue | Question for the implemented Ludus system |
| --- | --- | --- |
| Stuart Byma and James R. Larus, [Detailed Heap Profiling](https://doi.org/10.1145/3210563.3210564), 2018 ([author PDF](https://infoscience.epfl.ch/server/api/core/bitstreams/58115553-3df7-4ffd-b879-5ef3407df66a/content)) | ISMM | Which Memoro diagnostics for repeated growth, allocation churn and retained storage can our sealed allocation history support? Which require separate read/write instrumentation? |
| Thierry Treyer, [Memoro: Scaling an LLVM-based Heap Profiler](https://llvm.org/devmtg/2019-10/talk-abstracts.html), 2019 | LLVM Developers' Meeting | Where do recorder metadata, contention and data collection become impractical on large workloads, and how should we measure and bound those costs? |
| Charlie Curtsinger and Emery D. Berger, [Stabilizer: Statistically Sound Performance Evaluation](https://doi.org/10.1145/2451116.2451141), 2013 ([author PDF](https://people.cs.umass.edu/~emery/pubs/stabilizer-asplos13-draft.pdf)) | ASPLOS | How can layout effects and experimental noise confound our overhead gate, and what evidence is needed to distinguish a small regression from noise? |
| Ziyang Xu, Yebin Chon, Yian Su, Zujun Tan, Sotiris Apostolakis, Simone Campanoni and David I. August, [PROMPT: A Fast and Extensible Memory Profiling Framework](https://research.google/pubs/prompt-a-fast-and-extensible-memory-profiling-framework/), 2024 ([DOI](https://doi.org/10.1145/3649827)) | PACMPL 8, OOPSLA1, article 110 | Can we reduce profiling work to the evidence a report needs while preserving our coverage, ordering, baseline and loss contracts? |
| Gene Novark, Emery D. Berger and Benjamin G. Zorn, [Efficiently and Precisely Locating Memory Leaks and Bloat](https://www.microsoft.com/en-us/research/publication/efficiently-precisely-locating-memory-leaks-bloat/), 2009 | PLDI | What additional evidence would improve survivor diagnostics, and which assumptions separate sampled staleness from our explicit owner-lifetime expectations? |
| Emery D. Berger, Sam Stern and Juan Altmayer Pizzorno, [Triangulating Python Performance Issues with Scalene](https://www.usenix.org/conference/osdi23/presentation/berger), 2023 | USENIX OSDI | Can sampling and copy-volume attribution help explain costly buffer growth or CPU/GPU transfers without weakening exact covered allocation accounting? |
| Renaud Lachaize, Baptiste Lepers and Vivien Quéma, [MemProf: A Memory Profiler for NUMA Multicore Systems](https://www.usenix.org/conference/atc12/technical-sessions/presentation/lachaize), 2012 | USENIX ATC | When measurements identify a locality problem, would correlating threads, objects and memory accesses explain performance beyond allocation counts? |

Start with Detailed Heap Profiling, the Memoro scaling talk, Stabilizer and
PROMPT, then select the other sources according to observed problems. Memoro's
access measurements require additional instrumentation; Scalene depends on
Python/runtime integration; Hound uses specialized heap organization; MemProf
targets NUMA access behavior. Evaluate those assumptions against Ludus before
transferring an algorithm or an overhead claim.

### Evidence required for a follow-up change

1. Identify a concrete report limitation or measured workload problem in the
   initial implementation, with a retained capture or reproducible benchmark.
2. Read the relevant full paper or watch the talk; record the sections consulted,
   assumptions, adopted ideas and departures in this review.
3. Compare a bounded experiment against the initial implementation using the same
   representative workloads. Report accuracy, loss, coverage, tooling bytes,
   median/tail latency and frame time as applicable.
4. Update the architecture only when the experiment justifies a change, preserve
   the explicit ownership and failure contracts, and credit adopted sources near
   the affected implementation.

Keep this follow-up evidence in the canonical documents. Research frameworks,
custom heap organization, access instrumentation, sampled stacks and new viewers
remain separate decisions requiring a demonstrated consumer and measurements.
