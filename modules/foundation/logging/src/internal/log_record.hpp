#pragma once

#include <ludus/foundation/base/types.h>
#include <ludus/foundation/logging/category.hpp>
#include <ludus/foundation/logging/level.hpp>

#include <string_view>

namespace ludus::foundation::logging::internal
{

// Fixed-size, trivially copyable record header for the (future) asynchronous
// queue (design.md section 4). Kept compact so the async backend can copy
// `header + message bytes` into a bounded slot without redesign. The textual
// message follows the header in the queue byte stream; it is never a
// std::string per record.
struct LogRecordHeader
{
    uint64 Sequence;       // successful reservation position (NOT event time)
    uint64 MonotonicTicks; // steady_clock ticks captured on the producer
    uint32 NativeThreadId; // OS thread id (requirements R23)
    uint32 CategoryId;     // LogCategory::Id
    uint32 SourceId;       // interned source-file/line descriptor id (0 = none)
    uint32 Line;           // std::source_location::line()
    LogLevel Level;
    uint8 Flags = 0;        // Truncated | FormatError | EmergencyMirror | ...
    uint16 MessageSize = 0; // bytes of message text following the header
};

static_assert(sizeof(LogRecordHeader) <= 40, "LogRecordHeader must stay compact for queue copies");

// The view a sink receives. In synchronous mode this borrows the caller's
// source metadata and formatted message; all members are non-owning references
// valid only for the duration of the Write() call, so sinks must copy anything
// they retain (requirements R24).
struct LogRecordView
{
    LogLevel Level = LogLevel::Info;
    LogCategory Category = LOG_CORE;

    std::string_view Message;

    // Source metadata (present even when a sink hides it for low severities).
    std::string_view File;
    std::string_view Function;
    uint32 Line = 0;

    // Human-readable thread name plus the readable counter id and the native OS
    // thread id (requirements R23; F11: native id enables debugger correlation).
    std::string_view ThreadName;
    uint32 ThreadId = 0;
    uint64 NativeThreadId = 0;

    // Monotonic tick count captured on the producer; the backend converts to
    // wall-clock text via the session anchor. Monotonic ordering never reverses
    // under a wall-clock correction (requirements R23; F11).
    uint64 MonotonicTicks = 0;

    // Successful submission sequence; correlates completion, not physical time.
    uint64 Sequence = 0;
};

} // namespace ludus::foundation::logging::internal
