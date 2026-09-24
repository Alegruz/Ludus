#pragma once

// -----------------------------------------------------------------------------
// Trace recorder: per-thread recording + capture lifecycle (final design §6,
// §8, §21, §23). Internal — never installed.
//
// Ownership model:
//   * Each producing thread owns a TLS `ThreadRecorder` holding the current
//     chunk. Pushing an event is single-writer, no atomics on the common path.
//   * A process-wide `TraceRecorder` owns the chunk pool (free list), the queue
//     of full chunks, the registered-thread list, and the capture-enabled flag.
//   * Full chunks move producer -> recorder via the lock-free ChunkStack.
//
// Capture lifecycle: BeginCapture() pre-reserves all chunks and flips the
// enabled flag; EndCapture() flushes partial chunks and assembles a Capture.
// Nothing allocates on the hot path; over-capacity events are dropped and
// counted (Trace overflow policy, §21).
// -----------------------------------------------------------------------------

#include "internal/trace_chunk.hpp"
#include "internal/trace_event.hpp"

#include <ludus/foundation/base/types.h>

#include <atomic>
#include <string_view>

namespace ludus::foundation::profiling::internal
{

// Global fast-path flag. Checked first on every emit so that, when no capture is
// active, an enabled-build instrumentation site costs a single relaxed atomic
// load + predicted-not-taken branch and nothing else (§19, gate G2). Defined in
// recorder.cpp.
extern std::atomic<bool> gCaptureActive;

[[nodiscard]] inline bool IsCaptureActive() noexcept
{
    return gCaptureActive.load(std::memory_order_relaxed);
}

// Runtime statistics / health (mirrors the logging LogHealth hooks; §21).
struct TraceHealth
{
    uint64 EventsRecorded = 0;
    uint64 EventsDropped = 0; // over-capacity drops (chunk pool exhausted)
    uint64 ChunksInFlight = 0;
    uint64 ThreadsRegistered = 0;
};

// Process-wide recorder. Single instance accessed via Instance(). Lifetime is
// managed like the logging AsyncBackend: Shutdown() joins/flushes before freeing
// (§21) — for the MVP the collector drain is synchronous in EndCapture(), so
// there is no background thread to join yet (documented deviation; see
// recorder.cpp).
class TraceRecorder
{
public:
    [[nodiscard]] static TraceRecorder& Instance() noexcept;

    // Pre-reserve the chunk pool and arm capture. `maxChunks` bounds total trace
    // memory (maxChunks * sizeof(TraceChunk)). Idempotent-safe: a second call
    // while active is a no-op that returns false.
    bool BeginCapture(usize maxChunks) noexcept;

    // Disarm capture, flush every thread's partial chunk, and return the number
    // of full chunks now queued for draining. After this the events can be
    // assembled into a Capture (see capture.hpp / EndCaptureInto).
    void EndCapture() noexcept;

    // Register/unregister the calling thread. Registration assigns a stable
    // profiling-local thread id and stores the name for export. Safe to call
    // before BeginCapture. Unregister flushes the thread's partial chunk (§21,
    // thread destruction).
    uint32 RegisterThread(std::string_view name) noexcept;
    void UnregisterThread() noexcept;

    // Thread name for a registered id (for export). Empty view if unknown.
    [[nodiscard]] std::string_view ThreadName(uint32 threadId) const noexcept;
    [[nodiscard]] usize ThreadCount() const noexcept;

    // Site registry (§13/§14). RegisterSite records id -> {name,file,line} at
    // most once per site (caller guards with a per-site static). Names are
    // borrowed string_views into process-lifetime string literals, so we store
    // the view directly. SiteName returns empty for an unknown id.
    void RegisterSite(uint32 siteId, std::string_view name, std::string_view file, uint32 line) noexcept;
    [[nodiscard]] std::string_view SiteName(uint32 siteId) const noexcept;

