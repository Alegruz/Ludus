# Resource manager reference review

Status: Literature review supporting the proposed
[resource manager architecture](resource-management.md). Reviewed on 2026-10-04.
The architecture is a Ludus engineering recommendation; historical examples and
current vendor documentation do not establish Ludus performance measurements.

The initial architecture was written before reading the articles discovered in
`references/game-dev-gems-toc.md`. The index covers engine books as well as Gems
volumes. Relevant chapter contents were read, rather
than treating their titles as evidence. The resulting changes strengthen the
design's cache, cooking, streaming, and authoring contracts while retaining its
small coordinator and subsystem-owned preparation/retirement.

## Design before the article review

The initial document selected these contracts:

1. Separate durable asset keys, logical runtime handles, per-caller requests,
   immutable resident versions, and owned leases.
2. Preserve Content version 1 and AudioContent; extend coordination through
   explicit typed adapters instead of making a universal resource base class.
3. Give one control owner structural mutation; use worker-owned inputs/results
   and nonblocking polling at publication boundaries.
4. Deduplicate equal loading work while keeping caller cancellation independent.
   Gate delayed completion with owner, generation, revision and subsystem epoch.
5. Load a required acyclic dependency closure, pin exact dependency versions,
   stage replacements privately, and preserve active content on failure.
6. Account for all memory domains and old/new replacement overlap; never reclaim
   submitted work from a refcount or fixed frame delay alone.
7. Start with a bounded idle cache and loose files; add cooked artifacts and
   independently addressed packs when their consumers exist.
8. Keep source provenance, structured errors, lifetime blockers, and stage traces
   inspectable. Validate contracts before optimizing from measured workloads.

The initial cache policy was intentionally general, and cooking/streaming policy
was less specific. The review made the refinements below concrete. Lifetime,
concurrency, failure, and browser rules beyond the historical samples remain
explicit Ludus adaptations.

## Changes resulting from the review

| Concern | Baseline | Final refinement |
| --- | --- | --- |
| Handle mechanics | Typed generational logical identity | Separate small registry mechanics from domain policy; consume exposed reservations and retain release validation. Bilas. |
| Access and residency | Explicit borrow and lease | Borrow never loads; actual peak reservations precede dispatch and delayed free remains charged. Boer. |
| Optional presentation | Named fallback policy | Immutable type-correct fallbacks with visible failure and coverage/layout checks. Llopis. |
| Cache selection | Simple idle eviction | LRU baseline plus probationary prefetch; record recent use, reload cost, reclaimable domain bytes and churn for an optional measured alternative. McAnlis. |
| Package layout | Loose files then packs | Collect access traces, repack co-used data, compare against the same baseline, and separate storage/compression/residency granularity. Koenig and Hughes. |
| Editing iteration | Private replacement candidates | Tools-side conversion, coalesced file events, desired-revision ordering, source provenance and per-asset preview overrides. Llopis/Nicholson and Arnaud. |
| Dependency scheduling | Required DAG | Compact adjacency/reverse edges, pending counts, runnable continuations and explicit failure propagation; workers never wait on dependencies. Hamaide. |
| Cooked representation | Versioned binary payload | Checked offset-based immutable views and artifact dump tools; exclude native C++ object dumps, pointer/vtable patching. Olsen and Hughes. |
| Streaming policy | Game-owned prefetch | Offline occupancy reports, end-to-end latency-based lead distance, boundary hysteresis and separate audio refill capacity. Franco. |
| Shared level transitions | Scoped leases | Admit incoming shared holds before releasing outgoing ones; count physical backing storage and exact version closure. Gregory and current Addressables documentation. |

The [final architecture](resource-management.md) contains these policies, their
failure/lifetime consequences, and implementation exit conditions.

## Evidence and reading scope

Thanks to Scott Bilas, James Boer, Noel Llopis, Colt “MainRoach” McAnlis,
David L. Koenig, Charles Nicholson, Julien Hamaide, Rémi Arnaud, John Olsen,
Jason Hughes, Simon Franco, and Jason Gregory for the ideas examined below,
and to Epic, Unity, Godot, and Microsoft for the consulted primary documentation.
Each section identifies the exact work and adopted or rejected ideas. These are
design inspirations and adaptations; no source implementation was copied.

