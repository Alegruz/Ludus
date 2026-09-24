#include "internal/recorder.hpp"

#include <ludus/foundation/profiling/clock.hpp>

#include <cstdlib>

namespace ludus::foundation::profiling::internal
{

std::atomic<bool> gCaptureActive{false};

// -----------------------------------------------------------------------------
// MVP collector topology (documented deviation from the design's §23 diagram).
//
// The final design shows a dedicated collector thread draining producers
// continuously. For Phase 1 (explicit capture sessions, not the continuous ring
// / hitch trigger of Phase 3) a background thread buys nothing and adds join /
// lifetime surface. So the MVP drains SYNCHRONOUSLY: producers still hand full
// chunks to a lock-free stack during capture, and EndCapture() (or the exporter)
// drains that stack on the calling thread. The producer hot path is identical to
// the final design; only the (off-hot-path) drain is simplified. The dedicated
// collector thread lands with Phase 3, where continuous rolling capture needs
// it. This preserves every hot-path property the gates measure.
// -----------------------------------------------------------------------------

TraceRecorder& TraceRecorder::Instance() noexcept
{
    static TraceRecorder instance;
    return instance;
}

ThreadRecorder& CurrentThreadRecorder() noexcept
{
    thread_local ThreadRecorder recorder;
    return recorder;
}

ThreadRecorder::~ThreadRecorder() noexcept
{
    // Thread destruction: flush any partial chunk so its events are not lost
    // (§21). The recorder owns the chunk memory, so we only publish, never free.
    // A chunk from a stale generation points into freed storage — drop it.
    if (Current != nullptr)
    {
        if (Generation == TraceRecorder::Instance().Generation())
        {
            TraceRecorder::Instance().RecycleOrPublishOnThreadExit(Current);
        }
        Current = nullptr;
    }
}

} // namespace ludus::foundation::profiling::internal

