# Filesystem architecture

Status: F0 design and reference review complete; F1 through F4 implemented. Validation is
recorded in the implementation PR.
Reference review follows the baseline and records revisions below. This is a storage architecture for
Ludus; it does not implement an operating-system filesystem.

## Baseline decision

Keep three boundaries: FoundationFilesystem owns explicit-error byte I/O and
root capabilities; a later virtual namespace maps logical paths to immutable
mount snapshots; Content owns resource identity, schemas, hashes and decoding.
Filesystem depends only on FoundationBase, never on logging, content, graphics,
windowing, a global service locator or a not-yet-existing job system.

Prefer offset reads into caller-owned buffers. A file owns an opened revision,
not a mutable shared cursor or a pathname that can silently select a different
revision. Opening and cloning may allocate with explicit failure; warm reads
and path validation do not allocate. Sequential cursors belong in adapters.
Every operation exposes typed errors; native error numbers remain available to
debuggers and higher-level diagnostics. No exceptions or heavy public headers.

A directory capability pins its native root once. Validate relative UTF-8 paths
without silently rewriting names: reject absolute paths, empty components,
'.', '..', NUL, backslashes, colon and control bytes. Traverse with descriptor-
relative operations that refuse symlinks at every component. A trusted root is
selected by the host; this boundary is not a sandbox against hostile mutation
of already-open directories, bind mounts, hard links or privileged processes.

Linux and macOS provide synchronous regular-file reads through a shared POSIX
backend. Other targets explicitly return Unsupported. Empty files are valid. Bound
sizes and offsets before narrowing. ReadAt returns the actual progress, retries
interrupted syscalls and handles short reads. EOF is successful zero progress.
Atomic pathname replacement preserves an open file's old revision. Detect
ordinary in-place size/mtime changes before and after reads; metadata is not a
cryptographic snapshot guarantee. Assets need immutable publication plus digest
validation when strong revision integrity is required.

