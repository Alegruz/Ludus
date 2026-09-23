#pragma once

#include "internal/log_record.hpp"

namespace ludus::foundation::logging::internal
{

// Compact write/flush status returned by sinks (design.md section 9,
// requirements R28/R45). A sink NEVER throws (engine builds -fno-exceptions);
// it reports failure as a status so the backend can track per-sink health and
// so a failed write cannot masquerade as a successful delivery (fixes F8).
enum class SinkStatus : uint8
{
    Ok,          // record written / buffers flushed
    Disabled,    // sink is intentionally inactive (e.g. debugger not attached)
    Failed,      // an I/O error occurred; sink marked unhealthy by caller
    Unsupported, // operation not applicable to this sink (e.g. durable flush)
};

// Internal sink abstraction. This interface is a private implementation detail:
// engine code logs through the LUDUS_LOG_* macros and never sees or touches
// sinks. Methods are noexcept because a sink failure must never propagate into
// engine code; sinks absorb errors and report them via SinkStatus.
class ILogSink
{
public:
    ILogSink() = default;
    virtual ~ILogSink() = default;

    ILogSink(const ILogSink&) = delete;
    ILogSink& operator=(const ILogSink&) = delete;
    ILogSink(ILogSink&&) = delete;
    ILogSink& operator=(ILogSink&&) = delete;

    // Consume one record. The view's referenced storage is only valid for the
    // duration of this call; a sink that defers work must copy what it needs.
    virtual SinkStatus Write(const LogRecordView& record) noexcept = 0;

    // Push buffered bytes toward the OS ("visible"). Does not guarantee the data
    // reached the storage device.
    virtual SinkStatus Flush() noexcept = 0;

    // Request a platform durable flush (fsync/FlushFileBuffers). Default:
    // Unsupported for sinks without durable semantics (console/debugger).
    virtual SinkStatus FlushDurable() noexcept
    {
        return SinkStatus::Unsupported;
    }
};

} // namespace ludus::foundation::logging::internal
