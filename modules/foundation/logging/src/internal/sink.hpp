#pragma once

#include "internal/log_record.hpp"

namespace ludus::foundation::logging::internal
{

// Internal sink abstraction (spec section 18). This interface is a private
// implementation detail: engine code logs through the LUDUS_LOG_* macros and
// never sees or touches sinks. Both Write and Flush are noexcept because a sink
// failure must never propagate an exception into engine code on the logging
// path; sinks absorb and, at most, degrade quietly.
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
    virtual void Write(const LogRecordView& record) noexcept = 0;

    // Push any buffered bytes toward durable storage / the terminal.
    virtual void Flush() noexcept = 0;
};

} // namespace ludus::foundation::logging::internal
