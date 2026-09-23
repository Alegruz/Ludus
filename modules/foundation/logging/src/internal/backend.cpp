#include "internal/backend.hpp"

#include "internal/log_record.hpp"
#include "internal/sink.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <new>
#include <thread>
#include <vector>

namespace ludus::foundation::logging::internal
{

// All the worker's concurrency machinery lives here (PIMPL), so backend.hpp
// stays free of <thread>/<condition_variable>/<mutex> and the <chrono>/<format>
// they drag in (keeps the header within the build-time budget).
struct AsyncBackend::Impl
{
    using Queue = MpscQueue<AsyncBackend::kCapacity>;

    Queue queue;
    std::vector<ILogSink*> sinks; // worker-owned raw owning pointers (deleted on stop)
    std::thread worker;

    ~Impl()
    {
        for (ILogSink* sink : sinks)
        {
            delete sink;
        }
    }

    std::atomic<bool> stop{false};
    std::atomic<uint64> published{0};
    std::atomic<uint64> written{0};

    std::mutex wakeMutex;
    std::condition_variable wakeCv;

    std::mutex controlMutex;
    std::condition_variable flushDone;
    std::condition_variable exitedCv;
    std::atomic<bool> flushRequestedFlag{false};
    bool flushRequested = false;
    bool flushDurable = false;
    bool lastFlushOk = true;
    bool exited = false;
    uint64 flushFence = 0;
    uint64 flushAckedPos = 0;
    uint32 flushIntervalMs = 0;

    void RenderAndWrite(const QueuedRecord& rec) noexcept
    {
        // Reconstruct the sink view over the dequeued OWNED bytes. Message points
        // into the record's own storage (valid for this call); category/file are
        // process-lifetime views; thread name was copied into the record so it is
        // valid even after the producer thread exits (requirements R24/R36).
        LogRecordView view;
        view.Level = rec.Level;
        view.Category = LogCategory{rec.CategoryId, rec.CategoryName};
        view.Message = std::string_view(rec.Message, rec.MessageLen);
        view.File = rec.File;
        view.Function = std::string_view{};
        view.Line = rec.Line;
        view.ThreadName = rec.ThreadNameView();
        view.ThreadId = rec.ThreadId;
        view.NativeThreadId = rec.NativeThreadId;
        view.MonotonicTicks = rec.MonotonicTicks;
        view.Sequence = rec.Sequence;

        for (auto& sink : sinks)
        {
            (void)sink->Write(view);
        }
        // Error/Fatal flush promptly so their visibility promise holds in async
        // mode too (requirements R54).
        if (rec.Level >= LogLevel::Error)
        {
            for (auto& sink : sinks)
            {
                (void)sink->Flush();
            }
        }
    }

