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
for continued improvement. FS-R1 through FS-R5 are proposed experiments, not
results claimed by this page. Retain the F4 baseline above when comparing them.

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
