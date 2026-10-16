# Memory Phase 0: Trace Prerequisites and Baselines

This page records phase 0 of the
[memory profiling architecture](../architecture/memory-profiling.md): steps 0D
(trace slab alignment and lifetime) and 0E (reproducible baselines) of the
[memory management plan](../architecture/memory-management.md#phase-0-establish-trustworthy-correctness-and-baselines).
Step 0A, the aliased `Array` resize, is recorded in
[Memory Phase 0A](memory-phase-0a.md). Step 0C, the `UniquePtr` ownership repair,
lands separately through the
[smart pointer](../architecture/smart-pointers.md) delivery slice 1; this phase
does not change `UniquePtr` and has no owner baseline.

No allocator, allocation domain counter, memory recorder or production
allocation policy is introduced. FoundationProfiling now depends on
FoundationMemory for its trace slab.

## Trace slab alignment (0D)

`TraceChunk` is `alignas(64)`, but the recorder placement-constructed chunks in
storage from `calloc`, which does not portably supply 64-byte alignment. The
size `maxChunks * sizeof(TraceChunk)` was also unchecked.

[`recorder.cpp`](../../modules/foundation/profiling/src/recorder.cpp) now
computes the slab size with `TryMultiply`. It reserves the slab from the system
`AllocationDomain` at `alignof(TraceChunk)` and releases it through the same
domain with the same size and alignment. An overflowing or failed reservation
leaves capture disarmed and the engine running normally. Chunks are still
value-initialized, so footprint and zeroing cost match the previous `calloc`
path once touched.

## Trace lifetime and recycling (0D)

The audit in the memory architecture listed four defects. Each now has a fix
and a regression in
[`recorder_tests.cpp`](../../modules/foundation/profiling/tests/recorder_tests.cpp).

| Defect | Repair | Regression |
| --- | --- | --- |
| The free-list CAS stack was ABA-prone: concurrent producers could pop a chunk another thread had already popped and reused. | The free and full lists are intrusive lists under one recorder mutex, taken once per chunk (4,096 events), never per event. | Eight producers sharing three chunks while a collector drains and recycles concurrently; every accepted event is drained exactly once. |
| `EndCapture` only disarmed recording. It did not wait for producers or collect their partial chunks, and a later `BeginCapture` could release the slab under a producer still writing. | Each `Emit` announces itself in a thread-local flag before testing the capture flag (sequentially consistent pairing). `EndCapture` disarms, waits until no linked producer is emitting, then publishes every partial chunk. Chunks are acquired only while armed, so the slab is never released under a producer. | Live threads holding partial chunks at `EndCapture`; threads exiting during capture; 40 stop/restart sessions with varying slab sizes while producers keep emitting, each checking that drained events equal recorded events. |
| Chunks left in the full list by an unexported capture, and per-thread chunks discarded by the generation check, were never returned to the pool. A second capture without an intervening export recorded nothing. | `BeginCapture` rebuilds the free list from every chunk; unexported events of the previous capture are discarded explicitly. | Repeated captures in the alignment and restart tests. Reproduced against the previous recorder by the baseline tool: its second capture recorded 0 of 200,000 events. |
| Thread and site registration claimed a table slot with a relaxed increment and wrote the entry afterwards, so readers could see a count before the entry. The instrumentation macro guarded site registration with an unsynchronized `static bool`. | Writers append under the mutex and publish the count with release ordering; readers acquire it. Site registration uses a guarded function-local static, which runs once even when several threads reach the site first. | Eight threads registering and reaching one site concurrently; every name resolves. |

The recorder is constant-initialized and never destroyed, so thread-exit hooks
that run during process teardown still detach safely. The public contract is in
[`trace_system.hpp`](../../modules/foundation/profiling/include/ludus/foundation/profiling/trace_system.hpp)
and summarized in the [FoundationProfiling guide](../modules/foundation/profiling.md#capture-lifecycle-contract).
This protocol is specific to the CPU trace. It does not provide the memory
capture loss, baseline or safe-point contracts of the memory profiling design.

## Validation

Host: Apple M5 (4 performance and 6 efficiency cores), macOS 26.6, pinned
Homebrew Clang 18.1.8 with the repository's libc++ headers. Checks and
measurements ran on `6337d603`; the later base `5939344e` changes only CI.

| Check | Result |
| --- | --- |
| `macos-clang-development` build, warnings as errors | Warning-clean. |
| `macos-clang-development` CTest | 86 of 88 passed. `ludus_platform_cocoa_tests` skips every case without `LUDUS_TEST_COCOA=1` and passes with it in a WindowServer session. `ludus_header_self_sufficiency` hit its 120 s limit under concurrent load and passes on its own (155 headers). |
| Profiling tests, repeated | 28 cases, 20 consecutive passing runs. |
| ThreadSanitizer | 15 consecutive passing runs of the 28 profiling cases, no reports. |
| AddressSanitizer + UndefinedBehaviorSanitizer | 5 consecutive passing runs, no reports. |
| clang-tidy 18, clang-format 18, source policy, include and naming gates | Pass, including the benchmark source. |

The pinned LLVM 18 sanitizer runtimes do not start on this macOS 26.6 host: a
TSan build of an empty `main` crashes and an ASan build hangs. The sanitizer rows
therefore used Apple Clang 21.0.0 with the same sources, generated headers and
defines; the Linux CI sanitizer jobs remain the pinned-toolchain evidence.

## Baselines (0E)

Raw samples (`benchmarks.csv`) and per-process peak RSS, build and host
metadata (`metadata.json`) are stored for three runs:
[baseline](memory-phase-0-evidence/baseline/metadata.json) (every workload, 5
fresh processes each, seed 10), and
[trace before](memory-phase-0-evidence/trace-before/metadata.json) and
[trace after](memory-phase-0-evidence/trace-after/metadata.json) (trace
workloads, 6 processes each, seed 11). Their `revision` field names a local
pre-squash commit; the measured C++ sources are identical to this change. Each process ran one warmup and seven samples, so medians are over 35
or 42 samples. The build was `macos-clang-profile` (RelWithDebInfo, `-O2`,
Profile flavor, profiling enabled), without sanitizers or CPU pinning. Two
unrelated sanitizer processes from other worktrees occupied about two cores
throughout; the recorded one-minute load average was about 5.4. Treat absolute
values as specific to this loaded host.

| Workload | Operation | Median ns/op | Interquartile range | Median peak RSS |
| --- | --- | ---: | ---: | ---: |
| `idle` | process baseline | — | — | 1,376 KiB |
| `system-small-churn` | random alloc/free of 16–256 B, 4,096 slots | 27.96 | 26.82–29.74 | 1,808 KiB |
| `system-large-blocks` | 4 MiB, page-aligned, one write per 4 KiB, free | 5,814 | 5,182–5,839 | 5,552 KiB |
| `system-remote-free` | free a 64 B block on another thread | 11.12 | 10.55–12.38 | 6,112 KiB |
| `array-append-unreserved` | `Array<uint32>` append to 2^20 with growth | 0.598 | 0.595–0.610 | 9,776 KiB |
| `array-append-reserved` | the same after `EnsureCapacity` | 0.537 | 0.478–0.539 | 5,552 KiB |
| `array-append-aligned64` | 64-byte over-aligned element append to 2^16 | 5.07 | 4.99–5.15 | 9,776 KiB |
| `trace-emit-1-thread` | one instant event, capture armed | 30.26 | 28.52–32.01 | 45,136 KiB |
| `trace-emit-4-threads` | wall time per event, four producers | 56.51 | 54.14–62.62 | 45,280 KiB |
| `trace-session-setup` | Begin + End, slab released and reserved | 41,065 | 40,857–41,363 | 13,904 KiB |

The large-block cost is far below a first-touch page-fault cost for 4 MiB,
which suggests the system allocator reuses the freed block's resident pages
between iterations; it is a steady-state reuse measurement, not a cold-allocation
one. Peak RSS of the trace emit rows includes the untimed export drain of
200,000 events; the 64-chunk slab itself is about 6 MiB.

### Trace recorder before and after

`trace-before` links the same benchmark, flags and libraries against the
FoundationProfiling sources of `6337d603` (the base of this change); only the
recorder differs. Both variants recorded every event of every sample, with no
drops.

| Workload | Before median (IQR) | After median (IQR) | Change in median |
| --- | ---: | ---: | ---: |
| `trace-emit-1-thread`, ns/event | 28.17 (27.97–31.02) | 28.64 (28.43–28.97) | +1.6% |
| `trace-emit-4-threads`, ns/event | 51.33 (48.02–55.64) | 52.83 (50.35–57.35) | +2.9% |
| `trace-session-setup`, ns/session | 84,362 (82,120–89,831) | 38,460 (36,762–40,285) | −54% |

The per-event interquartile ranges overlap, so these samples show no clear
emit regression from the in-flight flag. They do not rule out a small one, and
they say nothing about higher thread counts or contended producers on other
hardware. The setup difference is consistent across samples but its cause was
not isolated: both variants value-initialize every chunk, and the former
`calloc` path and the aligned system-domain path may differ in how the platform
allocator reuses or zeroes a freed large region. No speedup claim is made from it.

## Limitations

- Results come from one Apple Silicon host and one optimized configuration. They
  are a starting point for later comparisons on the same workloads, not engine
  frame-time claims; the workloads are synthetic models, not captured traces.
- Peak RSS is per process and includes the runtime, the tool and the workload's
  own retained memory; the `idle` row is the process baseline to subtract.
- Logging setup/flood/shutdown and RHI startup baselines in the memory plan's
  workload list are not measured here.
- The owner churn baseline belongs with the separate `UniquePtr` repair.

## Reproducing

Configure an optimized preset with the benchmark enabled and run the fresh-process
runner. On macOS the system Python may be older than the required 3.10; use the
pinned host-tools environment or another 3.10+ interpreter.

```bash
out/host-tools/venv/bin/cmake --preset macos-clang-profile -DLUDUS_BUILD_MEMORY_BENCHMARKS=ON
```

```bash
out/host-tools/venv/bin/cmake --build --preset macos-clang-profile --target ludus_memory_bench
```

```bash
out/host-tools/venv/bin/python scripts/python/memory_baseline.py --binary out/build/macos-clang-profile/tools/memory-bench/ludus_memory_bench --cmake-cache out/build/macos-clang-profile/CMakeCache.txt --output-dir out/memory-baseline
```

`ludus_memory_bench <workload>` runs one workload: one warmup, then seven timed
samples printed as integer CSV (`workload,sample,operations,nanoseconds,bytes,checksum`).
`--list` prints the workload names. The runner starts each workload in a fresh
process, in seeded random order per round, records the process's peak RSS with
`wait4`, and writes `benchmarks.csv` and `metadata.json`. Trace workloads are
available only where `LUDUS_PROFILING_ENABLED` is 1 (Debug, Development and
Profile); they drain each capture to `/dev/null`, untimed, so every sample starts
from the same pool state.