The TOC and PDF volumes are local reference material, not distributed with the
repository. Printed pagination identifies passages in independently obtained
copies; local PDF positions document the copies used for this review rather than
linking to unavailable repository files.

Read eleven Gems chapters or specified chapter sections from six local PDF
volumes, plus selected resource-manager sections in Gregory. Text-native chapters
used PDF extraction. Gems 1, 4, and 6 were scanned and used OCR. Opening pages and
the Gems 6 contents page were visually checked for title, author and pagination.
One-based file-page numbers below are independent of printed book-page numbers.
No companion CD samples, historical external code archives, or performance
experiments were executed. Summaries are original paraphrases; no book code was
copied into the architecture.

Gems 1 lacks its printed contents pages, and Gems 6's TOC omits article page links.
Their ranges were located and checked against the scanned chapter pages. In the
local Gems 6 copy, §1.9 begins on PDF page 106, not 107; §1.10 begins on PDF page
112, not 113. Those offsets are verified individually, rather than assuming one
page-number offset applies throughout a volume.

## Typed handles

Scott Bilas, §1.6, “A Generic Handle-Based Resource Manager,”
*Game Programming Gems 1*, printed pp. 68–79;
local PDF pp. 66–77.
Read the complete article and listings.

The article puts an indexed store, validity numbers and a free list beneath a
typed domain manager. A tag distinguishes handle types without becoming a runtime
object. The higher-level manager owns file/texture policy, while the generic
mechanism owns identity/storage bookkeeping. The sample discusses name indexes,
sharing and optional refcounts.

**Adopt:** Small shared mechanics with typed domain adapters and explicit
initialization/release. Make logical, operation, request and lease records distinct
so one generic handle cannot obscure their different lifetimes.

**Exclude:** Wrapping 16-bit validity counters, native C++ bitfield encodings,
optional release-build validation, singleton lookup in `operator->`, runtime handles
in durable saves, and the historical assumption that copying `std::string` is
reference-counted and nearly free. The sample's allocation and pointer-return
behavior is not a concurrent lease or recoverable-OOM implementation.

