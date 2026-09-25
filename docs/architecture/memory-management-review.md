# Memory Management: Independent Adversarial Design Review

**Disposition:** Proceed with a narrow container correctness repair, then measured byte-API work. Do not begin the original combined tracking/capture phase. The general allocator strategy survives review; several lifetime and observability contracts required correction.

**Evidence baseline:** Original proposal at `07cf666`; source review refreshed at `d4a84ca`, including the new foundational-header policy. Uncommitted `.gitignore`, RHI, Conan and build-script work was present and left untouched. Observed RHI work adds enumeration/validation configuration, not a renderer or submission scheduler. This report reviews the [memory architecture](memory-management.md) and its revised [ADR 0008](../decisions/0008-memory-management.md). It does not implement an allocator or claim that the existing defects below are fixed.

Severity is scoped to the affected phase. A critical capture defect blocks capture implementation, not an unrelated container repair. **Corrected in design** means the document now specifies a defensible contract; implementation must still satisfy the listed proof/test gate.

## Review evidence and method

Read the architecture in full and checked it against [AGENTS.md](../../AGENTS.md), [steering rules](../../.kiro/steering/coding-standards.md), the standard-library and foundational-header ADRs, and these implementation surfaces:

| Surface | Evidence used |
| --- | --- |
| Containers and lifetime | [`array.hpp`](../../modules/foundation/containers/include/ludus/foundation/containers/array.hpp), [`array_support.cpp`](../../modules/foundation/containers/src/array_support.cpp), [`contiguous_storage.hpp`](../../modules/foundation/containers/include/ludus/foundation/containers/detail/contiguous_storage.hpp), [`relocation.hpp`](../../modules/foundation/containers/include/ludus/foundation/containers/relocation.hpp), `StaticArray`, container tests and allocation-count executable |
| Object ownership | [`pointer.hpp`](../../modules/foundation/base/include/ludus/foundation/base/pointer.hpp), platform factories and native-resource deleters |
| Bootstrap and diagnostics | [`core.h`](../../modules/foundation/base/include/ludus/foundation/base/core.h), Base assertion/diagnostic implementation, generated configuration, logger state and shutdown |
| Threading/capture | [`recorder.cpp`](../../modules/foundation/profiling/src/recorder.cpp), [`trace_chunk.hpp`](../../modules/foundation/profiling/src/internal/trace_chunk.hpp), profiling control/export, logging async/flush workers and queue |
| Platform/resource lifetime | [`window_wayland.cpp`](../../modules/platform/src/window_wayland.cpp), headless window implementation, [`rhi.cpp`](../../modules/graphics/rhi/src/rhi.cpp), [`apps/smoke/main.cpp`](../../apps/smoke/main.cpp) |
| Build/SDK | Root/module CMake, [`EngineOptions.cmake`](../../cmake/EngineOptions.cmake), sanitizer/flavor configuration, [`CheckSdkVariant.cmake.in`](../../cmake/CheckSdkVariant.cmake.in), SDK consumer tests, Conan, CI and header gates |

Repository searches checked allocation families, placement construction, alignment, globals/TLS, synchronization, exceptions, module exports and jobs. No production scheduler, device submission or general memory module was found. There are real logging/profiling workers and TLS state; absence of a scheduler does not make teardown single-threaded.

The adversarial method was to construct interleavings and ownership histories, then ask whether the stated contract rules them out. These are design counterexamples, not runtime failures in an allocator that does not yet exist. One existing container defect was independently reproduced using pinned Clang 18 and ASan/UBSan. No allocator-performance experiment was run. The Gems review was not repeated: none of the decisive issues depended on disputing a historical technique. A narrow C++23 language check was needed for the storage-lifetime contract in C3.

## Critical issues

### C1 — Callback counting does not by itself protect callback acquisition

**Blocks:** Phase 4 event capture and any reuse of the CPU recorder's lifecycle. **Status:** Corrected in architecture §§6.4, 11.5, 13 and Phase 4B; production synchronization remains to implement.

The proposal required a safe point but also permitted unspecified in-flight accounting as if that supplied the missing reclamation proof. This interleaving defeats a common implementation:

1. Producer reads the active session pointer, then pauses before incrementing its in-flight count.
2. Control disables capture, observes zero in-flight callbacks and frees the session.
3. Producer increments or calls through the stale pointer.

Even counting only callbacks is too narrow for exact snapshots: a producer can be inside allocation between backend success, accounting and event publication. A generation comparison is not a lifetime pin. A TLS destructor can also publish after the recorder/session has gone away.

The current `TraceRecorder::EndCapture()` only disarms recording. `ThreadRecorder` owns a borrowed chunk and its destructor consults the process recorder; this is not evidence of synchronized reclamation. Logging has separate worker and optional flush-thread lifetimes. A shutdown sequence that stops renderer workers but leaves logging active is insufficient.

**Correction:** The first memory capture supports externally quiesced sessions only. Its lifecycle owner closes work admission and joins or obtains acknowledgements after all producers leave whole Memory operations, keeps them stopped, then attaches/detaches and seals their buffers. This applies to activity capture as well as complete capture. No new general thread registry or epoch-reclamation framework. If a worker cannot be stopped, retain the session and callback code and return an incomplete/busy outcome; never free under it. TLS cleanup does not own eventual publication.

**Gate:** Deterministically pause a producer before observer entry, inside observation, after it but before API return, and during thread exit. Verify that control cannot reclaim state until the owner has acknowledged each operation's completion. Verify allocations after detach still work. Later live attachment needs a separate admission proof that protects reference acquisition before reading the session pointer, plus its own tests; it is postponed.

### C2 — “Started at startup” is not a complete ownership baseline

**Blocks:** Phase 4C complete-live-set/leak claims and terminal invalid-free diagnosis from ledger absence. **Status:** Corrected in §§7.4 and 11.3–11.5.

The design correctly allows pre-main Core allocations, but originally described a ledger “from startup” as sufficient for exact validation. Counterexample:

1. A static initializer allocates block P through Core.
2. `main` starts a ledger and labels it complete.
3. P is freed later; no ledger entry exists.
4. Missing-entry-as-invalid-free reports a fatal error for a valid block, or a leak query silently excludes a real allocation.

Starting before logging, starting at a frame boundary, or sampling live-byte totals cannot reconstruct P's identity. After ledger overflow, snapshotting the incomplete table cannot repair it either.

**Correction:** Completeness is per covered domain and begins either before its first allocation or at a synchronized, valid zero-outstanding boundary followed by uninterrupted tracking. Core can remain activity-only while a newly registered resource domain is completely covered. Unknown-baseline frees remain unknown; metadata absence is proof only with full coverage of that ownership history. Byte counters, ledger completeness and event-stream completeness are distinct states.

**Gate:** Multi-translation-unit static-initialization allocation/free tests, late activation with nonzero live totals, proven-empty-domain activation, ledger/event overflow, and stop/restart with surviving allocations. No valid pre-baseline free may fail merely because tracking started later.

### C3 — Replacing the allocation function must preserve C++ object creation semantics

**Blocks:** Phase 1B container seam migration and Phase 3 vendor backend acceptance. **Status:** Corrected in §7.1 and the implementation gates.

`Array::AllocateStorage` casts the returned pointer to `T*`; container helpers perform pointer arithmetic and construct elements individually. The current standard `operator new` allocation supplies language-level implicit object creation. Alignment alone is not that guarantee. Naming `posix_memalign`, `_aligned_malloc` or a vendor function as the replacement left the C++23 storage-lifetime obligation unspecified.

