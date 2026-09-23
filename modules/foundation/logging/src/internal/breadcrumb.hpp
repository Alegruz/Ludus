#pragma once

#include <ludus/foundation/base/types.h>
#include <ludus/foundation/logging/level.hpp>

#include <atomic>
#include <cstring>
#include <string_view>

namespace ludus::foundation::logging::internal
{

// Bounded, producer-side breadcrumb ring (design.md section 10; requirements
// R34). It preserves the most recent selected records so recent context is
// available even if those records never reached a sink (e.g. dropped, or the
// worker was parked). The copy happens on the PRODUCER, because a consumer-only
// history cannot preserve a record that never reached the consumer.
//
// Concurrency contract (the review's explicit warning): a seqlock counter over
// non-atomic payload bytes is a data race under the C++ memory model. This ring
// therefore uses per-slot exclusive-writer ownership plus a publication
// generation, and a reader verifies the generation did not change across its
// read (and that the slot is not mid-write). Writers never block: a writer that
// finds its target slot busy simply advances (skips), so a stalled reader can
// never wall off the ring. A crash reader that observes an odd (in-progress)
// generation skips that slot rather than reading a torn record.
//
// The ring is process-local: it survives termination ONLY if a core/minidump or
// external capture preserves it (documented, not implied). Size and captured
// severity are prototype values here; final coverage is measurement-dependent.
class BreadcrumbRing
{
public:
    static constexpr usize kCapacity = 256;   // prototype (spec CD3)
    static constexpr usize kMessageCap = 200; // bounded copied prefix

    // Message is stored in atomic 64-bit words so a concurrent reader never
    // performs a non-atomic read racing a writer's non-atomic write. A seqlock
    // generation over PLAIN bytes would still be a data race under the C++
    // memory model (the spec's explicit warning); atomic payload words make the
    // concurrent access legal (requirements R34).
    static constexpr usize kMessageWords = (kMessageCap + 7) / 8;

    struct Slot
    {
        // Even generation = stable/published; odd = write in progress. A reader
        // accepts a slot only if it reads the same even generation before and
        // after copying the payload (requirements R34).
        std::atomic<uint64> Generation{0};
        std::atomic<uint64> Sequence{0};
        std::atomic<uint64> MonotonicTicks{0};
        std::atomic<uint32> CategoryId{0};
        std::atomic<LogLevel> Level{LogLevel::Info};
        std::atomic<uint16> MessageLen{0};
        std::atomic<uint64> MessageWords[kMessageWords]{};
    };

    // Record a breadcrumb. Exclusive-writer per slot via an odd/even generation
    // fence; never blocks. Called on the producer, after the record's text is
    // final. The single internal caller passes each field explicitly.
    void Push(uint64 sequence,
              uint64 monotonicTicks, // NOLINT(bugprone-easily-swappable-parameters)
              uint32 categoryId,
              LogLevel level,
              std::string_view message) noexcept
    {
        const usize index = mNext.fetch_add(1, std::memory_order_relaxed) % kCapacity;
        Slot& slot = mSlots[index];

        // Enter write: bump generation to odd (write-in-progress) with acquire so
        // a concurrent reader sees the in-progress marker.
        const uint64 gen = slot.Generation.load(std::memory_order_relaxed);
        slot.Generation.store(gen + 1, std::memory_order_release);
        std::atomic_thread_fence(std::memory_order_acquire);

        slot.Sequence.store(sequence, std::memory_order_relaxed);
        slot.MonotonicTicks.store(monotonicTicks, std::memory_order_relaxed);
        slot.CategoryId.store(categoryId, std::memory_order_relaxed);
        slot.Level.store(level, std::memory_order_relaxed);
        const usize n = message.size() < kMessageCap ? message.size() : kMessageCap;
        // Pack bytes into atomic words (relaxed: the generation release/acquire
        // pair provides the ordering; the atomics make the access race-free).
        for (usize w = 0; w < kMessageWords; ++w)
        {
            uint64 word = 0;
            for (usize b = 0; b < 8; ++b)
            {
                const usize idx = w * 8 + b;
                if (idx < n)
                {
                    word |= static_cast<uint64>(static_cast<unsigned char>(message[idx])) << (b * 8);
                }
            }
            slot.MessageWords[w].store(word, std::memory_order_relaxed);
        }
        slot.MessageLen.store(static_cast<uint16>(n), std::memory_order_relaxed);

        // Publish: bump to the next even generation with release.
        slot.Generation.store(gen + 2, std::memory_order_release);
    }

    // Snapshot copy for a reader (tests / crash reader). Returns the number of
    // stable slots copied into `out` (most recent first is NOT guaranteed; caller
    // may sort by Sequence). A slot with an odd/changing generation is skipped
    // (never read torn). This is a best-effort in-process reader.
    struct Entry
    {
        uint64 Sequence;
        uint64 MonotonicTicks;
        uint32 CategoryId;
        LogLevel Level;
        uint16 MessageLen;
        char Message[kMessageCap];
    };

    usize Snapshot(Entry* out, usize outCapacity) const noexcept
    {
        usize count = 0;
        for (usize i = 0; i < kCapacity && count < outCapacity; ++i)
        {
            const Slot& slot = mSlots[i];
            const uint64 g0 = slot.Generation.load(std::memory_order_acquire);
            if (g0 == 0 || (g0 & 1u) != 0u)
            {
                continue; // never written, or a write is in progress
            }
            Entry entry;
            entry.Sequence = slot.Sequence.load(std::memory_order_relaxed);
            entry.MonotonicTicks = slot.MonotonicTicks.load(std::memory_order_relaxed);
            entry.CategoryId = slot.CategoryId.load(std::memory_order_relaxed);
            entry.Level = slot.Level.load(std::memory_order_relaxed);
            const uint16 len = slot.MessageLen.load(std::memory_order_relaxed);
            const usize n = len < kMessageCap ? len : kMessageCap;
            // Read atomic words back into bytes (race-free even if a writer is
            // concurrently storing; the generation recheck below decides accept).
            for (usize w = 0; w < kMessageWords; ++w)
            {
                const uint64 word = slot.MessageWords[w].load(std::memory_order_relaxed);
                for (usize b = 0; b < 8; ++b)
                {
                    const usize idx = w * 8 + b;
                    if (idx < n)
                    {
                        entry.Message[idx] = static_cast<char>((word >> (b * 8)) & 0xFFu);
                    }
                }
            }
            entry.MessageLen = static_cast<uint16>(n);
            std::atomic_thread_fence(std::memory_order_acquire);
            const uint64 g1 = slot.Generation.load(std::memory_order_acquire);
            if (g1 != g0)
            {
                continue; // changed under us: skip rather than accept a torn read
            }
            out[count++] = entry;
        }
        return count;
    }

private:
    std::atomic<usize> mNext{0};
    Slot mSlots[kCapacity];
};

} // namespace ludus::foundation::logging::internal

namespace ludus::foundation::logging
{
// Test/diagnostic accessor: snapshot the process breadcrumb ring. Defined in
// logger.cpp. Not part of the installed SDK.
usize SnapshotBreadcrumbs(internal::BreadcrumbRing::Entry* out, usize capacity) noexcept;
} // namespace ludus::foundation::logging
