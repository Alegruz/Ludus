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
    std::uint64_t timestamp_ns; // steady/wall clock nanoseconds since epoch
    std::uint32_t thread_id;    // internal numeric thread id
    std::uint32_t category_id;  // LogCategory::id

    std::uint32_t file_id; // interned source-file id (0 = not interned yet)
    std::uint32_t line;    // std::source_location::line()

    LogLevel level;
    std::uint8_t reserved0 = 0;
    std::uint16_t message_size = 0; // bytes of message text following the header
};

static_assert(sizeof(LogRecordHeader) <= 32, "LogRecordHeader must stay compact for queue copies");

// The view a sink receives. In synchronous mode this borrows the caller's
// thread-local scratch buffer and source metadata; in asynchronous mode the
// consumer thread will reconstruct an equivalent view over the dequeued bytes.
// All members are non-owning references valid only for the duration of the
// Write() call, so sinks must copy anything they retain.
struct LogRecordView
{
    LogLevel level = LogLevel::Info;
    LogCategory category = LogCore;

    std::string_view message;

    // Source metadata (spec section 10). Present in the record even when a sink
    // chooses to hide it for low-severity levels.
    std::string_view file;
    std::string_view function;
    std::uint32_t line = 0;

    // Human-readable thread name (e.g. "Main", "Render") plus the numeric id.
    std::string_view thread_name;
    std::uint32_t thread_id = 0;

    // Wall-clock timestamp in nanoseconds since the Unix epoch; sinks format it
    // as they see fit (console uses HH:MM:SS.mmm).
    std::uint64_t timestamp_ns = 0;
};

} // namespace ludus::foundation::logging::internal
