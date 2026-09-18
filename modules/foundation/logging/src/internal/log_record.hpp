#pragma once

#include <ludus/foundation/logging/category.hpp>
#include <ludus/foundation/logging/level.hpp>

#include <cstdint>
#include <string_view>

namespace ludus::foundation::logging::internal {

// Fixed-size, trivially copyable record header (spec section 14). Phase 1 is
// synchronous and does not enqueue records, but the header is defined now so the
// asynchronous backend can copy `header + message bytes` into a bounded ring
// buffer without redesign. The textual message is stored separately and follows
// the header in the queue's byte stream; it is never a std::string per record.
struct LogRecordHeader
{
    std::uint64_t TimestampNs; // steady/wall clock nanoseconds since epoch
    std::uint32_t ThreadId;    // internal numeric thread id
    std::uint32_t CategoryId;  // LogCategory::Id

    std::uint32_t FileId; // interned source-file id (0 = not interned yet)
    std::uint32_t Line;   // std::source_location::line()

    LogLevel Level;
    std::uint8_t Reserved0 = 0;
    std::uint16_t MessageSize = 0; // bytes of message text following the header
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
    std::uint32_t Line = 0;

    // Human-readable thread name (e.g. "Main", "Render") plus the numeric id.
    std::string_view ThreadName;
    std::uint32_t ThreadId = 0;

    // Wall-clock timestamp in nanoseconds since the Unix epoch; sinks format it
    // as they see fit (console uses HH:MM:SS.mmm).
    std::uint64_t TimestampNs = 0;
};

} // namespace ludus::foundation::logging::internal
