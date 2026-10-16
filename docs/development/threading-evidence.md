# Threading kernel validation and performance evidence

## Scope and method

Measured on 2026-10-04 with Clang 18, the Development build (optimized, assertions
enabled), on an Intel Core i5-8265U: four physical cores, eight logical CPUs.
No affinity or frequency pinning. Static analysis and sanitizer tests shared
the machine; the recorded one-minute system load was 7.24-7.27. These are
contended-machine observations, not an isolated latency guarantee.

The optional `ludus_threading_benchmark` builds 256 independent jobs before timing.
Each sweep uses one warm-up and 11 complete Submit-to-Wait samples per worker/grain
combination. Three sweeps were run; the table is the median of their medians in
milliseconds. Worker counts exclude the helping caller. Every job output is compared
with its serial oracle; all comparisons passed. This is a synthetic arithmetic
batch, not an engine-frame or competitive scheduler benchmark.

| Iterations per job | 0 workers | 1 worker | 2 workers | 4 workers | 8 workers |
| --- | ---: | ---: | ---: | ---: | ---: |
| 0 | 0.007905 | 0.040011 | 0.053121 | 0.090280 | 0.121377 |
| 10000 | 5.838764 | 2.907198 | 2.022913 | 1.277750 | 1.144395 |
| 100000 | 58.761773 | 29.747872 | 20.214239 | 13.258273 | 12.040987 |

Checksums across all worker counts: `256`, `18367026793921049022`, and
`7928766144346410088`, respectively. Checksums supplement the per-job equality
checks; equality is not inferred from checksum agreement alone.

Tiny jobs show a clear dispatch penalty. At 10,000 and 100,000 iterations, four
workers plus a helping caller completed these batches about 4.6x and 4.4x faster
than the serial executor. Eight workers do not provide a reliable improvement
over four across all sweeps on this SMT machine. Keep worker budget explicit and
batch small tasks; neither the results nor hardware thread count justify consuming
the entire CPU budget of a game. The shared gate remains a known scaling limit.

Reproduce with:

```sh
./init.sh --cli --locked --with-tests --preset-only linux-clang-development
out/host-tools/venv/bin/cmake --preset linux-clang-development \
  -DLUDUS_BUILD_THREADING_BENCHMARK=ON -DLUDUS_WARNINGS_AS_ERRORS=ON
out/host-tools/venv/bin/cmake --build --preset linux-clang-development \
  --target ludus_threading_benchmark
out/build/linux-clang-development/modules/foundation/threading/ludus_threading_benchmark
```

## Correctness coverage

The threading suite exercises graph bounds, cycles, duplicate/stale/foreign
handles, ordinary non-atomic output publication through fan-in dependencies, wide
fan-out/fan-in and repeated reuse, serial/worker execution, invalid callback
outcomes, failure/cancellation propagation, recursive-wait rejection, graph/system
destruction and draining. Linux linker wrappers inject synchronization initialization
and partial thread startup failures and verify joins and retry. Allocation injection
checks initialization cleanup; a separate allocation-counting executable checks
zero scheduler allocation during repeated Add/Seal/Submit/Wait/Reset cycles.

TSan runs the execution and native startup tests independently of ASan/UBSan. The
browser serial smoke checks dependency execution, reset invalidation and failure
blocking under the pinned Emscripten toolchain. An installed SDK consumer links
and executes the public API without private engine headers. CI also exercises the
normal Debug/Development, sanitizers, PCH, public-header and build-time-budget gates.

