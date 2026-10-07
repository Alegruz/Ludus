#pragma once

#include <ludus/foundation/base/core.h>
#include <ludus/foundation/filesystem/namespace.hpp>

namespace ludus::foundation::filesystem
{
/// Admission/lifecycle outcome, independent of the eventual read result.
enum class AsyncStatus : uint8
{
    Ok,                   ///< Operation succeeded.
    InvalidArgument,      ///< Invalid configuration, file, range or options.
    NotInitialized,       ///< Initialize has not succeeded.
    AlreadyInitialized,   ///< Shutdown and drain before destroying an initialized scheduler.
    Stopped,              ///< Shutdown has stopped admission.
    OutOfMemory,          ///< Fixed scheduler storage could not be allocated.
    CapacityExceeded,     ///< All request slots are retained, including uncollected completions.
    ByteBudgetExceeded,   ///< Requested destinations exceed the retained byte budget.
    InvalidHandle,        ///< Handle is foreign, stale, or already collected.
    IdentityExhausted,    ///< Unique scheduler or request identities cannot be represented.
    ThreadCreationFailed, ///< Native synchronization or a worker could not start; started workers are joined.
    Unsupported,          ///< Blocking workers are unavailable on this target (including browser builds).
    InsideRead,           ///< Shutdown was called from this reader's provider/worker and would join itself.
};

/// Observable request progression; cancellation is an intent rather than a state.
enum class RequestState : uint8
{
    Queued,     ///< Accepted, waiting for a worker.
    Submitted,  ///< Worker owns the destination and may be reading.
    Completing, ///< Read is terminal; completion awaits host collection.
    Completed,  ///< Host has collected the terminal completion and may reuse the destination.
};

/// Terminal disposition; Read retains the underlying ReadResult, including errors.
enum class ReadDisposition : uint8
{
    Read,            ///< ReadAt finished, or cancellation arrived after it became terminal.
    Cancelled,       ///< Cancellation won before terminal publication; discard any touched destination.
    DeadlineExpired, ///< The latest-start deadline passed before I/O began; destination is untouched.
};

/// Opaque value identity; copying does not retain a request. Never valid across schedulers.
struct RequestHandle final
{
    uint64 Scheduler = 0; ///< Unique scheduler lifetime identity.
    uint64 Sequence = 0;  ///< Non-reused admission sequence in that scheduler.
};

/// Fixed storage and worker budgets. No accepted request allocates scheduler storage.
struct AsyncConfig final
{
    uint32 MaxRequests = 256; ///< Queued + submitted + uncollected completions; range 1..4096.
    uint32 WorkerCount = 2;   ///< Dedicated blocking I/O workers; range 1..64, separate from job workers.
    uint64 MaxBytes = uint64{16} * 1024 * 1024; ///< Sum of full destination lengths until collection; must be nonzero.
    uint64 StarvationNanoseconds =
        uint64{5} * 1000 * 1000; ///< After this queue age, oldest-first overrides priority/deadline; nonzero.
    uint32 TraceCapacity = 0;    ///< Opt-in terminal trace ring, 0..4096; overflow discards oldest records.
};

/// Borrowed request policy; Submit copies values and optionally the trace key.
struct ReadOptions final
{
    int32 Priority = 0; ///< Larger values first, after deadline ordering; FIFO breaks ties.
    uint64 DeadlineNanoseconds =
        0;          ///< Latest I/O start in AsyncNow's clock; zero means none. Never interrupts running I/O.
    uint64 Tag = 0; ///< Host correlation value copied unchanged to completion and trace.
    const VirtualPath* TracePath =
        nullptr; ///< Optional valid diagnostic key copied when tracing is enabled; does not select the file.
};

/// Host-delivered terminal record. Exactly one Poll succeeds for each accepted handle.
struct ReadCompletion final
{
    RequestHandle Handle;                                ///< Accepted identity, invalid after collection.
    RequestState State = RequestState::Completed;        ///< Terminal host-delivery state.
    ReadDisposition Disposition = ReadDisposition::Read; ///< Whether ReadAt ran to a published result.
    ReadResult Read;             ///< Valid progress for Read; zero valid bytes for cancellation/expiry.
    uint64 Tag = 0;              ///< Copied host correlation value.
    uint64 MountId = 0;          ///< Retained file's mount identity.
    uint64 Generation = 0;       ///< Retained file's snapshot generation.
    uint64 QueueNanoseconds = 0; ///< Admission to submission/terminal skip.
    uint64 ServiceNanoseconds =
        0; ///< Submission to terminal publication, including read/gate contention; zero for skips.
    uint64 CompletionNanoseconds =
        0; ///< Admission to collection under the gate, including backlog; excludes final file release.
    uint64 CancellationNanoseconds = 0; ///< First cancellation intent to collection, zero if not cancelled.
};

/// Bounded opt-in read trace; no logging, callbacks, or pathname lookup during delivery.
struct ReadTrace final
{
    ReadCompletion Completion; ///< Terminal result and timing.
    VirtualPath Path;          ///< Copied diagnostic key, invalid when the host supplied none.
    uint64 Offset = 0;         ///< Requested byte offset.
    uint64 RequestedBytes = 0; ///< Full destination size in bytes.
};

/// Mutex-consistent counters. Totals saturate at uint64's maximum; live gauges are exact.
struct AsyncMetrics final
{
    uint64 Accepted = 0;               ///< Accepted since initialization.
    uint64 Rejected = 0;               ///< Rejected admissions since initialization.
    uint64 Reads = 0;                  ///< Terminal published Read dispositions, including failed reads.
    uint64 Cancelled = 0;              ///< Terminal cancellations.
    uint64 Expired = 0;                ///< Missed latest-start deadlines.
    uint64 BytesRead = 0;              ///< Valid published progress.
    uint64 BytesInFlight = 0;          ///< Full retained destinations, including completion backlog.
    uint64 PeakBytes = 0;              ///< Maximum BytesInFlight.
    uint32 InFlight = 0;               ///< All uncollected accepted requests.
    uint32 PeakRequests = 0;           ///< Maximum InFlight.
    uint32 Queued = 0;                 ///< Waiting requests.
    uint32 Submitted = 0;              ///< Active blocking reads.
    uint32 Completing = 0;             ///< Awaiting host collection.
    uint64 OldestQueueNanoseconds = 0; ///< Current oldest queue age, zero when empty.
    uint64 DroppedTraces = 0;          ///< Trace ring records overwritten before PollTrace.
};

/// Monotonic nanoseconds for scheduler deadlines and measurement; unrelated to wall-clock time.
[[nodiscard]] uint64 AsyncNow() noexcept;

// Thanks to Neil Gower, "Asynchronous I/O for Scalable Game Servers", Game
// Programming Gems 8, sec. 5.3, pp. 506-513: queue/control-buffer lifetime and
// drain-before-release cancellation inform this original implementation.
// Networking examples are not disk-performance evidence. See docs/architecture/filesystem.md.
/// Host-owned bounded offset-read scheduler. Native blocking pool on Linux/macOS/Windows;
/// browser initialization returns Unsupported. Dedicated workers may block; never call
/// Submit/Poll/Shutdown from an audio callback or render critical section.
/// Initialize/destruction are owner-thread operations exclusive of all other calls.
/// Submit, Cancel, GetState, Poll, PollTrace and Metrics may run concurrently while alive.
/// Shutdown is serialized with itself and destruction, and may overlap those other calls.
/// The host keeps each accepted destination alive and exclusively writable until Poll
/// returns its completion, or until destruction has drained all I/O. No completion callbacks.
/// Stop with Shutdown, collect every completion, then destroy. Destruction also drains,
/// but deliberately abandons any completion the host chose not to collect.
class AsyncReader final
{
public:
    /// Creates an uninitialized scheduler.
    AsyncReader() noexcept = default;
    /// Stops admission, cancels queued reads, joins workers, and releases abandoned records.
    ~AsyncReader() noexcept;
    /// Scheduler identity and native synchronization are immovable.
    AsyncReader(const AsyncReader&) = delete;
    /// Scheduler identity and native synchronization are immovable.
    AsyncReader& operator=(const AsyncReader&) = delete;
    /// Scheduler identity and native synchronization are immovable.
    AsyncReader(AsyncReader&&) = delete;
    /// Scheduler identity and native synchronization are immovable.
    AsyncReader& operator=(AsyncReader&&) = delete;
    /// Allocates fixed storage and starts all workers; failure leaves this uninitialized.
    [[nodiscard]] AsyncStatus Initialize(AsyncConfig config = {}) noexcept;
    /// Retains file without opening/cloning descriptors. Charges full destination bytes until
    /// collection, including EOF and empty reads. Offset must be <= file.Size(); no overlapping
    /// live destinations are permitted (host responsibility). Rejection preserves handle and
    /// destination. TracePath, when provided, must be valid even if tracing is disabled.
    [[nodiscard]] AsyncStatus Submit(const VirtualFile& file,
                                     uint64 offset,
                                     std::span<uint8> destination,
                                     RequestHandle& handle,
                                     ReadOptions options = {}) noexcept;
    /// Records first cancellation intent. Queued reads skip I/O; running reads drain and report
    /// Cancelled with zero valid bytes. A Completing request keeps its already published result.
    /// Success never permits immediate buffer reuse; stale/foreign handles return InvalidHandle.
    [[nodiscard]] AsyncStatus Cancel(RequestHandle handle) noexcept;
    /// Inspects a live request; failure preserves output. Collected handles are invalid.
    [[nodiscard]] AsyncStatus GetState(RequestHandle handle, RequestState& output) const noexcept;
    /// Collects one terminal request in terminal-publication order, releases its budget/file,
    /// and returns true. False preserves output. No payload reads, completion callbacks or scheduler allocations; file
    /// release may close the final provider revision, so collect outside critical engine sections.
    [[nodiscard]] bool Poll(ReadCompletion& output) noexcept;
    /// Removes the oldest trace without allocation; false preserves output.
    [[nodiscard]] bool PollTrace(ReadTrace& output) noexcept;
    /// Copies counters; failure preserves output. No external tracing/logging occurs.
    [[nodiscard]] AsyncStatus Metrics(AsyncMetrics& output) const noexcept;
    /// Permanently stops admission, cancels queued requests and joins all submitted reads.
    /// Does not deliver or discard completions: Poll remains usable. Repeated calls succeed.
    /// Can block for an uninterruptible provider read; providers must eventually return.
    /// Returns InsideRead without stopping admission if invoked by this reader's provider
    /// on its I/O worker. Never destroy a reader from a provider it is currently reading.
    [[nodiscard]] AsyncStatus Shutdown() noexcept;

private:
    struct Impl;
    Impl* mImpl = nullptr;
};
} // namespace ludus::foundation::filesystem
