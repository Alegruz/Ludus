#pragma once

#include <ludus/foundation/base/pointer.hpp>
#include <ludus/foundation/base/types.h>
#include <ludus/foundation/containers/vector.hpp>

#include "internal/mpsc_queue.hpp"
#include "internal/sink.hpp"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

// This is a plain class, not a PIMPL. The concurrency members (std::thread,
// std::mutex, std::condition_variable) and the owned sink vector are visible
// here; their method DEFINITIONS live in backend.cpp so the header stays free of
// heavy inline bodies. These standard members necessarily pull <thread>/
// <condition_variable> into the header, which drag in <format> on libstdc++;
// that is a real cost, but backend.hpp has a single consumer (logger.cpp) which
// already includes those headers for its own synchronous backend, so hiding them
// behind a PIMPL would not reduce that TU's build time. The build-time budget
// carries a documented per-header override for this header instead (see
// config/build_budget.json / ADR 0005).
//
// The owned sink list is Ludus::Vector<UniquePtr<ILogSink>> (migrated from
// std::vector<std::unique_ptr>): it drops the <memory>/<vector> includes and
// their transitive <format> pull, and matches the engine container policy.

namespace ludus::foundation::logging::internal
{

// Asynchronous backend: one worker thread OWNS the ordinary sinks and drains a
// bounded MPSC queue (design.md sections 7, 8; requirements R35-R40).
//
// Ownership: producers never touch sinks. The worker is the sole owner of the
// sink vector between Start() and Stop(); it flushes/closes on Stop(). A flush
// request travels on a SEPARATE control channel (a small mutex-guarded fence) so
// a full data queue can never prevent requesting a flush (requirements R29).
class AsyncBackend
{
public:
    static constexpr usize kCapacity = 4096; // prototype (spec CD3)
    using Queue = MpscQueue<kCapacity>;

    // Takes ownership of the sinks and starts the worker. The worker becomes the
    // sole owner/user of the sinks until Stop().
    void Start(foundation::Vector<foundation::UniquePtr<ILogSink>>&& sinks, uint32 flushIntervalMs) noexcept;

    // Producer: enqueue an owned record. Returns false if dropped (queue full /
    // contention). Never blocks. Wakes the worker.
    [[nodiscard]] bool Enqueue(const QueuedRecord& record) noexcept;

    struct FlushOutcome
    {
        bool Ok = false;
        bool TimedOut = false;
        uint64 AcknowledgedPos = 0;
    };

    // Capture a flush fence at the current enqueue position and wait (bounded)
    // for the worker to process through it and flush the requested kind. A
    // timeout is honest: it reports incomplete rather than cancelling a blocked
    // OS write (requirements R28/R29).
    FlushOutcome Flush(bool durable, uint32 timeoutMs) noexcept;

    // Stop: drain, flush, close, join. Returns false (RETAINING all storage) if
    // the worker cannot be joined within the deadline, so a running worker never
    // references freed storage (requirements R40).
    [[nodiscard]] bool Stop(uint32 joinTimeoutMs) noexcept;

    [[nodiscard]] uint64 Dropped() const noexcept;
    [[nodiscard]] uint64 Written() const noexcept;

private:
    void RenderAndWrite(const QueuedRecord& rec) noexcept;
    void DrainBatch() noexcept;
    void ServiceFlush() noexcept;
    void Run() noexcept;

    Queue mQueue;
    foundation::Vector<foundation::UniquePtr<ILogSink>> mSinks; // worker-owned
    std::thread mWorker;

    std::atomic<bool> mStop{false};
    std::atomic<uint64> mPublished{0};
    std::atomic<uint64> mWritten{0};

    // Wakeup channel.
    std::mutex mWakeMutex;
    std::condition_variable mWakeCv;

    // Control channel (flush fence), separate from the data queue. The pending
    // flag is atomic so the wakeup predicate can read it without the control
    // lock; the fence details are guarded by mControlMutex.
    std::mutex mControlMutex;
    std::condition_variable mFlushDone;
    std::condition_variable mExitedCv;
    std::atomic<bool> mFlushRequestedFlag{false};
    bool mFlushRequested = false;
    bool mFlushDurable = false;
    bool mLastFlushOk = true;
    bool mExited = false;
    uint64 mFlushFence = 0;
    uint64 mFlushAckedPos = 0;
    uint32 mFlushIntervalMs = 0;
};

} // namespace ludus::foundation::logging::internal
