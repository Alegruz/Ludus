# Filesystem measurements

The opt-in native benchmark measures storage delivery through the public SDK.
It verifies transferred lengths and deterministic first/last bytes. It does not
measure Content resource decode, audio callbacks, GPU uploads or frame pacing.

## Reproduce

```bash
out/host-tools/venv/bin/cmake --preset linux-clang-development -DLUDUS_BUILD_FILESYSTEM_BENCHMARK=ON
out/host-tools/venv/bin/cmake --build --preset linux-clang-development --target ludus_filesystem_benchmark
python3 tests/filesystem/generate_benchmark.py out/filesystem-benchmark
out/build/linux-clang-development/modules/foundation/filesystem/ludus_filesystem_benchmark out/filesystem-benchmark/loose loose 0
out/build/linux-clang-development/modules/foundation/filesystem/ludus_filesystem_benchmark out/filesystem-benchmark raw.pack 4
out/build/linux-clang-development/modules/foundation/filesystem/ludus_filesystem_benchmark out/filesystem-benchmark lz4.pack 4
```

Use workers 0, 1 and 4 for each layout; macOS uses the corresponding preset and
build directory. The deterministic corpus has 64 small 4 KiB files and eight
2 MiB files. Each pass performs 2,048 offset reads: seven 4 KiB requests per
1 MiB request, 263 MiB delivered. Async runs use up to 32 distinct destination
slots, 32 MiB admission budget and a 5 ms starvation threshold. Files open once
before timing and retain independent offsets. The sequential baseline uses one
destination; the async throughput test includes queue and host polling overhead.
Thus latency differences include concurrency/backlog, not only thread handoff.

The first and warm passes use identical accesses, followed by 128 submit/cancel/
collect requests. Percentiles are sorted observed samples, without interpolation.
Completion time is sampled when the host collects a record under the scheduler
gate, before releasing the final file outside the gate. It includes delivery
backlog; final provider destruction/close cost is outside that timestamp. Service
time includes read execution and terminal-publication gate contention. CPU is process CPU time, including
workers and the yielding host polling loop. A real host polls during its update;
this benchmark's yielding loop can consume a core. Open time and descriptor
count are measured separately. C++ new/new[] calls are counted across all threads
only during the read pass after storage/file/worker initialization; this does not
claim zero native runtime allocation. The allocation regression independently
checks warm submission, cancellation, collection, tracing and metrics.

The Development CI job preserves `filesystem-benchmark` artifacts: runner CPU/OS,
all timings, and separate `strace -f -c` summaries for pread64, metadata, open and
close syscalls. Trace runs are separate from throughput runs because tracing
changes timing. Each Linux throughput process starts after `sync` and an OS cache
reset on its disposable runner; the first pass therefore starts with cold payload
pages, then naturally warms this small repeat-access working set. Pack metadata
and file opens occur before read timing. This is not a per-request cold-cache or
physical-device worst-case latency guarantee. Pack publishing/verification syscall
counts must be distinguished from the timed payload loop.

## Local baseline and result

Apple M5 MacBook Air (10 cores, 16 GiB), macOS 26, pinned Clang 18.1.8, Development
`-O2`, local buffered filesystem, 2026-10-06. Cache state was uncontrolled locally;
only the second pass is labeled warm. This is one run with other host workloads,
not a universal performance claim or an isolated backend comparison. An initial
single-file synchronous baseline before implementation measured p50 0.917 us,
p95 34.917 us and p99 38.459 us on mixed 4 KiB/1 MiB reads.

Warm mixed-asset results; latency in microseconds, CPU in milliseconds, queue in
milliseconds, throughput in MiB/s:

| Layout | Workers | p50 us | p95 us | p99 us | MiB/s | CPU ms | Max queue ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| loose | 0 | 0.92 | 24.00 | 30.17 | 32418.44 | 8.10 | 0.00 |
| loose | 1 | 193.92 | 219.88 | 278.71 | 20810.94 | 23.00 | 0.29 |
| loose | 4 | 88.83 | 142.46 | 163.67 | 43258.36 | 26.98 | 0.11 |
| raw.pack | 0 | 24.62 | 5860.71 | 5961.25 | 169.89 | 1546.99 | 0.00 |
| raw.pack | 1 | 24540.75 | 24878.67 | 25008.46 | 167.22 | 3125.66 | 25.11 |
| raw.pack | 4 | 6077.38 | 11564.62 | 12312.92 | 645.09 | 2015.75 | 7.83 |
| lz4.pack | 0 | 24.79 | 5896.58 | 6217.08 | 167.49 | 1561.32 | 0.00 |
| lz4.pack | 1 | 25843.88 | 40682.58 | 61577.08 | 147.90 | 3291.50 | 76.85 |
| lz4.pack | 4 | 6246.12 | 12041.75 | 12632.33 | 638.57 | 2039.22 | 7.95 |

Loose retained descriptors: 77; raw/LZ4 pack layouts: 6 (including standard/runtime
and the descriptor iterator). The 72 virtual opens share one pack archive revision;
Submit performs allocation-free retention rather than descriptor duplication.
Measured open time was about 1.2–1.7 ms loose and 0.05–0.06 ms pack. These counts
include provider lifetime and do not isolate individual syscall costs; CI's syscall
summaries provide the native operation counts and time distribution separately.

All warm/timed passes observed zero C++ allocations. Async peaks were 32 requests,
4,308,992 retained destination bytes with one worker and 7,442,432 with four,
within both configured bounds. The benchmark's synchronous metrics are disabled
and report zero scheduler gauges, not zero destination storage.

Four-worker cancellation-to-host-delivery p50/p95/p99 (us): loose 5.625/31.917/41.083;
raw pack 6.125/5977.417/6047.625; LZ4 pack 5.917/5978.666/6219.708. Immediate intent
usually cancels queued work; tails include a submitted read that must finish before
buffer reuse. Tests additionally hold a provider blocked and prove that Cancel and
Shutdown never release submitted destinations early.

The existing pack reader validates every touched block before copying bytes. Its
CRC/decode cost dominates these highly compressible cached assets. Four workers
improve throughput in this run by using independent opened-file scratch buffers,
but queueing raises completion latency. Cached small loose reads favor synchronous
ReadAt. No coalescing, direct I/O, mmap, io_uring/IOCP, codec change or automatic
async crossover is justified by this limited workload. Measure the game's asset
mix and target hardware before choosing worker count or enabling those backends.

## Post-baseline evidence