Native validation covers UTF-8 names, trusted symlink roots, child symlink and
FIFO rejection, sparse offsets above 4 GiB, nanosecond modification times,
replacement/unlink lifetime, clone/move ownership and concurrent reads. Linux
uses GNU symbol wrapping for fault injection. A separate macOS test library
compiles the same exception-free POSIX implementation with a private compile-time
syscall/allocation seam; production has no hooks or mutable fault state. Darwin
tests exercise interrupted operations, short/partial reads, mutation during a
read, allocation/metadata failure cleanup, close-on-exec and descriptor reuse
after a failed close. macOS CI runs these tests and Content read adapters in
Development and with ASan/UBSan. Content also supports Linux/macOS atomic saves
through its separately tested adapter; see [native saves](content-resources.md#native-saves).
FoundationFilesystem F5 persistence and watchers remain separate work; F4 adds
a dedicated bounded I/O pool above retained virtual file revisions.

## Virtual namespace and shipping storage

Use explicit roots such as assets, project, cache and user; keep write policy
separate from read lookup. Publish immutable mount tables at a host-controlled
boundary. Resolve by explicit priority plus a stable tie-breaker; a collision
must be inspectable. Only NotFound permits fallback to a lower-priority mount;
corruption, access denial and I/O errors must remain visible. An open file pins
its provider and mount revision through unmount/reload.

Loose files serve authoring. Shipping packs use a versioned, endian-explicit
index with sorted canonical logical paths, offsets, stored/decoded lengths,
codec identity and integrity metadata. Hashes accelerate lookup but never
replace full-key equality. Validate overflow, overlapping/escaping ranges,
duplicates, unsupported features and configured resource limits before mount.
Use independent bounded compression blocks and measured alignment, not one
monolithic compressed stream. Signatures are a separate authenticity policy;
a checksum alone does not make downloaded content trusted.

## Streaming and persistence

First measure synchronous baseline latency and syscall/descriptor overhead.
Then add bounded asynchronous requests above offset I/O: byte and request
budgets, caller-owned destination lifetimes, deadlines/priority with starvation
prevention, per-request cancellation, terminal completion exactly once, and
host-owned completion delivery. A blocking worker fallback establishes behavior;
io_uring/IOCP or browser fetch can replace submission behind the same contract
only after measurement. Cancellation is not permission to reuse memory until
completion. Never perform I/O or decompression in an audio callback or render
critical section.

Keep storage-page caching, decoded resource residency and GPU upload budgeting
separate. Default to buffered OS I/O; mmap and direct I/O require evidence and
platform-specific failure/lifetime contracts. Prefetch is a hint, not a
correctness condition. Watchers invalidate hints; overflow triggers bounded
rescan and explicit revision selection, not mutation of active readers.

Saving uses same-directory exclusive temporary creation, complete checked
writes, explicit flush policy and atomic publication. Distinguish publication
from durable persistence, including directory sync where supported. Expected-
revision checks require cooperative locking for strict compare-and-swap;
uncooperative writers can race. Keep current Content save behavior until the
write contract has dedicated tests and a replacement API.

## Delivery plan and evidence

| Slice | Scope | Acceptance |
| --- | --- | --- |
| F1 | Root capability, validated paths, move-only revision files, offset reads; Content read adapter migration | Regression tests, native builds, sanitizer runs, format/tidy, SDK and browser compile contracts |
| F2 | Virtual paths, immutable mounts, memory provider | Deterministic overlays, failure fallback, unmount lifetime, injected provider faults |
| F3 | Pack reader and pack builder | Fuzzed index validation, bounded decode, reproducible packs, corrupt input tests |
| F4 | Bounded async scheduler and metrics | Cancellation/lifetime/overload tests, representative latency/throughput evidence |
| F5 | Persistence and watcher adapters | Failure injection across publication stages, durability reporting and rescan tests |

No throughput improvement is claimed before measurement. Benchmark mixed
small/large assets, warm/cold cache, concurrent offset reads, loose/pack layout
and target hardware. Record p50/p95/p99 completion latency, CPU, allocation,
bytes in flight, descriptor count, queue age and cancellation latency. Keep
traces bounded and opt-in; report operation, provider/mount generation, logical
path, requested range, result/native code and bytes without recursive logging.

## Contracts for F1

Public headers use FoundationBase, span and string_view only. Root and file
objects are move-only, with transactional Open/Clone: failure preserves the
previous output. They release descriptors through RAII. Destruction, moving,
reopening and closing require exclusive ownership; concurrent const OpenRead,
Clone and ReadAt calls are supported while their owners remain alive. Destination
buffers must not overlap during concurrent reads. Explicit Close reports errors;
destructors release best-effort and never log. Linux and Darwin close are never retried after
EINTR because the descriptor may already have been released and reused. Thanks
to Apple, XNU `kern_descrip.c`, `fp_close_and_unlock` in the
[macOS 14 source](https://github.com/apple-oss-distributions/xnu/blob/xnu-10002.1.13/bsd/kern/kern_descrip.c):
its descriptor-release-before-final-close order confirms the Darwin ownership
contract. No XNU code was copied. Darwin revision checks use `st_mtimespec`;
Linux uses `st_mtim`, preserving seconds and nanoseconds on both hosts.

ReadAt fills the requested range up to the captured EOF, looping on short reads.
Offsets above the captured size fail; offset equal to size succeeds with zero
bytes. Each syscall is capped below the native transfer limit, and 64-bit file
offsets and opt-in checked integer conversions at native/size boundaries are required at compile time. On a syscall error, returned progress is
inspectable but not a complete requested range. Changed returns zero valid bytes;
the destination may have been touched and must be discarded. A metadata check
also runs on an empty/EOF read. Clone duplicates the descriptor, retains the
same metadata revision and has no pathname lookup. Content's adapter owns its
own position and clones to position zero, preserving its existing decoder API.

Logical path equality is byte-exact and case-sensitive. Unicode is validated,
not case-folded or normalized by runtime I/O. F2/F3 tooling must detect host case
and Unicode-normalization collisions before publishing a cross-platform pack;
native lookup follows each filesystem's case and Unicode equivalence rules,
including case-insensitive macOS volumes. Linux filenames are not automatically
portable to macOS or Windows.
The cooking/pack pipeline must also reject reserved names and trailing dots or
spaces when publishing portable assets.
F1's limit matches existing Content (1,024 path bytes); native roots have a
separate 4,096-byte limit and embedded NUL is always rejected. Roots are trusted
host input, so their own symlink ancestry is permitted. Child symlinks are not.

## Contracts for F2

`namespace.hpp` adds owned, allocation-free `VirtualPath` keys with explicit
assets/project/cache/user roots and F1's relative-path validation. Logical
keys remain byte-exact and case-sensitive, without Unicode normalization.
`CreateMemoryProvider` copies input paths and bytes, rejects duplicate full
keys, and uses a sorted index with binary lookup. Empty files and empty
providers are valid on every platform, including the browser. The directory
provider delegates to F1, retaining its native filename-equivalence and revision
checks rather than claiming portable host spelling or a stronger snapshot.

The host creates a complete `MountSnapshot` with a nonzero generation, then
publishes a handle under its own synchronization. Mount IDs are nonzero and
unique; priority descends, and the lowest stable ID wins equal priorities,
independent of input order. Prefixes match whole components and are removed
before provider lookup. An exact prefix has no relative file key and does not
match. Providers are retained and prefixes copied, so editing inputs cannot
mutate published lookup. An empty snapshot represents unmounting everything.
No write policy or process-global mutable namespace is introduced.

Only `NotFound` falls back. Every other status/native diagnostic remains
visible and preserves the caller's prior opened file. `Describe` enumerates
mounts in resolution order, and an optional caller-owned bounded attempt trace
reports actual provider outcomes and IDs; its total count remains available
when the trace is truncated. A provider returning success without a file is
reported as `IoError`. Custom providers must implement the documented explicit-
error, independent revision lifetime and concurrent offset-read contracts.

`VirtualFile` retains the entire originating snapshot and provider file through
unmount/reload; clones share that opened revision without pathname lookup or
allocation. Open/create failures are transactional, and all ownership changes
require exclusive access to the affected handle. Separate stable handles and
const lookups/reads support concurrent use. Reads and path validation allocate
nothing; provider/snapshot creation and opening use checked admission and
nothrow allocations with explicit `OutOfMemory`. Indexes are bounded to 65,536
memory entries and 256 mounts; this synchronous slice adds no async scheduler.
Portable case/normalization/reserved-name collision detection remains required
at F3's pack publishing boundary, not inferred from a Linux/macOS filename.

Regression coverage includes deterministic shuffled overlays, root/prefix
boundaries, duplicate/invalid publication, bounded traces, fallback for every
status, partial provider read faults, retained unmount/reload lifetimes,
independent native revision reads, and an isolated allocation seam exercising
every construction/open allocation plus allocation-free warm reads/clones.
The seam is compiled only into an unexported test archive.

## Contracts for F3

`pack.hpp` adds `CreatePackProvider(storage, relativePath, limits, output)`.
The storage provider may be a native directory or an immutable memory provider;
pack lookup and decoding work on every target, including the browser. The factory
pins one opened archive revision and validates its complete metadata before
publishing a provider. Failure preserves the previous output. Packs mount through
F2 without a separate namespace, Content identity scheme or runtime global.

Version 1 uses little-endian unsigned integers, an 80-byte header and byte-exact
UTF-8 paths in strictly increasing lookup order. All reserved fields must be zero.
The following offsets are bytes from the start of the archive:

| Header offset | Field |
| --- | --- |
| 0 | Eight bytes `LUDPACK` followed by NUL |
| 8, 12, 16 | u32 version (1), header size (80), feature flags (0) |
| 20, 24, 28 | u32 decoded block size, file count, block count |
| 32, 40, 48, 56 | u64 index offset (80), index size, payload offset, exact archive size |
| 64, 68 | u32 index CRC32, header CRC32 (with bytes 68-71 zeroed) |
| 72 | u64 reserved (0) |

The index begins with variable-length entries: u32 path length, first block,
block count, reserved; u64 decoded length, stored length; then exactly that many
UTF-8 path bytes. It ends with block records: u64 absolute payload offset; u32
stored length, decoded length, codec and decoded CRC32. Codec 0 stores raw bytes;
codec 1 stores an independent LZ4 block without dictionaries or frame headers.
Compressed blocks must be smaller than their decoded bytes. The decoded block
size is a power of two from 256 through 65,536 bytes. Each file owns a consecutive
run of block records; interior decoded blocks are full, and the last has the
remaining bytes. Empty files own no blocks. There is no padding or implicit
alignment policy in version 1: payload ranges must cover the payload exactly,
without overlaps, gaps, escapes or trailing bytes, independently of index order.

Before publication, the reader checks header/index CRC32, UTF-8/F1 path validity,
full-key sorted uniqueness, integer admission, table ownership, exact length
sums, codecs and every storage range. Defaults admit at most 8 GiB storage,
32 MiB serialized index, 65,536 files, 262,144 blocks, 1 GiB decoded per file and
16 GiB total decoded. `PackLimits` can lower or raise these budgets, with a hard
64 KiB decoded-block ceiling and native allocation-size checks. Malformed data
returns `CorruptData`, unsupported versions/features/codecs return `Unsupported`,
and configured admission failures return `LimitExceeded`. Storage failures retain
their original status/native diagnostic. Only F2's `NotFound` permits fallback.

Opening a nonempty pack file allocates at most twice the advertised block size
as scratch. Warm reads and clones allocate nothing. Reads of one opened revision
serialize its scratch use; separate opens own separate scratch. Each touched
block is read and fully checked before any of its bytes reach the caller. A
fault reports only previously verified progress; `Changed` reports zero valid
bytes. Native revision checks bracket reads, including empty/EOF reads, and run
at lookup even for missing keys. Replacement/unlink retains an opened archive;
ordinary in-place size/mtime changes fail. CRC32 detects corruption, not hostile
forgery or authenticity, and metadata-preserving mutation needs a separate
immutable-publication/digest/signature policy. Metadata is captured at mount;
payload verification is lazy, so successful mounting does not certify every
payload byte.

Build packs offline with Python 3.10 or later, without third-party Python packages:

```sh
./scripts/pack cooked-assets out/assets.lpk
./scripts/pack cooked-assets out/assets.lpk --layout out/layout.json
./scripts/pack cooked-assets out/assets.lpk --raw --block-bytes 4096
```

The output must be outside the trusted source tree. The builder rejects child
symlinks, nonregular files, invalid/bounded paths, Windows reserved components,
trailing dots/spaces and file/directory collisions. It checks case-folded NFD
Unicode equivalence at every directory prefix without rewriting logical keys;
this conservative publishing policy is distinct from each host's native lookup.
It checks ordinary source size/mtime mutation while streaming at most one input
block at a time into a temporary payload spool. Publishing uses an exclusive
same-directory temporary output and replacement; failure preserves an existing
output. This offline publication is not F5's durable-save guarantee or a sandbox
against hostile changes to the trusted source tree.

An optional layout manifest is exactly `{"version":1,"paths":["startup/a", ...]}`.
Listed files come first in that order; unlisted files follow canonical UTF-8
order. Unknown/repeated paths or unsupported manifest fields are rejected. The
lookup index stays sorted regardless of physical layout. Pack bytes contain no
timestamps, machine paths or random identifiers. Identical source bytes, paths,
block/codec policy and manifest produce identical bytes independent of source
enumeration. The printed JSON report records format/builder/Unicode database
versions, counts, budgets used and the canonical manifest SHA-256. Keep that
report and the input manifest with trace provenance; the builder does not infer
an access trace or claim loading speedups. Reproduce publishing admission with
the same Unicode database version when exchanging toolchain environments.

Tests cover corrupt metadata and payloads, resource limits, raw/compressed/empty
files, cross-block and concurrent offset reads, native replacement/unlink/change,
unmount lifetimes, every pack allocation failure and allocation-free warm reads.
Builder tests check reproducibility, reordered payloads, portable collisions,
bounded source reads, failed publication and compatibility with an independent
system LZ4 decoder. SDK and browser consumers exercise a fixed golden pack.
`LUDUS_BUILD_FILESYSTEM_FUZZERS=ON` builds an isolated native Clang/libFuzzer
executable with the entire reader/decoder instrumented by ASan/UBSan. Its bounded
harness fuzzes raw codec inputs and repairs metadata checksums to reach deep
index/range validation. CI runs a seeded 60-second campaign; this is regression
coverage, not a proof that every malformed input is safe. Reproduce on the
reference Linux toolchain:

```sh
cmake --preset linux-clang-development -B out/build/filesystem-fuzz -DLUDUS_BUILD_FILESYSTEM_FUZZERS=ON
cmake --build out/build/filesystem-fuzz --target ludus_filesystem_pack_fuzzer
python3 tests/filesystem/generate_pack_fixtures.py out/fuzz/fixture.hpp --corpus out/fuzz/corpus
out/build/filesystem-fuzz/modules/foundation/filesystem/ludus_filesystem_pack_fuzzer out/fuzz/corpus -max_total_time=60 -max_len=65536 -timeout=2 -rss_limit_mb=512
```

Thanks to Yann Collet, [*LZ4 Block Format Description*, revision 2022-07-31](https://github.com/lz4/lz4/blob/v1.10.0/doc/lz4_Block_format.md),
for token/length/offset and final-literal constraints; the bounded encoder/decoder
are original implementations. Thanks to L. Peter Deutsch,
[RFC 1952, section 2.3.1](https://www.rfc-editor.org/rfc/rfc1952), for reflected
IEEE CRC32 semantics; this format does not use GZIP framing. Thanks to Microsoft,
[*Naming Files, Paths, and Namespaces*](https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file),
and the Python Software Foundation, [*unicodedata*](https://docs.python.org/3/library/unicodedata.html),
for the publishing checks. Preserve the GPG container/layout attribution below;
no cited implementation code was copied.

## Reference review and design revision

Reviewed after the baseline above, using the repository's
[reference index](../../references/game-dev-gems-toc.md). These are design inputs,
not evidence that historical performance results transfer to today's hardware.

| Article actually reviewed | Useful contribution | Decision/revision |
| --- | --- | --- |
| Bruno Sousa, GPG2 1.15, *File Management Using Resource Files*, printed pp. 100-104 (PDF 97-101) | Signature/version, per-entry metadata, separation of container and resource | Keep F3's validated index and explicit codec/version fields; require bounded independent blocks rather than whole-resource decompression or a process-wide current directory. Do not adopt historical encryption examples. |
| Colt McAnlis, GPG7 1.1, *Efficient Cache Replacement Using the Age and Cost Metrics*, printed pp. 5-14 (PDF 38-47) | Replacement needs usage history and reconstruction cost; test oversubscribed working sets | Add trace replay comparing LRU with measured cost-aware policies. Keep this in Content residency; no mandatory second byte cache or per-frame filesystem scan. |
| David L. Koenig, GPG6 1.9, *Faster File Loading with Access-Based File Reordering*, printed pp. 103-108 (PDF 106-111; scanned pages visually inspected) | Capture accesses, reorder the pack, then rerun; caching and alternate gameplay paths can distort gains | F3 builder accepts a deterministic trace-derived layout manifest. Compare startup, level transitions and mod workloads on target SSD/HDD/browser delivery; retain canonical sorted lookup independently of payload placement. |
| Noel Llopis and Charles Nicholson, GPG6 1.10, *Stay in the Game: Asset Hotloading for Fast Iteration*, relevant printed pp. 109-114 (PDF 112-117; visually inspected) | Converter/monitor/listener/rebinding boundaries and resource indirection | F5 watchers observe completed cooked output, debounce duplicate hints and trigger validation. Content publishes replacement handles at a controlled frame boundary, keeps the prior revision on failure and budgets overlapping residency. Filesystem never rewrites active readers. |
| Neil Gower, GPG8 5.3, *Asynchronous I/O for Scalable Game Servers*, printed pp. 506-513 (PDF 521-528) | Queue boundary, buffer/control-structure lifetime, asynchronous cancellation, small-request overhead | F4 uses an explicit request state machine and drain-before-release shutdown; compatible range coalescing remains a measured follow-up. Networking examples inform ownership; they do not establish modern disk throughput. |

F3 payload layout is decoupled from index order. Layout changes do not alter
resource identity. Trace-derived grouping is an optional offline optimization;
record its input/version and test scenarios beyond the training trace. Existing
asset cooking and catalog identity remain Content responsibilities. F5 reloads
follow prepare -> validate -> publish -> retire; failure retains the previous
asset, and GPU/audio retirement respects the owning subsystem's fence/lifetime.

F4 states are Queued -> Submitted -> Completing -> Completed; cancellation is
an intent on a live request, with an eventual Cancelled or completed result.
An accepted request keeps its file revision, destination and completion record
alive. Shutdown stops admission, cancels queued work and drains submitted work
before freeing storage. Coalescing must preserve provider/revision, destination
layout, limits and deadlines. Measure backend crossover rather than always
routing small cached reads asynchronously.

Modern OS contracts were checked against primary documentation:
[pread](https://man7.org/linux/man-pages/man2/pread.2.html) supplies independent
offsets and permits short reads; [open](https://man7.org/linux/man-pages/man2/open.2.html)
explains descriptor revision lifetime and final-component O_NOFOLLOW semantics;
[close](https://man7.org/linux/man-pages/man2/close.2.html) explains Linux's
non-retry rule. The per-component walk is necessary because final-component
O_NOFOLLOW alone does not protect ancestors. Future stronger Linux containment
can use [openat2](https://man7.org/linux/man-pages/man2/openat2.2.html) behind a
separate stated policy; do not silently claim its guarantees for this walk.
[Windows cancellation](https://learn.microsoft.com/en-us/windows/win32/fileio/canceling-pending-i-o-operations)
also requires waiting for terminal completion before buffer reuse.

## F4 bounded asynchronous reads

`async.hpp` adds host-owned `AsyncReader` above `VirtualFile::ReadAt`. The module
still depends only on FoundationBase among engine modules. Private fallible native
thread/condition boundaries create a dedicated blocking pool on Linux, macOS and
Windows. This pool deliberately does not run on FoundationThreading: that module's
job callbacks must not block on I/O. Windows supports memory/pack providers; native
directory I/O still reports Unsupported. Browser initialization explicitly reports
Unsupported without starting threads or allocating scheduler storage.

Initialize allocates a fixed slot array and worker array; optional tracing adds
fixed per-slot diagnostic keys and a bounded trace ring. Admission does not grow
storage or duplicate descriptors. Requests retain the exact opened file and its
mount generation. Capacity and full destination-byte budgets cover queued,
submitted **and uncollected** completions. A host that stops polling therefore
receives overload errors instead of an unbounded completion backlog. Rejected
requests preserve handles and buffers. Separate budgets remain necessary for pack
scratch, provider storage, decoded resources, caches and GPU uploads.

Workers transition Queued -> Submitted -> Completing; only host `Poll` publishes
Completed and releases the slot, byte budget and revision. Poll returns exactly one
record per accepted request while the scheduler is retained; collected identities
cannot affect a reused slot or another scheduler. File release occurs outside the
scheduler gate. Completion delivery invokes no callbacks. Destinations remain
host-owned and exclusively writable until their completion is collected. Submit
cannot detect overlapping destinations; the host must not admit them.

Cancellation is an intent, not buffer-release permission. Queued cancellation
skips I/O. Submitted cancellation drains the blocking read, then returns Cancelled
with zero valid bytes (discard a possibly touched buffer). Cancellation after
terminal publication leaves the completed read result intact. Shutdown permanently
stops admission, cancels queued requests, wakes sleepers and joins submitted reads;
completions remain available to Poll. A stuck provider can delay shutdown without
bound. Providers must eventually return. A provider calling Shutdown on its own
reader receives InsideRead without stopping admission, avoiding self-join. Reader
destruction must remain with the host, outside any active provider ReadAt. Destruction performs the same drain and
then discards any records the host chose not to collect; buffer storage must
outlive destruction on this abandonment path. Preferred lifecycle is shutdown,
collect every completion, then destroy. No asynchronous buffers belong on a short-
lived stack frame unless that frame drains them before returning.

DeadlineNanoseconds uses the monotonic `AsyncNow` clock and is a **latest-start**
deadline: expired queued work skips I/O; an active read is never interrupted by a
deadline. Before the starvation threshold, earlier deadlines sort first, then
higher priority, then FIFO admission sequence. Once any request reaches the
configured queue age, the oldest queued request overrides those preferences.
This prevents priority starvation when providers make progress; it is not a hard
latency guarantee for arbitrary blocking storage. No coalescing or native
io_uring/IOCP backend is enabled without measured evidence and a compatibility
proof for revision, destination layout, budgets and deadlines.

Metrics are mutex-consistent gauges and saturating totals: queue/submitted/backlog,
retained and peak bytes/requests, valid bytes, read/cancel/expiry counts, rejection,
oldest queue age and trace overflow. Completions report queue, service, admission-
to-host-delivery and cancellation-to-host-delivery nanoseconds. Optional traces copy
a host-supplied logical key plus mount/generation, offset, requested bytes and
terminal/native result; this diagnostic key does not select a different file.
Trace overflow drops the oldest record and increments DroppedTraces, never logs or
allocates. Clock samples and mutex acquisition are included in scheduler overhead.

See the [filesystem guide](../wiki/guides/filesystem.md) for host integration and
[measurement procedure](../development/filesystem-benchmark.md) for reproducible
mixed loose/raw/LZ4 workloads and the measured synchronous baseline. These are
storage delivery measurements, not resource decode or GPU residency claims.

## Dependency and ownership map

```mermaid
flowchart TD
    Host[Host owns roots and mount snapshots] --> FS[FoundationFilesystem: directories, mounts, packs and file revisions]
    Content[Content: IDs, schemas, hashes, decode and residency] --> FS
    FS --> Base[FoundationBase: types and compiler vocabulary]
    Stream[Audio stream adapter: independent cursor] --> Content
    Async[Host-owned bounded async reader and dedicated I/O workers] --> FS
```

F1 does not change catalogs, hashes, save APIs or logging sink dependencies.
A Content read still publishes its output only after success; native missing
ancestors now report NotFound, empty files can be streamed, and in-place
mutation checking also covers a change during the read. DEL is rejected as a
control character alongside the existing invalid-path cases.