**Correction:** A successful nonstandard backend result must pass through a standard nonallocating placement-new storage-lifetime bridge, or an equally explicit supported implementation guarantee, before typed container use. Return the suitable resulting pointer, preserve address/alignment and the original free family, and leave nontrivial element construction to Containers. This is a storage adapter obligation, not a new allocator hierarchy or permission to use raw realloc on objects. See the [C++23 object-creation rule](https://timsong-cpp.github.io/cppwp/n4950/intro.object) and [standard allocation semantics](https://timsong-cpp.github.io/cppwp/n4950/c.malloc).

**Gate:** Review the language argument on the actual pinned toolchain and test over-aligned and nontrivial arrays through each backend. ASan/UBSan are useful but cannot establish all C++ lifetime rules. Do not expand the engine's relocatability extension in the same change.

### C4 — The first migration consumer already has a reproduced use-after-free

**Blocks:** Trustworthy container baseline/migration. **Status:** Existing defect, recognized by the original audit and independently reproduced here; not repaired by this documentation task.

`TryResizeImpl` grows/frees the old buffer before consuming its fill reference. Minimal reproduction:

```cpp
Array<int32> values;
values.EnsureCapacity(1);
values.Add(42);
values.Resize(2, values[0]);
```

Pinned Clang 18, `-std=c++23 -fno-exceptions -O1 -g -fsanitize=address,undefined`, current headers and `array_support.cpp` reproduce **heap-use-after-free** at `UninitializedFill`/`ConstructAt`, reached from `array.hpp`'s fill after `TryEnsureCapacity`. The probe linked the existing sanitizer Base library solely for diagnostic symbols; it was not a fresh full-engine build. Probe source and log were kept outside the repository in `/tmp/ludus-memory-hostile-review/`.

Changing the allocator can hide this defect through different reuse behavior. A faster microbenchmark would not make the result valid.

**Correction:** Phase 0A is now precisely this repair and its regressions, before any allocator implementation. `TryAddInPlace` also constructs a self-moved value before it knows growth will succeed; that separate unchanged-on-failure contract is Phase 0B. Neither justifies a combined smart-pointer/profiler/backend refactor.

## Significant issues

### S1 — API counters were mislabeled as physical backing peaks

**Before Phase 1 statistics and Phase 4 export. Corrected in §§7.1 and 11.2–11.3.**

The free protocol removes metadata and subtracts bytes before backend free. Pause that thread at this point; a second thread allocates another equally sized block. Domain high-water can stay at one block while the backend temporarily holds two. Subtracting afterward permits the opposite observation lag and address reuse complications. Several atomics do not form one transaction either.

The corrected metric is outstanding **requested bytes at API accounting boundaries**, reconciled at quiescence, with a high-water of that accounting history. It includes explicitly charged allocate-copy-free overlap but is not the true physical backend peak. Backend retained/committed/usable metrics and process RSS remain separate sources. No global allocation serialization is introduced to strengthen a graph label.

Counter overflow was also relegated to validation. Long-running cumulative traffic and IDs can overflow without live address-space exhaustion. All-build telemetry now requires checked updates, sticky invalid/saturated status, no silent return to “exact,” and controlled rebaselining. IDs never wrap into a live identity. Test near-limit injected state, concurrent snapshots, and resize/failure reconciliation; include checked-update/CAS cost in benchmarks.

### S2 — Existing allocation-count tests would observe the wrong seam

**Before Phase 1B. Corrected in §10.3 and its phase.**

[`allocation_tests.cpp`](../../modules/foundation/containers/tests/allocation_tests.cpp) replaces global new/delete and counts those calls. A direct C/vendor backend bypasses it. Positive allocation counts fail, while a zero-allocation test can pass vacuously. The current sanitizer exclusion is an artifact of the interposition mechanism, not an unavoidable property of allocation tests.

The migration must replace counting/failure observation in the same PR with a private test-target seam/backend, including a positive control. No production mutable backend and no public allocator interface for testing. Run the resulting contract tests with sanitizers. Retain isolated interposition probes only for their separate whole-process purpose.

### S3 — The initial phases were too broad and falsely sequential

**Before implementation planning. Corrected in §17.**

Original Phase 0 bundled Array, UniquePtr, profiler alignment/reclamation and a benchmark harness. Phase 4 bundled counters, origin IDs, ledger, live validation, capture control, loss recovery and export. These are multiple independent ownership/concurrency changes, with different failure proofs and rollback boundaries.

The roadmap now names small steps and actual dependencies. UniquePtr repair gates object-factory migration; trace repair gates trustworthy trace benchmarks/shared lifecycle, not a standalone byte API. Gauges precede activity capture; complete covered domains follow only when needed. Backend comparison can occur before domain-aware Array. One consumer determines whether an arena is worth publishing.

Each step has a testable result and no half-migrated pointer family. SDK/backend rollback requires a full rebuild, not a runtime toggle.

### S4 — Borrowed arenas lacked an annotation handback contract

**Before Phase 5. Corrected in §§8 and 12.**

Poisoning on rewind/reset is insufficient when backing is borrowed. If the arena is destroyed while its stack/heap buffer remains alive, leaving the bytes poisoned breaks the owner's subsequent legitimate use. Overlapping arena borrows or shadow-granule updates can also poison a neighbor.

Borrowing is now exclusive; backing outlives the arena; destruction requires no scopes/users and restores annotations before returning the range. Arbitrary byte packing cannot promise exact ASan suballocation redzones. Sanitizer-only padding must expose its capacity difference, and unrepresentable edge poisoning must not affect external bytes. Tests cover nested rewind, adjacent ranges, unaligned backing and reuse after destruction. This does not claim that ASan catches a stale pointer once its address has been reused.

### S5 — SDK installation checks do not prove container ABI agreement

**Before Phase 2B. Corrected in §§6.5 and 17.**

`CheckSdkVariant.cmake.in` rejects a conflicting install prefix. It does not reject a precompiled consumer whose Array is 24 bytes when a newly linked module expects 32. Generated policy values alone do not force old object files to participate in a check.

The layout migration requires a full rebuild and a memory-policy-versioned allocation-seam symbol/signature, exercised by a deliberately stale-header SDK consumer test. Remove the old unversioned symbol; adding a new guard call alone cannot reject old objects that never called it. This protects cooperating seam users; it is not a general ABI verifier. Modules exchanging owning C++ objects still need matching compiler/runtime/policy, and future DSOs need one host runtime or creator-owned destruction. Static libraries do not eliminate CRT ownership requirements.

### S6 — A public raw object factory would make the wrong destructor convenient

**Before Phase 6. Corrected in §7.3 and Phase 6.**

The draft warned against placing Memory-created pointers in default `UniquePtr`, but its easiest proposed factory returned exactly such a raw pointer. Current `UniquePtr` also loses an existing destination on move assignment and does not transfer deleter state. “Prefer RAII” did not specify the safe path.

After the independent Base owner repair, the first consumer-facing factory should return the existing UniquePtr with a domain-carrying Memory deleter. Keep raw creation/destruction internal unless a consumer needs release. Require nothrow construction/destruction and exact-type ownership; conversions need creator-provided dynamic destruction and original allocation address. No second smart-pointer hierarchy, base-pointer size guessing, or Memory dependency in Base. Test partial initialization, nonzero base subobject addresses, and deleter state transfer before migrating polymorphic windows/sinks.

### S7 — Origin attribution and current ownership were conflated

**Before Phase 2 and profiler labeling. Corrected in §§6.3 and 10.2.**

Moving a buffer must carry its domain to preserve the free contract. It follows that its tag need not name the current business owner. Copy construction inherits the source domain; copy assignment retains the destination. Nested owners have independent policies: `Array<Array<T>>(rhiDomain)` does not retag default-constructed inner arrays.

The revised design calls these allocation-origin budgets, documents one-owner-deep propagation, and tests nested/moved containers. This is preferable to hierarchical allocator traits or ambient mutable tag scopes. A subsystem needing different attribution explicitly copies into an owner constructed with that domain.

### S8 — Performance gates needed controlled comparisons

**Before Phase 0E/3 decisions. Corrected in §15.3.**

The proposal appropriately disclaimed measured gains, but some phrases still invited treating cheap counters, a backend swap or a bump allocator as established improvements. Per-domain relaxed atomics can contend and false-share; the additional Array word affects density even if allocation speed is unchanged. A ledger lock may dominate an instrumented profile but says nothing about ordinary Release.

Added controlled pairs: same backend with benchmark-only accounting off/on; same accounting and payload across backends; same consumer outputs for reserve/reuse versus arena. Keep growth/layout constant, verify payload outputs, separate throughput timing from sampled latency, quantify timing overhead, and use independent process runs for retention/peaks. Closed-loop allocation timing does not measure queued work/frame stalls. Hardware counters, RSS and tail samples need their own coverage/noise labels.

| Claim or assumption | What would actually test it | Readiness |
| --- | --- | --- |
| System is an adequate initial backend | Correct aligned/fallible behavior, current container/system workload and process-memory baseline | Architectural baseline; no fastest-allocator claim |
| Mimalloc might improve Ludus | Identical explicit coverage; full workload time/tails, retained/RSS/usable memory, remote frees, startup and binary cost | Experiment only; vendor results do not select it |
| Counters are acceptably cheap | Controlled off/on benchmark binary; same-domain contention and thread-exit remote frees; absolute and application cost | Unproven; 1% target is a proposed budget |
| Domain-aware Array is worth one word | Dense empty/nested-owner traversal and total working set, not just append throughput | Unproven; gate before ABI migration |
| An arena improves scratch work | Equal outputs/destruction versus stack/inline/reserved storage; include setup/reset, retained capacity, exhaustion and sanitizer capacity differences | No current required consumer; postpone if no benefit |
| Frame arenas improve frame times | Real renderer with retirement delays; p99 frame latency and slots × workers × retained capacity | Cannot establish with present event loop |
| Capture is low overhead | Idle/activity/validation/stack modes separately; transport loss, metadata bytes and exporter peak | No claim until each mode exists and is measured |
| Requested-byte peaks describe fragmentation | They do not; add allocator usable/retained data with scope and process metrics | Corrected metric definition; no invented fragmentation percentage |

The draft's 5% workload/10% memory benefit and 2% tail/5% memory regression gates are provisional review budgets. They require adequate samples and uncertainty smaller than the claimed effect; they are not universal allocator truths or unit-test pass thresholds.

## Minor improvements

1. **Decision numbering and source drift:** Renumbered the memory proposal to ADR 0008 because accepted ADR 0007 now belongs to `core.h`. Refreshed the review baseline without treating concurrent RHI/build edits as this task's work.
2. **Header boundary:** Explicitly keep Memory, tags, vendor includes and ownership adapters out of `core.h` and its optional PCH. Require standalone public-header checks with PCH disabled; a source-level module arrow alone does not enforce include hygiene.
3. **No-op resize:** Equal-size resize may preserve the pointer without backend traffic or an event. Actual byte resize remains deferred until there is a consumer; Array object relocation does not use it.
4. **Names and immutable identity:** Domain identity/routing is immutable; statistics are mutable. Retain Ludus verbs and `usize` for layouts. Tags/site definitions must have stable storage, bounded registration and no ID reuse; cache IDs with the definition epoch when session-owned.
5. **Diagnostic wording:** Exactness, unknown baseline, saturation, transport loss and unavailable backend metrics need distinct result flags. `noexcept` means no exception propagation; it does not prove recoverable OOM for strings, constructors or `std::thread` startup.

## Rejected criticisms

These concerns were investigated and do not justify replacing the chosen architecture.

| Concern tested | Finding and reason for rejection |
| --- | --- |
| A AAA engine must own malloc | No current workload defeats platform/mature backends. Handwritten size classes, coalescing and remote frees would add obligations without evidence. Retain System baseline and optional measured mimalloc. |
| No global interception means unusable observability | Explicit Array/slab/factory seams provide useful bounded coverage. Unknown strings/runtime/native heaps remain labeled and can be investigated externally. Observability does not justify silently changing third-party ownership. |
| Override only some C++ allocation overloads | The proposal deliberately overrides none: scalar/array, sized, aligned and nothrow families remain the runtime's responsibility. Existing aligned Array allocation/free pairing is deliberate. Partial overrides and mixed sanitizer interceptors would be worse. |
| Platform malloc interception is needed for completeness | It adds bootstrap, CRT, symbol and sanitizer problems and still cannot infer semantic lifetime. Keep it outside the SDK in isolated tools. Foreign callbacks can be added per library when justified. |
| Core needs a destructive shutdown function | Pre-main, late local/static and TLS frees are real. Process-lifetime routing/counters/domains are the appropriate simplification; capture lifecycle must not own the heap. No required initialization for raw allocation. |
| Cross-thread free requires TLS heaps or per-thread domain ownership | Domains are stable, backend concurrency is selected explicitly, and callers transfer payload ownership with synchronization. Origin metadata survives the originating thread. A private TLS heap would create new reclamation hazards. |
| Size/alignment on free are redundant API baggage | Array already knows capacity bytes and element alignment. Passing them avoids mandatory per-block headers. Wrong layout/domain is a caller violation; complete diagnostic metadata can validate it. Unsized external callbacks can use local adapter metadata. |
| Zero bytes must behave like every platform malloc | The explicit zero-to-null, alignment-first contract is coherent and documented. Pointer-only return cannot distinguish rejected zero layout from empty success; callers requiring that distinction validate layout. A new result hierarchy is unnecessary for current consumers. |
| Realloc can optimize arbitrary Array relocation | Correctly rejected. Typed lifetime/relocation remains in Containers; any later raw-byte resize must preserve old storage on failure, alignment, attribution and ordering. No silent domain retagging. |
| An OOM path should log, capture a stack or ask subsystems to reclaim | Correctly excluded from Memory. These operations can allocate, acquire unsafe locks or reenter the allocator. Caller policy and the independent Base emergency path are sufficient. |
| Assertions necessarily recurse into Logging | The inspected Base path uses bounded state and native diagnostic output, not ordinary Logging. The design correctly mandates terminal REQUIRE/FATAL for proven corruption and does not rely on resumable ASSERT. Its arguments must still remain allocation-free. |
| Every allocation should carry a stack/source location in Release | There is no workload justification. Cached sites and optional proven-safe stack collection belong to capture; offline symbolization and tooling-memory accounting are appropriate. Compiled-out argument evaluation needs tests. |
| Array should accept any allocator, including a frame arena | Correctly rejected: repeated growth retains abandoned buffers, Clear retains a potentially stale pointer, and destruction can outlive reset. Individually freeable domains and explicit transient spans have different contracts. |
| Frame-number modulo should authorize reset | Already rejected by the state machine. It only chooses a candidate; closed admission and all relevant CPU/GPU consumers determine reuse. Delayed jobs, cancellation/device loss and multi-queue retirement require tests when those systems exist. |
| All CPU scratch must wait on the GPU | Already rejected. CPU-only build data and GPU-visible mapped ranges have different consumers; one global GPU wait would inflate retention. |
| Generation IDs make escaped raw pointers safe | The original draft already states that raw pointers/spans cannot be checked this way and ASan may miss reuse. Keep explicit ownership and consumer tests; do not create a compulsory checked-pointer universe. |
| A universal VM layer is required for portability | Not now. Public size/alignment/ownership contracts are portable; native allocation adapters are private. Reserve/commit/discard semantics and consoles remain consumer-driven future work. Linux is the only validated build today. |
| Existing driver/native handles should enter the general heap | Wayland destroy/disconnect and Vulkan create/destroy pairing remain authoritative. GPU memory and CPU host callbacks are different accounting/lifetime systems. A future callback adapter must retain its originating module/context. |
| Debug guards duplicate sanitizers usefully everywhere | Mandatory guards/quarantines/stacks would impose cost with no demonstrated gap. ASan/UBSan remain primary, with TSan for synchronization and focused arena annotations. Debug and Release share ownership/failure contracts, not identical diagnostic coverage. |

The weak parts were precision and staging around these choices, not the absence of more allocator types.

## Simplifications

| Component | Concrete consumer | Revised scope |
| --- | --- | --- |
| `FoundationMemory` byte boundary | Existing Array seam; later aligned trace slab | Keep one small module with a build-selected private backend. No monolithic MemoryManager, public virtual allocator or runtime backend switch. |
| Allocation domains | Subsystem-origin budgets and matched free | Core first; bounded named domains when attribution lands. One handle per owning buffer; no private heap per tag or tag hierarchy. Measure the owner word. |
| Linear arena and scratch scope | No mandatory current consumer | Publish only after a specific batch beats reserve/reuse or demonstrates worthwhile predictability. One mechanism, no separate stack/region/scratch heaps. |
| Frame contexts | Future renderer completion ownership | Keep invariants in the design; no implementation or fixed two/three-frame ring yet. No speculative scheduling abstraction in Memory. |
| Pools, slabs, segregated/free-list heaps | None beyond existing local bounded storage | Postpone public allocators. Fix the existing trace pool's correctness independently; do not turn it into a generic allocator library. |
| TLS/VM-backed arenas | No measured current requirement | Postpone. Explicit task/worker ownership and native VM constraints can be introduced with actual scheduling/address-stability needs. |
| Ledger | Optional origin matching and ownership validation | Start with one private bounded locked table. Assign IDs under the existing lock; defer shards and per-thread ID registration/ranges until contention warrants them. |
| Capture lifecycle | Explicit diagnostics session | Require caller-quiesced start/stop initially. Defer live attachment and general reclamation infrastructure. No observer code at all in Phase 1. |
| Capture detail | Domain budgets first, then observed lifetimes | Gauges → activity session → proven complete domains. Defer stacks, a continuous collector, richer viewers and complete leak analysis until their prerequisites exist. |
| Object factory | A selected engine-owned object migration | Return the repaired existing owner with a domain deleter; raw helpers initially internal. No parallel ownership type. |

## Final implementation readiness

**The next change is Phase 0A, not a memory allocator.** Its exact scope is:

1. Add a failing regression for full-capacity `Array::Resize` and `TryResize` whose fill value aliases an existing element.
2. Repair alias handling in `array.hpp` without changing public API, allocation family, container layout or growth factor.
3. Cover trivial and nontrivial elements, growth/no-growth, zero/shrink behavior and balanced construction/destruction; run existing container tests and pinned ASan/UBSan validation. The reproduction in C4 must become clean.
4. Leave global allocation overrides, `UniquePtr`, tracing, tagging and production Memory untouched in this change. Those have separate reviewable prerequisites.

This is small, independently testable, reversible and keeps `main` buildable. The review makes no claim that these tests are currently green: the adversarial alias probe intentionally failed with ASan, and no production fix was authorized in this task.

| Phase/step | Feasibility and prerequisites after correction |
| --- | --- |
| 0A | Ready to implement now, exact scope above. Validate correctness; no performance claim required. |
| 0B / 0C / 0D | Separate contract/owner/trace repairs. Each has its own regression and benchmark boundary. 0B gates container migration; 0C gates Memory deleters; 0D gates trustworthy trace workload/shared lifecycle use. Do not bundle them. |
| 0E | Baseline runner plus result schema. Container/system baseline follows relevant repairs; trace benchmark waits for 0D. It is independently useful without Memory. |
| 1A | Small standalone Linux byte API/Core statistics after baseline setup; no engine migration. Needs no-exception failure tests, C++23 storage proof, checked arithmetic/accounting, multi-TU bootstrap/teardown, thread-exit remote free, header/SDK gates and measured wrapper cost. |
| 1B | Array seam and allocation-observation tests change together after 0A/0B and accepted 1A results. No Array layout change. Clean rebuild ensures no mixed live allocation family. |
| 2A / 2B | Registry first, then measured domain-aware Array plus propagation/ABI tests. Explicitly reject a stale-policy seam consumer; no change to Base's include boundary. Rollback requires full rebuild. |
| 3 | Optional pinned backend adapter after Phase 1, followed by a separate evidence/default decision. Same contract suite/coverage for both. Retaining System is a successful result. |
| 4A | Useful gauge export with no observer or ledger. Test label/validity semantics. |
| 4B | Ready only when its owner can demonstrate whole-operation quiescence and reliable export/control. Bounded activity capture, delayed-operation tests and loss/overhead measurements; no live attach or exact preexisting heap. |
| 4C | Conditional on a real complete-live-set/validation need. Requires proven per-domain baseline, continuous ledger lifetime and explicit loss recovery. Cannot be inferred from 4B passing. |
| 5 | Not yet justified for deployment. Requires a named consumer and comparison against reserve/reuse; sanitizer handback and escape tests. Prototype first, public API only on evidence. |
| 6 | One ownership family per change after the corresponding owner repair. Creator/destructor migrate together; raw realloc only with an actual adapter. OOM inside arbitrary user constructors remains outside the byte API guarantee. |
| 7 | Blocked by absent renderer/submission/scheduling requirements, intentionally. Need actual completion tokens, fake delayed-consumer tests and end-to-end frame/retention benchmarks before implementation. |

**Review validation:** Independently reproduced C4 with Clang 18 ASan/UBSan; checked source/build contracts and the C++23 rule cited in C3; checked documentation links/anchors, decision references and whitespace after editing. No production allocator code, benchmark result or full-engine test pass is claimed. Remaining choices such as capacities, active-capture budgets and a backend default require measurements at their named gates, not another architecture rewrite.