F1-F5 are implemented. The architecture owns the
[research inputs, priorities and acceptance criteria](../architecture/filesystem.md#post-baseline-research-and-improvement-plan)
for continued improvement. FS-R1 through FS-R5 identify follow-up experiments;
the harness descriptions and evidence below record their status. An implemented
harness does not certify supported-host behavior. Retain the F4 baseline above
when comparing results.

Append each evaluated experiment here with its FS-R identifier and result PR,
question/hypothesis, baseline and candidate revisions, commands, toolchain,
hardware/storage/OS configuration, workload hashes or seeds, budgets and cache
methodology. Include repeated-run variability, correctness checks and negative
or inconclusive comparisons. For recovery work also record the crash model,
permitted-state oracle, filesystem/mount settings, explored bounds and
reproducible failing image/trace location. Track Linux and macOS coverage
explicitly. For streaming work distinguish provider service, scheduler delay,
host delivery and integrated frame impact; storage-only timing does not measure
resource decode or GPU residency. Essential commands and conclusions belong in
this Markdown owner; bulky traces and images can be linked artifacts with hashes.

The [asynchronous I/O evolution decision](../architecture/filesystem.md#asynchronous-io-evolution-decision)
specifies proposed stage attribution and backend comparisons within FS-R2/FS-R3/FS-R4.
Its priority is to distinguish native transfer time from pack scratch/CRC/decode
and host backlog before changing the backend. It adds no new measured result.

## FS-R2: representative streaming trace replay

Implementation baseline: `5117c8c0`, with PR #163's design merged. The existing F4
benchmark mode and historical results above remain available unchanged. FS-R2
adds a separate `--stream` mode that measures the same public SDK, not a substitute
reader. No native backend, automatic crossover or pack-format change is enabled.

The question is whether service, scheduler queueing, host collection or shared
opened-pack scratch dominates representative loading shapes under CPU contention.
The shipped startup, transition and background traces are deterministic **synthetic
workloads**, not captured game traces. The text format also accepts captured accesses
mapped to this corpus: `fs-r2 1 COUNT`, then one row per request containing release
nanoseconds, file index, offset bytes, length bytes, priority and cancel intent
(0/1). Releases must be ordered; the parser rejects overflow, trailing fields,
invalid ranges and more than 4,096 requests. Trace/corpus identities are hashed.

```bash
out/host-tools/venv/bin/cmake --preset macos-clang-development -DLUDUS_BUILD_FILESYSTEM_BENCHMARK=ON
out/host-tools/venv/bin/cmake --build --preset macos-clang-development --target ludus_filesystem_benchmark
out/host-tools/venv/bin/python tests/filesystem/streaming.py generate out/fs-r2-corpus --count 256
out/host-tools/venv/bin/python tests/filesystem/streaming.py measure out/fs-r2-corpus --benchmark out/build/macos-clang-development/modules/foundation/filesystem/ludus_filesystem_benchmark --output out/fs-r2-results --compiler-description 'pinned Clang 18, Development O2' --storage-description 'record actual device/filesystem and background host load'
```

The default matrix repeats each combination three times in deterministic shuffled
order: three trace families, loose/raw/LZ4, synchronous/one/four workers, zero/two
integer-mix CPU-load threads, shared/independent opens and tracing off/on. Each
process uses 16 destination slots and a 16 MiB retained-destination budget. Used
assets are opened before timing. `shared` clones one opened file per asset across
slots; `independent` opens that asset separately per slot, giving packs independent
scratch. Report whole-process peak RSS as well as scheduler retained bytes: scratch,
metadata, harness buffers and samples are not covered by the destination budget.

Each corpus includes fully compressible, mixed and position-sensitive pseudo-random
assets. Expected bytes are independently generated by Python and checked across
every returned byte by C++; first/last-byte equality alone is insufficient. The
runner validates input hashes before running. Transition traces include cancel
intent; cancellation races may legitimately return completed reads instead. All
accepted identities must collect exactly once, final in-flight bytes/requests
must be zero, and trace records plus dropped records must reconcile. Native
regressions corrupt an interior byte and reject malformed/out-of-range traces.

Async admission/collection occurs on a periodic host tick (`--poll-us`, default
1,000), with sleeping between ticks. This removes the old yielding busy poll from
the new mode. Synchronous reads block serially at their release times; their queue
and delivery fields are zero, and marked cancellation skips the read. Async
cancellation is a measured intent/drain race, so do not compare delivered byte
counts or cancellation tails as if those modes performed identical work.

Each run reports nearest-rank p50/p95/p99 for queue, provider service, host delivery,
admission-to-collection, release lateness and observed cancellation-to-collection.
Delivery is completion minus queue minus service; service includes provider scratch,
CRC/decode and terminal-gate contention. It is **not isolated device time**. Release
lateness exposes waiting outside the bounded scheduler. Cancelled/skipped samples
have zero service and remain in the disposition-inclusive distributions; inspect
cancellation counts before comparing tails. An empty cancellation distribution
means no recorded intent interval, not zero cancellation latency.

Process CPU includes the host, I/O workers, validation and explicit CPU-load threads.
Full-byte validation runs during collection before a slot is reused; its accumulated
wall time is reported separately and may itself delay the host. Peak queue/completion
backlog gauges are sampled at ticks, while request/byte peaks come from scheduler
counters. C++ allocations are counted across threads after setup; this does not
cover all native allocations. Open cost, prewarming, initialization and report
sorting/allocation are outside the timed pass. Trace-capacity pairs quantify
instrumentation overhead; retain variation and negative comparisons.

`environment.json`, `runs.jsonl` and `summary.json` preserve the source/binary/corpus
identity, exact commands, failures, host details and each repetition. Summaries
report min/median/max of per-run metrics; they do not pool latency samples or imply
statistical significance. Use `--families`, `--layouts`, `--workers`, `--contention`,
`--open-modes`, `--tracing` and `--repetitions` to scope a declared comparison. The
native benchmark invocation is also retained in each JSON record for direct replay.

Cache mode defaults to `warm`, which pre-reads and validates this trace before timing.
`uncontrolled` makes no cold claim. `cold-start` requires Linux plus the explicit
`--disposable-runner` flag and permission to reset global caches: the driver syncs
and drops caches before each child. Files and pack metadata still open before
read timing, and repeated requests warm the corpus within a pass. This is neither
per-request cold storage nor a physical-device worst-case guarantee. CI preserves
three warm repetitions of the basic three-family/layout/worker matrix; CPU sharing,
opened-file scratch and tracing comparisons remain explicitly selectable studies.

Frame impact, Content resource decode/upload and actual resource-ready latency are
**not measured** by this standalone byte-delivery harness. Validate those in a real
Content/renderer consumer before adopting FS-R3/FS-R4. Before a comparison, declare
acceptable p99/CPU/residency/frame regressions; no universal budgets or performance
thresholds are inferred from these fixtures. Linux/SSD/HDD and controlled-cold runs
need their own evidence; local macOS results cannot establish them.

### Local FS-R2 harness validation

Apple M5, 16 GiB, macOS 26.6, pinned Clang 18.1.8 Development, buffered APFS.
A 128-request corpus passed 81 runs: three repetitions of every trace-family,
layout and worker-count combination, shared opens, tracing disabled, 1 ms host
ticks and no explicitly added CPU-load threads. All transferred bytes and final
accounting passed; all hot passes counted zero C++ allocations. The manifest SHA256
is `5396dfb20cd859bf90cc13fa1a6ab62a5526d33252b3d412831aa9caa84eb334`.
Other development builds were running on this host. These are **contended-host
smoke results**, unsuitable for choosing a backend or declaring a performance gain.

For the startup trace with four workers, p99 admission-to-collection milliseconds
across the three repetitions were:

| Layout | Minimum | Median | Maximum |
| --- | ---: | ---: | ---: |
| Loose | 69.26 | 130.76 | 182.45 |
| Raw pack | 411.89 | 451.29 | 914.50 |
| LZ4 pack | 257.86 | 396.97 | 526.77 |

The variation itself prevents a reliable isolated comparison. The full repeated
records, CPU/service/queue/delivery distributions, failures and exact commands are
in the runner's `runs.jsonl`/`summary.json` outputs. Repeat on an otherwise quiet
host and preserve CI artifacts before drawing a bottleneck conclusion. The old
F4 table used different payloads, access patterns, collection and byte validation;
its numbers do not supply a controlled before/after comparison for this harness.

The native filesystem suite, two fixture tests and four native benchmark tests
passed. The native cases exercise synchronous/one/four-worker reads, shared and
independent opens, trace overflow and full cancel-intent windows. Interior byte
corruption and malformed/overflowing/out-of-range traces are rejected. Integrated
frame/resource-ready acceptance, controlled-cold/device coverage and isolated
performance qualification remain open; FS-R3/FS-R4 adoption is not enabled.