Windows uses a private native backend but has not been compiled or executed by
this Linux validation. It requires validation on a supported Windows toolchain
before shipping there. The first real engine workload, the static-lighting bake,
is recorded [below](#static-lighting-bake-t1). See [the architecture](../architecture/threading.md) for the staged measurement
and consumer-integration plan.

## macOS ARM64 validation (T0)

Measured on 2026-10-16 on an Apple M5 (4 performance and 6 efficiency cores,
10 logical CPUs, 16 GiB) running macOS 26.6. The pinned Clang 18.1.8 built the
Development configuration with warnings as errors. Other builds shared the machine;
the one-minute load average was 24.8-25.2.

- `ludus_foundation_threading_tests` (16 test cases, including the T0 contract
  tests) passed in 200 consecutive runs. `ludus_threading_alloc_tests` passed in 20
  repeated CTest runs, with zero scheduler allocations during steady reuse.
- `ludus_threading_benchmark` produced the same checksums as the Linux run above
  (`256`, `18367026793921049022`, `7928766144346410088`). Every per-job output
  matched the serial oracle for 0, 1, 2, 4 and 8 workers. Because of the load,
  its timings are not reported as performance evidence.
- ASan/UBSan and TSan were not completed on this host. With the pinned LLVM 18
  runtime on macOS 26.6, ASan deadlocks during runtime initialization before
  `main` (an existing ASan test binary from another checkout behaves the same), and
  an empty TSan program crashes on start. These are toolchain failures before test
  entry, not threading results. CI's macOS 14 ASan/UBSan job now runs
  `ludus_foundation_threading_tests`; the development job runs every test labelled
  `threading`.

The Linux-only startup-failure tests (`--wrap` linker injection), a completed
macOS TSan run and any Windows execution remain open under milestone T3 of
[the architecture](../architecture/threading.md#milestones).

## Static-lighting bake (T1)

Measured on 2026-10-16 on the same Apple M5 (4 performance and 6 efficiency cores,
16 GiB, macOS 26.6), with the pinned Clang 18.1.8 Development build (optimized,
assertions enabled), no affinity or frequency pinning. Other sessions share this
machine, so the benchmark started only once the one-minute load average fell below 4.
The reported sweep ran at a load of 3.8-4.2.

`ludus_static_lighting_benchmark` bakes a 2,062-triangle scene: a point-lit
32×32-cell receiver grid under 13 area emitters and a partial blocker, at the
editor's quality of 64 samples per triangle and depth 4. Scheduled bakes use the
default 64 triangle ranges plus one preparation job. Each configuration runs one
warm-up and 21 measured bakes, and every run (warm-up included) compared every chart
byte for byte with the serial `Step` oracle. All runs matched. Worker counts exclude the
caller, which helps in `Finish`, so *N* workers execute on *N*+1 threads. Times are
milliseconds unless marked. Percentiles are nearest-rank over the 21 runs.

| Configuration | Bake p50 | p95 | p99 | Critical path p50 | Callback work p50 | Dispatch to first range p50 | Overhead p50 | Speedup p50 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Former editor path: `Initialize` + `Step(256)` on one thread | 135.18 | 136.46 | 141.14 | — | — | — | — | — |
| Serial executor (0 workers) | 121.16 | 126.90 | 130.95 | 3.79 | 121.15 | 0.2 µs | 0.02 | 1.00 |
| 1 worker | 76.35 | 84.89 | 85.39 | 4.69 | 149.80 | 2.2 µs | 1.45 | 1.59 |
| 2 workers | 56.81 | 57.82 | 58.46 | 5.17 | 164.96 | 4.5 µs | 1.82 | 2.13 |
| 4 workers | 40.00 | 41.29 | 42.06 | 6.51 | 189.95 | 9.8 µs | 2.01 | 3.03 |
| 8 workers | 28.80 | 39.39 | 42.64 | 7.38 | 230.53 | 28.2 µs | 3.19 | 4.21 |

Definitions, from `StaticLightingBakeProfile`:

- *Bake* is the owner's wall time from `Submit` until `Finish` returns, including
  allocation, graph construction and the owner merge.
- *Critical path* is preparation + dispatch to the first range + the longest range
  + the owner merge. That is the span of this two-level graph. Serially it is 3.8 ms:
  preparation (BVH and surface terms) plus one range of about 1.9 ms (121 ms / 64).
- *Callback work* sums every job callback's wall time plus the merge. The owner merge
  (status merge and chart encoding) took 0.05-0.09 ms at p50.
- *Overhead* is bake time minus the lower bound max(critical path, callback work /
  executing threads). On the serial executor that is pure scheduling cost: 0.02 ms
  for 65 jobs, about 0.3 µs per job. With workers it also includes load imbalance,
  wake latency and the 64-range tail. That totals 1.4-3.2 ms, or 2-11% of the bake.

Interpretation:

- The serial executor costs nothing measurable compared with the former single-thread
  loop. The two differ by about 10% between sweeps in both directions (a second sweep at
  load 4.2-5.5 measured 119.90 and 122.51 ms). This is the oracle configuration.
- Callback work grows from 121 ms serial to 231 ms with 8 workers. Once
  more than four threads run, the efficiency cores and shared caches make each path
  slower, so throughput scales sublinearly: 4.2× on nine threads. The critical path
  (7.4 ms) is not the limit at this size. Dispatch to the first range grows with the
  wake herd (28 µs at eight workers), but it stays well under 1% of the bake.
- The editor runs three workers and polls without helping, which also means three
  executing threads, so it is comparable to the 2-worker row. That moves the former
  135 ms bake to about 57 ms of background latency while keeping cores free for Qt and
  the RHI. The second sweep's 2-worker row measured 75.31 ms. Run-to-run variance on
  this shared machine is therefore around ±30% at a given budget, and these figures
  are not latency guarantees.
- A third sweep was discarded: another session's build raised the load to 21 during
  it, and its serial executor median rose to 328 ms.

Correctness evidence for the consumer, in the `[static-lighting][threading]` renderer
tests:

- Charts equal the serial `Step` oracle bit for bit for 0/1/2/4 workers × 1/3/64/1024
  ranges, including more ranges than triangles, through both `Poll` and `Finish`.
- Admission and preparation failures (uninitialized or busy system, invalid range
  count, preparation-time validation failure) leave outputs and profiles preserved.
- Cancelling mid-evaluation stops ranges, reports `NotReady` and releases the system.
- `Cancel` followed by `Shutdown` reports cancellation, `Shutdown` without `Cancel`
  drains the bake to its oracle result, and destroying an in-flight bake cancels and
  drains it before the next bake reuses the system.

These cases passed 200 consecutive runs. The full renderer suite passed (96 cases),
and so did the editor suite (185 passed, 6 skipped), including the `StaticLightingJob`
interruption and destruction test. ASan/UBSan and TSan for this consumer must come
from CI for the reason given in the T0 section.

Reproduce with:

```sh
out/host-tools/venv/bin/cmake --preset macos-clang-development -B out/build/t1-benchmark \
  -DLUDUS_BUILD_THREADING_BENCHMARK=ON -DLUDUS_BUILD_EDITOR=OFF
out/host-tools/venv/bin/cmake --build out/build/t1-benchmark --target ludus_static_lighting_benchmark
out/build/t1-benchmark/modules/graphics/renderer/ludus_static_lighting_benchmark
```