Applied in [identity and lifetime](resource-management.md#identity-and-lifetime)
and [public acquisition](resource-management.md#public-acquisition-contract).

## Residency and memory reservations

James Boer, §1.7, “Resource and Memory Management,” *Game Programming Gems 1*,
printed pp. 80–87;
local PDF pp. 78–85.
Read the complete article, including the reservation and pointer-lifetime caveats.

The manager retains resource metadata while disposing/recreating payloads. Locks
prevent eviction, and priority, access time and size influence victim selection.
The article notes that inserting first can exceed a fixed memory limit; its
reservation method frees capacity before creation. It also warns that a pointer
from ordinary access can become invalid on the next manager call.

**Adopt:** Separate logical identity from resident data, reserve memory before
preparation, retain explicit ownership during access, and report reload/discard
churn. Extend its one-budget reservation into domain-specific peak reservations
covering shared work, staging, scratch, replacements and retirement.

**Exclude:** Synchronous recreation inside lookup, inheritance-driven resource
policy, arbitrary borrowed-pointer invalidation, after-allocation budget repair,
and destructor-based unconditional cleanup. The article's lock/refcount is not
proof that a GPU or audio callback has stopped using memory.

Applied in [budgets](resource-management.md#budgets-and-eviction) and
[physical retirement](resource-management.md#physical-retirement).

## Optional fallback objects

Noel Llopis, §1.7, “The Beauty of Weak References and Null Objects,”
*Game Programming Gems 4*, printed pp. 61–68;
local PDF pp. 75–82.
Read the complete article.

Stable intermediate records allow resources to move or be replaced. Null objects
offer a usable per-type replacement. The article explicitly warns about a caller
assuming the original dimensions and about mutating a shared fallback. Its use of
“weak reference” means indirection, distinct from ordinary ownership terminology.

**Adopt:** Keep a stable logical handle and an explicit current binding; use
immutable fallback assets only where the application declares them acceptable.
Return actual coverage and preserve the requested resource's diagnostic status.

**Exclude:** Redirecting a live borrow through a mutable holder, per-reference
heap allocations as the default, silently substituting required collision or
simulation data, and hiding every failure behind transparent/silent content.
The holder mechanism alone supplies no cross-thread publication or reclamation.

Applied in [acquisition](resource-management.md#public-acquisition-contract) and
[streaming](resource-management.md#streaming-and-platform-profiles).

## Cache replacement

Colt “MainRoach” McAnlis, §1.1, “Efficient Cache Replacement Using the Age and
Cost Metrics,” *Game Programming Gems 7*, printed pp. 5–14;
local PDF pp. 38–47.
Read the complete article, including tie cases and degeneration discussion.

The article represents recent usage in a rolling bit history, with a richer
history option, and combines usage with replacement cost. It explains why pure
recency can thrash and why pure cost can keep expensive unused data indefinitely.
Its examples focus on a texture-page cache and acknowledge equal-score ambiguity.

**Adopt:** Measure real recent uses, reload cost, churn and the pressured domain's
reclaimable bytes. Preserve a simple baseline and evaluate alternate selection
on trace replay. Keep speculative admissions probationary so scans cannot sweep
the whole useful cache. Eligibility remains a lifetime proof before any score.

**Adapt:** Costs can include read/decode/upload and dependency closure. The unit
actually reclaimed might be a shared backing page rather than one logical asset.
A short frame history is useful metadata, not a universal predictor. Do not copy
historical score weights or table arithmetic as a verified algorithm, nor infer
that every resource must be scanned every frame.

Applied in [budgets and eviction](resource-management.md#budgets-and-eviction).

## File layout

David L. Koenig, §1.9, “Faster File Loading with Access-Based File Reordering,”
*Game Programming Gems 6*, printed pp. 103–108;
local PDF pp. 106–111.
Read the complete article.

The chapter records runtime access, derives a packing order, rebuilds the pack,
and reruns the workload. It stresses comparing load times and discusses OS/disk
cache effects, fragmentation and layouts overfitted to one level. It also advocates
preprocessing and avoiding repeated reads.

**Adopt:** Produce operation/access traces suitable for cooker layout experiments,
cluster observed co-use, and compare cold/warm results on unchanged workloads.
Report overfetch and build/patch size as well as load speed.

**Adapt:** Seek-oriented optical-disk results do not prescribe NVMe concurrency,
web request grouping or modern compression chunks. Physical grouping must not
force residency of an entire level or duplicate all shared dependencies. Retain
a general baseline for new levels and user content. No historical speedup is a
Ludus estimate.

Applied in [cooking and storage](resource-management.md#cooking-and-storage).

## Hot loading

Noel Llopis and Charles Nicholson, §1.10, “Stay in the Game: Asset Hotloading for
Fast Iteration,” *Game Programming Gems 6*, printed pp. 109–116;
local PDF pp. 112–119.
Read the complete article.

The workflow separates source conversion, file monitoring, runtime notification,
and rebinding. It discusses batching duplicate file events, preserving indirection,
allowing loose per-asset overrides alongside packs, and treating complex level
state differently from simple visual assets. It also discusses overlap memory
and fragmentation.

**Adopt:** Use the normal converter for incremental changes, coalesce file events,
retain per-asset source provenance, and introduce reload first for frequently
edited immutable assets. Keep prepared overrides in private preview snapshots.
This turns the initial generic replacement mechanism into an authoring workflow.

**Exclude:** Blocking runtime loads as the default, freeing the old version before
replacement validates, assuming converted bytes need no runtime bounds checks,
and moving leased payloads during compaction. Desired-revision tokens, coherent
dependent transactions, device retirement and saved-source conflict handling are
Ludus extensions. Optional development transport is outside the core coordinator.

Applied in [replacement transactions](resource-management.md#replacement-transactions).

## Dependency jobs

Julien Hamaide, §1.9, “Multithread Job and Dependency System,”
*Game Programming Gems 7*, printed pp. 87–96;
local PDF pp. 120–129.
Read the complete article, including its future-work limitations.

The chapter separates job creation and scheduling, executes jobs on persistent
workers, and propagates dependency readiness with counts and reverse links.
Identifiers distinguish recycled table slots. The sample includes blocking group
waits, singleton management, and a specific scheduler/worker arrangement; its
future work notes limits around external loading synchronization.

**Adopt:** Count unresolved required dependencies and wake only runnable stages;
retain reverse edges for diagnostics and replacement. Schedule byte acquisition
and independent CPU preparation ahead of dependency installation. Reuse workers
when multiple real jobs justify a pool.

**Exclude:** Blocking resource waits on worker threads, a deleted/failed prerequisite
being implicitly met, a singleton scheduler, and unbounded growing tables. External
I/O/device completion uses durable result records; logical dependency completion
does not mean its memory has retired. A general job system remains optional.

Applied in [dependencies](resource-management.md#dependencies-and-publication)
and [loading concurrency](resource-management.md#loading-and-concurrency).

## Asset pipeline

Rémi Arnaud, Chapter 2, “The Game Asset Pipeline,” *Game Engine Gems 1*.
Read §§2.1–2.3, printed pp. 15–30;
local PDF pp. 43–58.
The later COLLADA/OpenCOLLADA sections are outside this review's technical scope.

These sections separate source, intermediate and target assets; discuss explicit
dependencies, automatic manifests, incremental builds, inspectable intermediate
data, editor feedback and an iteration fast path. The pull workflow traces an
editor-selected item back to its source/DCC tool rather than treating generated
data as the authoritative editing surface.

**Adopt:** Keep source/settings authoritative, derived artifacts rebuildable,
and release catalogs independently inspectable. Emit build/runtime/package roles
explicitly, preserve source mappings, and keep import/preview feedback consistent
with shipping validation. Delete-and-rebuild a cache must not lose authored work.

**Adapt:** Use a small local derived cache and explicit roots first. No universal
intermediate format, DCC automation network, database or particular XML/COLLADA
dependency is required. The engine's target variants and runtime readiness remain
separate from authoring source formats.

Applied in [cooking and storage](resource-management.md#cooking-and-storage).

## Cooked blocks

John Olsen, §1.8, “Fast Data Load Trick,” *Game Programming Gems 1*,
printed pp. 88–91;
local PDF pp. 86–89.
Read the complete article.

The article preprocesses content into a contiguous layout and reuses a temporary
buffer where the storage device's read granularity exceeds the data size.
Its example saves/restores the native bytes of a C++ object with `sizeof`.

**Adopt:** Move conversion to tools and reserve/reuse staging sized to actual
backend read requirements.

**Exclude:** Native object dumps, restored vtables/pointers, compiler-padding and
ABI dependence, unchecked direct reads into live objects, and old buffer constants
as modern backend requirements. This directly strengthens the binary-format rule.

Jason Hughes, Chapter 20, “Pointer Patching Assets,” *Game Engine Gems 2*,
printed pp. 345–357;
local PDF pp. 361–373.
Read the complete chapter, including toy implementation limitations.

The chapter moves layout preparation and relationship discovery offline, emits
coherent blocks with offsets and patch metadata, and discusses alignment, symbolic
binding, block linking, custom compression and offline introspection. Its toy
implementation assumes 32-bit pointers and omits per-structure alignment support.

**Adopt:** Contiguous typed data, explicit relationship tables, alignment propagation,
offline dependency discovery, and inspection tools. Preserve independent runtime
bindings for externally owned resources.

**Adapt:** Prefer checked relative offsets and read-only field views to runtime
pointer patching. Choose resource/chunk boundaries by measured use and lifetime,
not whole-level coherence by default. Cooked data can save allocations without
serializing C++ layouts or implementing resource relocation.

Applied in [cooking and storage](resource-management.md#cooking-and-storage).

## Sound packs

Simon Franco, Chapter 21, “Data-Driven Sound Pack Loading and Organization,”
*Game Engine Gems 2*, printed pp. 359–367;
local PDF pp. 375–383.
Read the complete chapter, including global-event limitations.

The chapter builds an event-overlap table from emitters, selects resident versus
streamed sound data, groups co-used samples, derives loading regions from storage
time and listener speed, and warns authors when overlapping loading regions
exceed audio memory. Globally possible sounds need a separate policy.

**Adopt:** Tools report likely simultaneous demand and actual dependency footprint;
streaming policy measures load latency against motion lead time. Keep global audio
under session holds and audio refill under its own timing/instance ownership.

**Adapt:** Include queueing, competing bandwidth, decode, upload/preparation and
margin in latency. Add hysteresis and explicit teleport/loading behavior. Overlap
heuristics and the chapter's grouping percentage are candidate measurements, not
a universal rule. The manager does not need spatial knowledge, binary sound banks,
or emitter graphs for its first audio acquisition.

Applied in [streaming profiles](resource-management.md#streaming-and-platform-profiles).

## Broader engine reference

Jason Gregory, *Game Engine Architecture*, third edition, Chapter 7.
Read resource database requirements in §7.2.1.2,
local PDF pp. 514–515;
build dependencies and runtime responsibilities/organization/identity/lifetime
in §7.2.1.4 and §§7.2.2.1–7.2.2.6,
local PDF pp. 521–529;
composite integrity in §7.2.2.8,
local PDF pp. 535–536;
and post-load initialization in §7.2.2.10,
local PDF pp. 540–542.

The selected passages reinforce offline/runtime separation, explicit dependency
build rules, referential integrity, source inspection and per-type preparation.
The shared-level example acquires incoming references before dropping outgoing
references. Apply that ordering to resource scopes while retaining asynchronous
physical-retirement accounting. Registry uniqueness means one prepared instance
per compatible revision/profile/session, with deliberate old/new overlap during
reload, rather than one global payload for every logical asset.

Keep stable logical identity independent of source/package paths. Historical
platform/package descriptions in this edition are not current capabilities or
requirements. Neither its pointer-fixup options nor any single example mandates a
universal manager, filesystem layer, database, or current GPU streaming algorithm.

## Current primary documentation

Current sources were checked to complement the historical readings. The source
facts below are separate from the resulting Ludus design judgments.

- Epic documents primary roots, named asset bundles, async streamable handles,
  and content auditing. Ludus adopts explicit root sets and inspectable dependency
  groups through composition-owned scopes.
  [Unreal asset management](https://dev.epicgames.com/documentation/en-us/unreal-engine/asset-management-in-unreal-engine).
- Unity describes reference counts, release differing from physical unload,
  and immediate unload/reload churn. Ludus uses separate holds, cache eligibility,
  physical retirement, and incoming-before-outgoing acquisition.
  [Addressables memory](https://docs.unity3d.com/Packages/com.unity.addressables@2.7/manual/memory-assets.html).
- Unity describes implicit dependency duplication and bundle-level dependency
  loading. Ludus reports duplication and retains independently addressed residency
  rather than equating a physical pack with an acquisition unit.
  [Addressables dependencies](https://docs.unity3d.com/Packages/com.unity.addressables@2.7/manual/AssetDependencies.html).
- Godot documents that fetching a background result can still block before it is
  ready. Ludus keeps Poll/Borrow nonblocking and disallows an implicit wait in lookup.
  [Godot background loading](https://docs.godotengine.org/en/stable/tutorials/io/background_loading.html).
- Microsoft describes accelerated queued reads and GPU/CPU decompression. Its
  guidance warns that full-queue enqueue can block, completion ordering cannot be
  assumed, dependent read discovery hurts throughput, and compression sections
  need independent decoding. Ludus uses bounded admission, durable completion,
  cooker-known dependencies and independent chunks; an accelerator is an optional
  backend, with no fixed universal chunk size.
  [DirectStorage samples](https://github.com/microsoft/DirectStorage/blob/main/README.md),
  [DirectStorage guidance](https://github.com/microsoft/DirectStorage/blob/main/Docs/DeveloperGuidance.md).

## Final assessment

The useful articles refine a coherent base rather than justify a larger framework.
The strongest improvements are explicit peak reservations, trace-based layout and
cache experiments, inspectable source-to-artifact provenance, dependency continuations,
and occupancy-aware streaming policy. The architecture's stricter cancellation,
version coherence, queue saturation, device/audio retirement, browser and shutdown
contracts come from applying those ideas to Ludus's existing boundaries.

Implementation begins with fixed capacities and explicit owner calls, and grows
only behind the same public identity/acquisition/lifetime contracts. No claim of
fastest cache policy, guaranteed streaming latency, or measured AAA throughput is
made before the [validation plan](resource-management.md#implementation-sequence-and-validation)
is implemented and measured.
