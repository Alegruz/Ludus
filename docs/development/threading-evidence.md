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
before shipping there. No real engine workload has been migrated in this slice.
See [the architecture](../architecture/threading.md) for the staged measurement
and consumer-integration plan.