// The thread-exit helper is declared out-of-line to keep the header lean.
namespace ludus::foundation::profiling::internal
{

TraceChunk* TraceRecorder::AcquireChunk(uint32 threadId) noexcept
{
    TraceChunk* chunk = mFreeList.Pop();
    if (chunk == nullptr)
    {
        return nullptr; // pool exhausted -> caller drops + counts
    }
    chunk->ThreadId = threadId;
    chunk->Count = 0;
    return chunk;
}

void TraceRecorder::PublishChunk(TraceChunk* chunk) noexcept
{
    mFullChunks.Push(chunk);
}

void TraceRecorder::RecycleOrPublishOnThreadExit(TraceChunk* chunk) noexcept
{
    // If it holds events, publish for draining; otherwise return it to the pool.
    if (chunk->Count > 0)
    {
        mFullChunks.Push(chunk);
    }
    else
    {
        mFreeList.Push(chunk);
    }
}

bool TraceRecorder::BeginCapture(usize maxChunks) noexcept
{
    if (gCaptureActive.load(std::memory_order_acquire))
    {
        return false;
    }
    if (maxChunks == 0)
    {
        maxChunks = 1;
    }

    // Pre-reserve the entire pool up front so nothing allocates on the hot path
    // (§21). One contiguous array; chunks are threaded onto the free list.
    if (mPoolStorage == nullptr || mPoolCapacity != maxChunks)
    {
        Reset();
        // Overflow-safe: TraceChunk is ~96 KiB; maxChunks is a small bounded
        // count. Use calloc so the (large) Events arrays start zeroed.
        void* raw = std::calloc(maxChunks, sizeof(TraceChunk));
        if (raw == nullptr)
        {
            return false; // OOM: leave capture disabled, engine runs normally.
        }
        mPoolStorage = static_cast<TraceChunk*>(raw);
        mPoolCapacity = maxChunks;
        for (usize i = 0; i < maxChunks; ++i)
        {
            TraceChunk* chunk = new (&mPoolStorage[i]) TraceChunk();
            mFreeList.Push(chunk);
        }
    }

    mEventsRecorded.store(0, std::memory_order_relaxed);
    mEventsDropped.store(0, std::memory_order_relaxed);
    // New generation: any ThreadRecorder::Current from a prior capture now points
    // into freed/reused storage and will be discarded on next touch.
    mGeneration.fetch_add(1, std::memory_order_acq_rel);
    gCaptureActive.store(true, std::memory_order_release);
    return true;
}

void TraceRecorder::EndCapture() noexcept
{
    // Disarm first so producers stop acquiring new chunks.
    gCaptureActive.store(false, std::memory_order_release);
    // Note: partial chunks still held by live producer threads are flushed lazily
    // (on their next Emit after re-arm, on thread exit, or by the exporter which
    // calls FlushAllForExport). For a single-shot capture the common pattern is
    // that the capturing thread has already closed its scopes.
}

uint32 TraceRecorder::RegisterThread(std::string_view name) noexcept
{
    ThreadRecorder& tr = CurrentThreadRecorder();
    if (tr.Registered)
    {
        return tr.ThreadId;
    }
    const uint32 id = mNextThreadId.fetch_add(1, std::memory_order_relaxed);
    tr.ThreadId = id;
    tr.Registered = true;

    // Store the name in the bounded table. Claim a slot with a relaxed CAS-free
    // fetch_add; this runs once per thread, off the hot path.
    const uint32 slot = mThreadNameCount.fetch_add(1, std::memory_order_relaxed);
    if (slot < kMaxThreads)
    {
        ThreadNameEntry& entry = mThreadNames[slot];
        entry.Id = id;
        const usize n = name.size() < kThreadNameCap ? name.size() : kThreadNameCap;
        for (usize i = 0; i < n; ++i)
        {
            entry.Name[i] = name[i];
        }
        entry.Len = static_cast<uint16>(n);
    }
    else
    {
        // Table full: keep the count from growing unbounded.
        mThreadNameCount.store(kMaxThreads, std::memory_order_relaxed);
    }
    return id;
}

std::string_view TraceRecorder::ThreadName(uint32 threadId) const noexcept
{
    const uint32 count = mThreadNameCount.load(std::memory_order_relaxed);
    const uint32 limit = count < kMaxThreads ? count : kMaxThreads;
    for (uint32 i = 0; i < limit; ++i)
    {
        if (mThreadNames[i].Id == threadId)
        {
            return std::string_view(mThreadNames[i].Name, mThreadNames[i].Len);
        }
    }
    return std::string_view{};
}

usize TraceRecorder::ThreadCount() const noexcept
{
    const uint32 count = mThreadNameCount.load(std::memory_order_relaxed);
    return count < kMaxThreads ? count : kMaxThreads;
}

void TraceRecorder::RegisterSite(uint32 siteId, std::string_view name, std::string_view file, uint32 line) noexcept
{
    // Called at most once per site (caller-guarded). Bounded table; a genuinely
    // new id past capacity is dropped from the name table (events still record,
    // the exporter falls back to the numeric id).
    const uint32 slot = mSiteCount.fetch_add(1, std::memory_order_relaxed);
    if (slot < kMaxSites)
    {
        SiteEntry& entry = mSites[slot];
        entry.SiteId = siteId;
        entry.Name = name;
        entry.File = file;
        entry.Line = line;
    }
    else
    {
        mSiteCount.store(kMaxSites, std::memory_order_relaxed);
    }
}

std::string_view TraceRecorder::SiteName(uint32 siteId) const noexcept
{
    const uint32 count = mSiteCount.load(std::memory_order_acquire);
    const uint32 limit = count < kMaxSites ? count : kMaxSites;
    for (uint32 i = 0; i < limit; ++i)
    {
        if (mSites[i].SiteId == siteId)
        {
            return mSites[i].Name;
        }
    }
    return std::string_view{};
}

void TraceRecorder::UnregisterThread() noexcept
{
    ThreadRecorder& tr = CurrentThreadRecorder();
    if (tr.Current != nullptr)
    {
        if (tr.Generation == mGeneration.load(std::memory_order_acquire))
        {
            RecycleOrPublishOnThreadExit(tr.Current);
        }
        tr.Current = nullptr;
    }
    tr.Registered = false;
}

bool TraceRecorder::Emit(const TraceEvent& event) noexcept
{
    ThreadRecorder& tr = CurrentThreadRecorder();

    // Fast reject when no capture is active — already checked by the inline
    // wrapper, re-checked here for direct callers.
    if (!gCaptureActive.load(std::memory_order_relaxed))
    {
        return false;
    }

    // Discard a Current left over from a previous capture generation: its pool
    // may have been reallocated, so we must not publish or write through it.
    const uint32 generation = mGeneration.load(std::memory_order_acquire);
    if (tr.Generation != generation)
    {
        tr.Current = nullptr;
        tr.Generation = generation;
    }

    if (tr.Current == nullptr || tr.Current->Count >= kEventsPerChunk)
    {
        if (tr.Current != nullptr)
        {
            PublishChunk(tr.Current);
        }
        tr.Current = AcquireChunk(tr.ThreadId);
        if (tr.Current == nullptr)
        {
            mEventsDropped.fetch_add(1, std::memory_order_relaxed);
            return false; // pool exhausted: drop + count (§21 overflow policy)
        }
    }

    tr.Current->Events[tr.Current->Count] = event;
    ++tr.Current->Count;
    mEventsRecorded.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void TraceRecorder::FlushAllForExport() noexcept
{
    // Publish the calling thread's partial chunk so a single-threaded capture is
    // fully drained at export time. Other threads' partials are published on
    // their own next Emit/exit; a capture that spans threads should EndCapture
    // after those threads have quiesced (documented contract).
    ThreadRecorder& tr = CurrentThreadRecorder();
    if (tr.Current == nullptr)
    {
        return;
    }
    if (tr.Generation != mGeneration.load(std::memory_order_acquire))
    {
        // Stale chunk from a prior capture; storage may be gone. Drop the pointer.
        tr.Current = nullptr;
        return;
    }
    if (tr.Current->Count > 0)
    {
        PublishChunk(tr.Current);
    }
    else
    {
        mFreeList.Push(tr.Current);
    }
    tr.Current = nullptr;
}

TraceChunk* TraceRecorder::DrainFullChunks() noexcept
{
    return mFullChunks.PopAll();
}

void TraceRecorder::RecycleChunk(TraceChunk* chunk) noexcept
{
    chunk->Count = 0;
    mFreeList.Push(chunk);
}

TraceHealth TraceRecorder::Health() const noexcept
{
    TraceHealth health;
    health.EventsRecorded = mEventsRecorded.load(std::memory_order_relaxed);
    health.EventsDropped = mEventsDropped.load(std::memory_order_relaxed);
    health.ThreadsRegistered = ThreadCount();
    return health;
}

void TraceRecorder::Reset() noexcept
{
    // Only safe with capture inactive and no producer holding a live chunk.
    // Clear the intrusive stack heads FIRST: every node they hold points into
    // mPoolStorage, so we must not walk PoolNext after the storage is freed.
    // Since all chunks live in the one contiguous array, dropping the heads is
    // sufficient — there is nothing else to reclaim.
    mFreeList.Reset();
    mFullChunks.Reset();
    if (mPoolStorage != nullptr)
    {
        for (usize i = 0; i < mPoolCapacity; ++i)
        {
            mPoolStorage[i].~TraceChunk();
        }
        std::free(mPoolStorage);
        mPoolStorage = nullptr;
        mPoolCapacity = 0;
    }
}

} // namespace ludus::foundation::profiling::internal