    void DrainBatch() noexcept
    {
        constexpr int kBatch = 256; // bounded so flush/control work is not starved (R39)
        QueuedRecord rec;
        for (int i = 0; i < kBatch; ++i)
        {
            if (!queue.TryDequeue(rec))
            {
                break;
            }
            RenderAndWrite(rec);
            written.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void ServiceFlush() noexcept
    {
        bool durable = false;
        uint64 fence = 0;
        bool requested = false;
        {
            std::lock_guard<std::mutex> lock(controlMutex);
            requested = flushRequested;
            durable = flushDurable;
            fence = flushFence;
        }
        if (!requested)
        {
            return;
        }
        // Complete the fence only once the consumer cursor has passed it (all
        // records reserved before the fence are drained). A paused producer
        // before the fence keeps us from acking; the requester's deadline makes
        // that observable (requirements R29).
        if (queue.DequeuePos() < fence)
        {
            return;
        }
        bool ok = true;
        for (auto& sink : sinks)
        {
            const SinkStatus s = durable ? sink->FlushDurable() : sink->Flush();
            if (s != SinkStatus::Ok && s != SinkStatus::Unsupported)
            {
                ok = false;
            }
        }
        {
            std::lock_guard<std::mutex> lock(controlMutex);
            flushAckedPos = queue.DequeuePos();
            lastFlushOk = ok;
            flushRequested = false;
            flushDurable = false;
            flushRequestedFlag.store(false, std::memory_order_release);
        }
        flushDone.notify_all();
    }

    void Run() noexcept
    {
        uint64 seen = 0;
        while (true)
        {
            {
                std::unique_lock<std::mutex> lock(wakeMutex);
                wakeCv.wait_for(lock,
                                std::chrono::milliseconds(flushIntervalMs == 0 ? 50 : flushIntervalMs),
                                [this, &seen] {
                                    return stop.load(std::memory_order_acquire) ||
                                           published.load(std::memory_order_acquire) != seen ||
                                           flushRequestedFlag.load(std::memory_order_acquire);
                                });
                seen = published.load(std::memory_order_acquire);
            }

            DrainBatch();
            ServiceFlush();

            if (flushIntervalMs != 0)
            {
                for (auto& sink : sinks)
                {
                    (void)sink->Flush();
                }
            }

            if (stop.load(std::memory_order_acquire))
            {
                QueuedRecord rec;
                while (queue.TryDequeue(rec))
                {
                    RenderAndWrite(rec);
                    written.fetch_add(1, std::memory_order_relaxed);
                }
                for (auto& sink : sinks)
                {
                    (void)sink->Flush();
                }
                ServiceFlush();
                break;
            }
        }
        {
            std::lock_guard<std::mutex> lock(controlMutex);
            exited = true;
        }
        exitedCv.notify_all();
    }
};

AsyncBackend::AsyncBackend() noexcept : mImpl(new(std::nothrow) Impl()) {}

AsyncBackend::~AsyncBackend()
{
    delete mImpl;
}

void AsyncBackend::AdoptSink(ILogSink* sink) noexcept
{
    if (mImpl == nullptr || sink == nullptr)
    {
        return;
    }
    mImpl->sinks.push_back(sink);
}

void AsyncBackend::Start(uint32 flushIntervalMs) noexcept
{
    if (mImpl == nullptr)
    {
        return;
    }
    mImpl->flushIntervalMs = flushIntervalMs;
    mImpl->stop.store(false, std::memory_order_relaxed);
    mImpl->worker = std::thread([impl = mImpl] { impl->Run(); });
}

bool AsyncBackend::Enqueue(const QueuedRecord& record) noexcept
{
    if (mImpl == nullptr)
    {
        return false;
    }
    const bool ok = mImpl->queue.TryEnqueue(record);
    if (ok)
    {
        mImpl->published.fetch_add(1, std::memory_order_release);
        mImpl->wakeCv.notify_one();
    }
    return ok;
}

AsyncBackend::FlushOutcome AsyncBackend::Flush(bool durable, uint32 timeoutMs) noexcept
{
    FlushOutcome outcome;
    if (mImpl == nullptr)
    {
        return outcome;
    }
    std::unique_lock<std::mutex> lock(mImpl->controlMutex);
    const uint64 fence = mImpl->queue.EnqueuePos();
    mImpl->flushFence = fence;
    mImpl->flushDurable = mImpl->flushDurable || durable;
    mImpl->flushRequested = true;
    mImpl->flushRequestedFlag.store(true, std::memory_order_release);
    mImpl->wakeCv.notify_one();

    const bool completed = mImpl->flushDone.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this, fence] {
        return mImpl->flushAckedPos >= fence && !mImpl->flushRequested;
    });

    outcome.AcknowledgedPos = mImpl->flushAckedPos;
    outcome.TimedOut = !completed;
    outcome.Ok = completed && mImpl->lastFlushOk;
    return outcome;
}

bool AsyncBackend::Stop(uint32 joinTimeoutMs) noexcept
{
    if (mImpl == nullptr)
    {
        return true;
    }
    mImpl->stop.store(true, std::memory_order_release);
    mImpl->wakeCv.notify_all();
    if (!mImpl->worker.joinable())
    {
        return true;
    }
    {
        std::unique_lock<std::mutex> lock(mImpl->controlMutex);
        const bool exited =
            mImpl->exitedCv.wait_for(lock, std::chrono::milliseconds(joinTimeoutMs), [this] { return mImpl->exited; });
        if (!exited)
        {
            return false; // caller retains storage; do not free (requirements R40)
        }
    }
    mImpl->worker.join();
    for (ILogSink* sink : mImpl->sinks)
    {
        (void)sink->Flush();
        delete sink;
    }
    mImpl->sinks.clear();
    return true;
}

uint64 AsyncBackend::Dropped() const noexcept
{
    return mImpl != nullptr ? mImpl->queue.Dropped() : 0;
}

uint64 AsyncBackend::Written() const noexcept
{
    return mImpl != nullptr ? mImpl->written.load(std::memory_order_relaxed) : 0;
}

} // namespace ludus::foundation::logging::internal
