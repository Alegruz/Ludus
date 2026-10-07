# Read assets through the filesystem

Link `Ludus::FoundationFilesystem` and include the API you use: `filesystem.hpp`
for native directories/files, `namespace.hpp` for logical roots and mounts,
`pack.hpp` for shipping packs, and `async.hpp` for bounded background reads.
Native directory reads work on Linux and macOS. Memory and pack providers work
on every target. AsyncReader has a dedicated blocking pool on Linux, macOS and
Windows; browser initialization returns Unsupported. Browser hosts currently use
synchronous memory/pack reads and own any fetch adapter themselves.

## Publish a read namespace

Create a directory provider for your trusted authoring root, or copy entries
into a memory provider. For shipping, run `scripts/pack SOURCE OUTPUT.pack` and
create a pack provider using the directory provider that holds the archive.
Create a `MountSnapshot` with explicit Root, provider, priority and stable nonzero
mount ID, then resolve a validated `VirtualPath` through `OpenRead`. Higher
priority wins; the smaller mount ID breaks ties. Only NotFound falls back to the
next mount. Pack corruption or access errors remain visible.

Use a new snapshot generation when publishing a new mount table. Opened files
retain their prior provider revision after unmount or reload. Publish replacement
asset handles in Content at your controlled synchronization boundary. Filesystem
reads bytes; it does not choose resource identity or replace live game objects.

## Submit bounded background reads

Initialize an `AsyncReader` during host setup with an explicit worker count,
request capacity and byte budget. Use its own workers for blocking storage;
ordinary job callbacks must not block on I/O. Submit an already-open `VirtualFile`
with an offset and a caller-owned destination. The scheduler retains that file
without reopening its pathname. Keep the destination alive and exclusively
writable until Poll returns its terminal completion. Live destinations must not
overlap. File ownership can be released immediately after successful admission.

For example, host-owned storage can persist across frame boundaries:

```cpp
#include <ludus/foundation/filesystem/async.hpp>

using namespace ludus::foundation;
using namespace ludus::foundation::filesystem;

struct AssetRead
{
    uint8 Bytes[4096]{}; // Storage outlives Reader, including destructor drain.
    RequestHandle Request;
    AsyncReader Reader;
};

// Host setup, with explicit overload and initialization handling.
AsyncStatus BeginRead(AssetRead& pending, const VirtualFile& file) noexcept
{
    const auto initialized = pending.Reader.Initialize({32, 2, 1024 * 1024, 5'000'000, 0});
    if (initialized != AsyncStatus::Ok) { return initialized; }
    return pending.Reader.Submit(file, 0, pending.Bytes, pending.Request);
}

// Call from your host update loop outside render/audio critical sections.
// True means the destination can now be reused, even when the read failed.
bool FinishRead(AssetRead& pending, ReadCompletion& completion) noexcept
{
    return pending.Reader.Poll(completion);
}
```

Poll delivers each accepted completion once. For `ReadDisposition::Read`, inspect
`completion.Read.Outcome` and `BytesRead`; successful EOF can mean fewer bytes than
the destination length. Changed invalidates potentially touched bytes. For
Cancelled, discard the buffer: a running read may already have written it.
DeadlineExpired means I/O never started. Rejected Submit calls preserve both
output handle and destination, and produce no completion.

CapacityExceeded includes completions you have not collected. ByteBudgetExceeded
charges full destination sizes through collection, even for reads at EOF. Poll
regularly and handle overload by deferring admission or choosing synchronous I/O
where measured small cached reads justify it. File releases during collection can
close a final provider revision, so polling belongs outside critical sections.

## Cancellation, deadlines and shutdown

Cancel records intent; it never permits immediate buffer reuse. A queued read
skips I/O. An active blocking read must return before its Cancelled completion can
be collected. Cancellation after terminal publication keeps the read result.
Stale handles cannot cancel a subsequent request that reuses the slot.

ReadOptions can carry a Tag, priority and an absolute latest-start deadline in
AsyncNow nanoseconds. Earlier deadlines, then higher priority, then FIFO select
work until the oldest request reaches the starvation threshold. Age then wins.
Deadlines do not interrupt active reads and are not a hard completion guarantee.

On shutdown, stop admission with Shutdown, collect all remaining completions,
then destroy the reader and destinations. Shutdown drains active reads and can
block until a provider returns. A provider calling Shutdown on its own reader
receives InsideRead without stopping admission. Keep destruction with the host,
outside active provider reads. Shutdown cancels queued work and preserves all terminal
records for the host. The destructor also drains, but abandons records you did not
collect. Initialize/destruction require exclusive owner access; other operations
are concurrent while the reader lives. Serialize Shutdown calls with each other.

## Inspect metrics and traces

Metrics reports current queue, active reads, delivery backlog, byte/request peaks,
rejections and terminal counts. ReadCompletion records queue/service/delivery and
cancellation latency in nanoseconds. Enable a finite TraceCapacity to collect
ReadTrace records via PollTrace; provide a valid VirtualPath in TracePath when
logical names are useful. The key is copied for diagnostics and does not resolve
the file. Traces include mount/generation, offset, requested bytes and native
errors. Overflow discards oldest records and increments DroppedTraces. No logging
or callbacks run inside the scheduler.

These budgets cover destinations and scheduler records. Set resource residency,
pack decode scratch and GPU upload budgets separately. Persistence and watchers
are planned in F5; this API grants no write permission or durable-save guarantee.
