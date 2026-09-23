#pragma once

#include <ludus/foundation/base/types.h>

#include "internal/log_record.hpp"
#include "internal/mpsc_queue.hpp"
#include "internal/sink.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace ludus::foundation::logging::internal
{

// Asynchronous backend: one worker thread OWNS the ordinary sinks and drains a
// bounded MPSC queue (design.md sections 7, 8; requirements R35-R40).
//
// Ownership: producers never touch sinks. The worker is the sole owner of the
// sink vector between Start() and Stop(); it flushes/closes on Stop(). A flush
// request travels on a SEPARATE control channel (a small mutex-guarded fence)
// so a full data queue can never prevent requesting a flush (requirements R29).
class AsyncBackend
{
public:
    static constexpr usize kCapacity = 4096; // prototype (spec CD3)
    using Queue = MpscQueue<kCapacity>;

    // Takes ownership of the sinks. Starts the worker.
    void Start(std::vector<std::unique_ptr<ILogSink>>&& sinks, uint32 flushIntervalMs) noexcept
    {
        mSinks = std::move(sinks);
        mFlushIntervalMs = flushIntervalMs;
        mStop.store(false, std::memory_order_relaxed);
        mWorker = std::thread([this] { Run(); });
    }

    // Producer: enqueue an owned record. Returns false if dropped (queue full /
    // contention). Never blocks. Wakes the worker.
    [[nodiscard]] bool Enqueue(const QueuedRecord& record) noexcept
    {
        const bool ok = mQueue.TryEnqueue(record);
        if (ok)
        {
            mPublished.fetch_add(1, std::memory_order_release);
            mWakeCv.notify_one();
        }
        return ok;
    }

    // Capture a flush fence at the current enqueue position and wait (bounded)
    // for the worker to process through it and flush the requested kind. Returns
    // whether all sinks flushed OK, and the acknowledged position. A timeout is
    // honest: it reports incomplete rather than cancelling a blocked OS write
    // (requirements R28/R29).
    struct FlushOutcome
    {
        bool Ok = false;
        bool TimedOut = false;
        uint64 AcknowledgedPos = 0;
    };

    FlushOutcome Flush(bool durable, uint32 timeoutMs) noexcept
    {
        std::unique_lock<std::mutex> lock(mControlMutex);
        const uint64 fence = mQueue.EnqueuePos();
        mFlushFence = fence;
        mFlushDurable = mFlushDurable || durable;
        mFlushRequested = true;
        mFlushRequestedFlag.store(true, std::memory_order_release);
        mWakeCv.notify_one();

        const bool completed = mFlushDone.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this, fence] {
            return mFlushAckedPos >= fence && !mFlushRequested;
        });

        FlushOutcome outcome;
        outcome.AcknowledgedPos = mFlushAckedPos;
        outcome.TimedOut = !completed;
        outcome.Ok = completed && mLastFlushOk;
        return outcome;
    }

    // Stop: signal, wake, join, then flush/close and release sinks. If the worker
    // cannot be joined within the deadline, RETAIN the sinks (do not free storage
    // still reachable by a running worker) and report incomplete
    // (requirements R40).
    [[nodiscard]] bool Stop(uint32 joinTimeoutMs) noexcept
    {
        mStop.store(true, std::memory_order_release);
        mWakeCv.notify_all();
        if (!mWorker.joinable())
        {
            return true;
        }
        // std::thread has no timed join; use a done-flag with a timed wait, then
        // join only once the worker signalled exit. If it never signals within
        // the deadline we must NOT detach+free — we leave the thread joined-later
        // by retaining this backend object (caller keeps it alive).
        {
            std::unique_lock<std::mutex> lock(mControlMutex);
            const bool exited =
                mExitedCv.wait_for(lock, std::chrono::milliseconds(joinTimeoutMs), [this] { return mExited; });
            if (!exited)
            {
                return false; // caller retains storage; do not free
            }
        }
        mWorker.join();
        for (auto& sink : mSinks)
        {
            (void)sink->Flush();
        }
        mSinks.clear();
        return true;
    }

    [[nodiscard]] uint64 Dropped() const noexcept
    {
        return mQueue.Dropped();
    }
    [[nodiscard]] uint64 Published() const noexcept
    {
        return mPublished.load(std::memory_order_relaxed);
    }
    [[nodiscard]] uint64 Written() const noexcept
    {
        return mWritten.load(std::memory_order_relaxed);
    }

