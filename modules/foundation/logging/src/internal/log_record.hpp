#pragma once

#include <ludus/foundation/base/types.h>
#include <ludus/foundation/logging/category.hpp>
#include <ludus/foundation/logging/level.hpp>

#include <string_view>

namespace ludus::foundation::logging::internal
{

// Fixed-size, trivially copyable record header (spec section 14). Phase 1 is
// synchronous and does not enqueue records, but the header is defined now so the
// asynchronous backend can copy `header + message bytes` into a bounded ring
// buffer without redesign. The textual message is stored separately and follows
// the header in the queue's byte stream; it is never a std::string per record.
struct LogRecordHeader
{
    uint64 TimestampNs; // steady/wall clock nanoseconds since epoch
    uint32 ThreadId;    // internal numeric thread id
    uint32 CategoryId;  // LogCategory::Id

    uint32 FileId; // interned source-file id (0 = not interned yet)
    uint32 Line;   // std::source_location::line()

    LogLevel Level;
    uint8 Reserved0 = 0;
    uint16 MessageSize = 0; // bytes of message text following the header
};

static_assert(sizeof(LogRecordHeader) <= 32, "LogRecordHeader must stay compact for queue copies");

// The view a sink receives. In synchronous mode this borrows the caller's
// thread-local scratch buffer and source metadata; in asynchronous mode the
// consumer thread will reconstruct an equivalent view over the dequeued bytes.
// All members are non-owning references valid only for the duration of the
// Write() call, so sinks must copy anything they retain.
struct LogRecordView
{
    LogLevel Level = LogLevel::Info;
    LogCategory Category = LOG_CORE;

    std::string_view Message;

    // Source metadata (spec section 10). Present in the record even when a sink
    // chooses to hide it for low-severity levels.
    std::string_view File;
    std::string_view Function;
    uint32 Line = 0;

    // Human-readable thread name (e.g. "Main", "Render") plus the numeric id.
    std::string_view ThreadName;
    uint32 ThreadId = 0;

    // Wall-clock timestamp in nanoseconds since the Unix epoch; sinks format it
    // as they see fit (console uses HH:MM:SS.mmm).
    uint64 TimestampNs = 0;
};

} // namespace ludus::foundation::logging::internal
