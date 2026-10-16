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

## FS-R1: bounded publication recovery harness

Implementation baseline: `5117c8c0` (PR #163's design is merged). The hypothesis
is that F5's file-then-rename-then-directory-sync ordering preserves the declared
recovery contract, including empty and partially written multi-block files.
There is no new publication API or stronger durability promise in this change.

Build `ludus_filesystem_recovery_probe`, then run:

```bash
out/host-tools/venv/bin/cmake --build --preset macos-clang-development --target ludus_filesystem_recovery_probe
out/host-tools/venv/bin/python tests/filesystem/recovery.py --probe out/build/macos-clang-development/modules/foundation/filesystem/ludus_filesystem_recovery_probe --output out/fs-r1-process
```

The controller kills a child running the actual `persistence_posix.cpp` at each
successful temporary-create, partial-write, file-sync, close, rename,
directory-sync and completed-publication boundary. It checks create/replace,
empty/12,305-byte payloads and all three sync policies. Short writes are capped
at 4 KiB through the existing private test seam. The kernel remains alive:
before rename the old/missing destination must remain; after rename the complete
new bytes must be visible. Each case republishes afterward to check lock release
and tolerance of abandoned private temporaries. JSON records preserve the exact
boundary and recovered byte hash. This is **process-crash evidence only**.

For storage recovery, use root in a disposable Linux VM with loop/device-mapper,
`dm-log-writes`, ext4/XFS tools and Josef Bacik's `replay-log` installed. The
`Filesystem block recovery` workflow pins its replay tool source revision and
runs this command with paths to its own outputs:

```bash
sudo out/host-tools/venv/bin/python tests/filesystem/recovery_block.py --probe out/build/linux-clang-development/modules/foundation/filesystem/ludus_filesystem_recovery_probe --replay-log out/replay-log-source/replay-log --filesystem both --output out/fs-r1-block --max-prefixes 256 --disposable-vm
```

The runner creates exclusive sparse images below a fresh output directory; it
accepts no device or mount-root argument. It formats and syncs a seed, records the
real publication through dm-log-writes, and replays every block-log prefix from
the start mark through completion onto a fresh copy of that seed. Recovery mounts
may replay the filesystem journal. A changed image from one recovery mount is
never reused for another prefix. Exceeding the declared bound fails rather than
sampling silently. Missing kernel/tool support is a failed/unavailable experiment,
never a storage-recovery pass.

The declared model is completion/flush-ordered block-prefix replay with devices
honoring flush/FUA. It excludes sector tearing, arbitrary write permutations,
controller faults and unbounded operation sequences. Before a possible rename,
the seeded destination is required. Without directory acknowledgment, the
conservative oracle allows the old, missing or new name; file-synced new data must
be exact, while PublishOnly makes no durability assertion about recovered new
bytes. After acknowledged FileAndDirectory sync, only the complete new destination
is permitted. These outcomes are fixed in `recovery.py` and have negative tests;
they must not be widened to turn a failure into a pass.

Each run records kernel/host, mount options, source revision, probe/tool hashes,
commands, boundary marks and prefix results. Failures retain the baseline,
recorded/replay images and log; successful cases retain logs, hashes and traces.
Artifacts are scratch data and stay outside `docs/`. `--max-prefixes` bounds each
case, not total disk usage: plan for sparse 512 MiB seed/record/replay images and
a 256 MiB log per active case, plus retained successful logs. The root runner
cleans up only its own mounts, mappings and loops; images remain for inspection.

Local validation on Apple M5 (16 GiB), macOS 26.6, Clang 18.1.8 Development:
84 process cuts and republications passed; all oracle/orchestration regressions
passed. The existing filesystem and persistence-fault suites also passed. Commands:
`ctest --test-dir out/build/macos-clang-development -R 'filesystem_(tests|persistence_fault_tests|process_recovery_tests|recovery_oracle_tests)' --output-on-failure`.
Linux block-recovery results are supplied by the separate workflow, not inferred
from these process tests. macOS
APFS storage recovery and stronger device-cache flushing remain evidence gaps;
the portable process test cannot close them. No native backend is enabled here.


### Linux FS-R1 block-prefix evidence

[PR #263](https://github.com/The-Ludus-Project/Ludus/pull/263), code candidate
`92a7f8f0` on the F5 baseline `5117c8c0`; CI checked-out merge source
`f7789882006966e1f03d5370ba2ffed897df59a4`. The
[successful recovery run](https://github.com/The-Ludus-Project/Ludus/actions/runs/38838253249)
used Ubuntu 24.04, kernel `6.17.0-1022-azure`, x86-64/glibc 2.39 and Ubuntu
Clang 18.1.3 Development. Its `filesystem-block-recovery` artifact records exact
commands, source/probe/tool hashes, mkfs output, marks and per-prefix byte/name
outcomes. Replay tool source was pinned to
`7b70d8a6863c5de30933d42a7672d35d01d2dc6c`.

| Filesystem | Completed create/replace × empty/multi × sync-policy cases | Recovered block-log prefixes |
| --- | ---: | ---: |
| ext4 | 12 | 196 |
| XFS | 12 | 134 |
| Total | 24 | 330 |

Every explored prefix satisfied the fixed byte and directory-entry oracles,
including exact new bytes/name after acknowledged file-and-directory sync. The
seeded old inode is additionally checked for unchanged synced bytes even when
PublishOnly permits arbitrary recovered **new** data. Each prefix started from
its own seed copy; the bound was 256 prefixes per case. This is a pass for the
declared loopback/block-prefix model on this host, not a physical power-cut,
sector-tearing, arbitrary-permutation or APFS result.

The requested mount options were `noatime,nodiscard`. Observed ext4 options were
`rw,noatime`; XFS reported `rw,noatime,attr2,inode64,logbufs=8,logbsize=32k,noquota`.
Images were harness-created 512 MiB files on the disposable runner; backing-host
and virtual-device assumptions remain part of this model. Probe SHA256:
`ca160170d52b2e626f668ebcd6f01560b3d9de3b88a982bd0afc5039d53ab8fa`;
replay-tool SHA256:
`12cb246bd8c1c377966ca4eac057588d2a680a04f8639f26079fffbbfcbab94b`.

The [first run](https://github.com/The-Ludus-Project/Ludus/actions/runs/38836953485)
completed all 12 ext4 cases and seven XFS cases, then failed with `target is busy`
while unmounting the next XFS seed, before that publication started. The preserved
command log identifies this cleanup failure separately from an oracle failure.
The controller now retries only that specific unmount condition for a bounded
interval, without force/lazy unmount; persistent busy and other failures still
fail the experiment. Dedicated regressions cover both paths. The original failed
run remains linked; the recovery oracle was strengthened, never relaxed.


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