    // Emit one event from the current thread. Returns false if dropped (pool
    // exhausted). Hot path: called via the inline detail::Emit* wrappers.
    bool Emit(const TraceEvent& event) noexcept;

    // Publish the calling thread's partial chunk so a single-threaded capture is
    // fully drained before export (§21). Spanning-thread captures must quiesce
    // their threads before relying on this.
    void FlushAllForExport() noexcept;

    // Thread-exit hook: publish a partial chunk that still holds events, else
    // return it to the free list. Called from ThreadRecorder's destructor.
    void RecycleOrPublishOnThreadExit(TraceChunk* chunk) noexcept;

    // Collector side ---------------------------------------------------------
    // Take ownership of all full chunks (LIFO). Caller walks PoolNext and, when
    // done, returns each chunk via RecycleChunk so the pool can be reused.
    [[nodiscard]] TraceChunk* DrainFullChunks() noexcept;
    void RecycleChunk(TraceChunk* chunk) noexcept;

    [[nodiscard]] TraceHealth Health() const noexcept;

    // Release all pooled memory. Only valid when capture is inactive and no
    // producer holds a chunk (asserted). Mirrors AsyncBackend::Stop discipline.
    void Reset() noexcept;

private:
    TraceRecorder() = default;

    [[nodiscard]] TraceChunk* AcquireChunk(uint32 threadId) noexcept;
    void PublishChunk(TraceChunk* chunk) noexcept;

public:
    // Current capture generation. A ThreadRecorder whose Generation differs owns
    // a Current that points into a (possibly reallocated) previous pool and must
    // not be dereferenced.
    [[nodiscard]] uint32 Generation() const noexcept
    {
        return mGeneration.load(std::memory_order_acquire);
    }

private:
    // Bounded thread-name table (§14/§21: fixed capacity, drop-with-none on
    // overflow — a rare event since threads register once). Guarded by a small
    // mutex used only off the hot path (registration/export), never per event.
    static constexpr usize kMaxThreads = 256;
    static constexpr usize kThreadNameCap = 32;
    struct ThreadNameEntry
    {
        uint32 Id = 0;
        uint16 Len = 0;
        char Name[kThreadNameCap] = {};
    };

    // Site registry entry: borrowed process-lifetime views (string literals /
    // __func__ static arrays).
    static constexpr usize kMaxSites = 4096;
    struct SiteEntry
    {
        uint32 SiteId = 0;
        uint32 Line = 0;
        std::string_view Name;
        std::string_view File;
    };

    ChunkStack mFreeList;               // recycled/empty chunks
    ChunkStack mFullChunks;             // chunks handed to the collector
    TraceChunk* mPoolStorage = nullptr; // contiguous backing array
    usize mPoolCapacity = 0;
    std::atomic<uint64> mEventsRecorded{0};
    std::atomic<uint64> mEventsDropped{0};
    std::atomic<uint32> mGeneration{0}; // bumped on each BeginCapture reservation
    std::atomic<uint32> mNextThreadId{0};
    ThreadNameEntry mThreadNames[kMaxThreads];
    std::atomic<uint32> mThreadNameCount{0};
    SiteEntry mSites[kMaxSites];
    std::atomic<uint32> mSiteCount{0};
};

// Thread-local recorder state. Holds the current chunk for this thread. The
// producer writes here with no synchronisation; when the chunk fills it is
// published and a fresh one acquired.
//
// `Generation` guards against a stale Current surviving across capture sessions:
// BeginCapture bumps the recorder generation, and a Current tagged with an older
// generation is discarded (not touched) rather than written through, because the
// pool it pointed into may have been reallocated (§21 lifetime discipline).
struct ThreadRecorder
{
    TraceChunk* Current = nullptr;
    uint32 ThreadId = 0;
    uint32 Generation = 0;
    bool Registered = false;

    ~ThreadRecorder() noexcept;
};

[[nodiscard]] ThreadRecorder& CurrentThreadRecorder() noexcept;

} // namespace ludus::foundation::profiling::internal
