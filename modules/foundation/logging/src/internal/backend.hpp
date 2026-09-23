#pragma once

#include <ludus/foundation/base/types.h>

#include "internal/mpsc_queue.hpp"

// NOTE: this header is deliberately light. It pulls neither <memory>/<vector>
// (which drag in <format> through libstdc++'s <memory>) nor the worker's
// concurrency machinery (std::thread / std::condition_variable / std::mutex).
// All of that lives in backend.cpp behind a PIMPL, so including backend.hpp does
// not tax build time (ADR 0004/0005; the Build-time budget gate flagged the
// previous header-heavy version). Sink ownership is transferred one at a time
// via AdoptSink() using a raw owning pointer, avoiding std::unique_ptr here.

namespace ludus::foundation::logging::internal
{

class ILogSink;

class AsyncBackend
{
public:
    static constexpr usize kCapacity = 4096; // prototype (spec CD3)

    AsyncBackend() noexcept;
    ~AsyncBackend();

    AsyncBackend(const AsyncBackend&) = delete;
    AsyncBackend& operator=(const AsyncBackend&) = delete;
    AsyncBackend(AsyncBackend&&) = delete;
    AsyncBackend& operator=(AsyncBackend&&) = delete;

    // Transfer ownership of one sink to the backend (call before Start()). The
    // backend deletes it on Stop()/destruction. Raw owning pointer keeps
    // <memory> out of this header.
    void AdoptSink(ILogSink* sink) noexcept;

    // Start the worker; it becomes the sole owner/user of the adopted sinks.
    void Start(uint32 flushIntervalMs) noexcept;

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
    // for the worker to process through it and flush the requested kind.
    FlushOutcome Flush(bool durable, uint32 timeoutMs) noexcept;

    // Stop: drain, flush, close, join. Returns false (RETAINING all storage) if
    // the worker cannot be joined within the deadline, so a running worker never
    // references freed storage (requirements R40).
    [[nodiscard]] bool Stop(uint32 joinTimeoutMs) noexcept;

    [[nodiscard]] uint64 Dropped() const noexcept;
    [[nodiscard]] uint64 Written() const noexcept;

private:
    struct Impl;
    Impl* mImpl = nullptr; // owned; freed in the destructor (avoids <memory> here)
};

} // namespace ludus::foundation::logging::internal