private:
    void RenderAndWrite(const QueuedRecord& rec) noexcept;

    void DrainBatch() noexcept
    {
        // Bounded batch so control/flush work is not starved (requirements R39).
        constexpr int kBatch = 256;
        QueuedRecord rec;
        for (int i = 0; i < kBatch; ++i)
        {
            if (!mQueue.TryDequeue(rec))
            {
                break;
            }
            RenderAndWrite(rec);
            mWritten.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void ServiceFlush() noexcept
    {
        bool durable = false;
        uint64 fence = 0;
        bool requested = false;
        {
            std::lock_guard<std::mutex> lock(mControlMutex);
            requested = mFlushRequested;
            durable = mFlushDurable;
            fence = mFlushFence;
        }
        if (!requested)
        {
            return;
        }
        // Only complete the fence once the consumer cursor has passed it (all
        // records reserved before the fence are drained). A paused producer
        // before the fence keeps us from acking; the requester's deadline makes
        // that observable (requirements R29).
        if (mQueue.DequeuePos() < fence)
        {
            return;
        }
        bool ok = true;
        for (auto& sink : mSinks)
        {
            const SinkStatus s = durable ? sink->FlushDurable() : sink->Flush();
            if (s != SinkStatus::Ok && s != SinkStatus::Unsupported)
            {
                ok = false;
            }
        }
        {
            std::lock_guard<std::mutex> lock(mControlMutex);
            mFlushAckedPos = mQueue.DequeuePos();
            mLastFlushOk = ok;
            mFlushRequested = false;
            mFlushDurable = false;
            mFlushRequestedFlag.store(false, std::memory_order_release);
        }
        mFlushDone.notify_all();
    }

    void Run() noexcept
    {
        uint64 seen = 0;
        while (true)
        {
            {
                std::unique_lock<std::mutex> lock(mWakeMutex);
                // Lost-wakeup-safe: wait until published advances, a flush is
                // requested, stop is set, or the timed-flush deadline elapses
                // (requirements R39).
                mWakeCv.wait_for(lock,
                                 std::chrono::milliseconds(mFlushIntervalMs == 0 ? 50 : mFlushIntervalMs),
                                 [this, &seen] {
                                     return mStop.load(std::memory_order_acquire) ||
                                            mPublished.load(std::memory_order_acquire) != seen || FlushPendingRelaxed();
                                 });
                seen = mPublished.load(std::memory_order_acquire);
            }

            DrainBatch();
            ServiceFlush();

            // Timed flush opportunity (requirements R46): flush visible on the
            // interval even without an explicit request.
            if (mFlushIntervalMs != 0)
            {
                for (auto& sink : mSinks)
                {
                    (void)sink->Flush();
                }
            }

            if (mStop.load(std::memory_order_acquire))
            {
                // Drain everything already published before exiting.
                QueuedRecord rec;
                while (mQueue.TryDequeue(rec))
                {
                    RenderAndWrite(rec);
                    mWritten.fetch_add(1, std::memory_order_relaxed);
                }
                for (auto& sink : mSinks)
                {
                    (void)sink->Flush();
                }
                ServiceFlush();
                break;
            }
        }
        {
            std::lock_guard<std::mutex> lock(mControlMutex);
            mExited = true;
        }
        mExitedCv.notify_all();
    }

    [[nodiscard]] bool FlushPendingRelaxed() const noexcept
    {
        return mFlushRequestedFlag.load(std::memory_order_acquire);
    }

    Queue mQueue;
    std::vector<std::unique_ptr<ILogSink>> mSinks; // worker-owned
    std::thread mWorker;

    std::atomic<bool> mStop{false};
    std::atomic<uint64> mPublished{0};
    std::atomic<uint64> mWritten{0};

    // Wakeup channel.
    std::mutex mWakeMutex;
    std::condition_variable mWakeCv;

    // Control channel (flush fence), separate from the data queue. The pending
    // flag is atomic so the wakeup predicate can read it without the control
    // lock (TSan-clean); the fence details are guarded by mControlMutex.
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
