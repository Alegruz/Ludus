#pragma once

// -----------------------------------------------------------------------------
// Trace chunks and the lock-free chunk handoff (final design §6, §8, §23).
//
// Recording is per-thread and single-writer: pushing an event is a plain bump of
// a chunk index with NO atomics on the common path (the buffer is owned by
// exactly one producing thread). Only when a chunk fills (or at frame flip / on
// thread exit) is the whole chunk published to the collector across a bounded
// lock-free MPSC queue — so cross-thread synchronisation is amortised over
// thousands of events rather than paid per event.
//
// Overflow policy is Trace's: DROP the event and count it (never block, never
// allocate, never grow). Memory profiling's different (lossless) contract lives
// elsewhere (§7, C4) and must never reuse this policy.
// -----------------------------------------------------------------------------

#include "internal/trace_event.hpp"

#include <ludus/foundation/base/types.h>

#include <atomic>
#include <new>

namespace ludus::foundation::profiling::internal
{

// Number of events per chunk. 4096 * 24 B = 96 KiB. Tunable; the final value is
// a measurement decision (gate G4/G5) — this is a defensible MVP default.
inline constexpr usize kEventsPerChunk = 4096;

// A fixed-capacity block of events owned by one producer at a time, then handed
// to the collector. Cache-line aligned so producer writes never false-share the
// bookkeeping the collector reads.
struct alignas(64) TraceChunk
{
    // Producer identity stamped when the chunk is acquired, so the collector can
    // attribute events to a thread without a per-event thread id (§10).
    uint32 ThreadId = 0;
    uint32 Count = 0;               // number of valid events (producer-written)
    TraceChunk* PoolNext = nullptr; // free-list / queue linkage (collector-only)
    TraceEvent Events[kEventsPerChunk];
};

// Bounded, lock-free MPSC intrusive stack used twice: producers push FULL chunks
// for the collector to drain, and the collector pushes RECYCLED chunks back to a
// free list. Intrusive (via PoolNext) so no allocation happens on push/pop. This
// is the same "publish a pointer, not per-event" discipline as the logging MPSC
// queue, specialised to pointer-sized items so it is genuinely allocation-free.
class ChunkStack
{
public:
    ChunkStack() noexcept : mHead(nullptr) {}

    // Push (producer or collector). Lock-free, wait-free-ish (bounded CAS retry).
    void Push(TraceChunk* chunk) noexcept
    {
        TraceChunk* head = mHead.load(std::memory_order_relaxed);
        do
        {
            chunk->PoolNext = head;
        } while (!mHead.compare_exchange_weak(head, chunk, std::memory_order_release, std::memory_order_relaxed));
    }

    // Pop one item, or nullptr if empty. Single consumer expected for the
    // "full chunks" stack (the collector); the free list tolerates multiple
    // poppers via the CAS loop.
    [[nodiscard]] TraceChunk* Pop() noexcept
    {
        TraceChunk* head = mHead.load(std::memory_order_acquire);
        while (head != nullptr)
        {
            TraceChunk* next = head->PoolNext;
            if (mHead.compare_exchange_weak(head, next, std::memory_order_acq_rel, std::memory_order_acquire))
            {
                head->PoolNext = nullptr;
                return head;
            }
        }
        return nullptr;
    }

    // Detach the whole stack for draining (collector). Returns the head; the
    // caller walks PoolNext. Order is LIFO; the collector sorts by timestamp.
    [[nodiscard]] TraceChunk* PopAll() noexcept
    {
        return mHead.exchange(nullptr, std::memory_order_acq_rel);
    }

    // Drop the head without walking the chain. Used only when the underlying
    // chunk storage is about to be freed wholesale (TraceRecorder::Reset), where
    // walking PoolNext would touch freed memory.
    void Reset() noexcept
    {
        mHead.store(nullptr, std::memory_order_release);
    }

private:
    std::atomic<TraceChunk*> mHead;
};

} // namespace ludus::foundation::profiling::internal
