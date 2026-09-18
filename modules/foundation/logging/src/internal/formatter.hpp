#pragma once

#include "internal/log_record.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace ludus::foundation::logging::internal {

// Whether a sink should append the "file:line" source suffix for a given level.
// Warning and above always show source; lower levels hide it in normal output
// even though the record still carries it (spec section 10).
[[nodiscard]] bool source_visible_for(LogLevel level) noexcept;

// Format the wall-clock timestamp (ns since epoch) as "HH:MM:SS.mmm" into `out`,
// which must have room for at least 12 characters plus a terminator. Returns the
// number of characters written. Allocation-free.
std::size_t format_timestamp(std::uint64_t timestamp_ns, char* out, std::size_t capacity) noexcept;

// Render one record as a full console/file line (without trailing newline) into
// `buffer`, reusing its capacity to avoid per-call allocation where possible.
// The produced line follows spec section 19:
//   HH:MM:SS.mmm [Thread] [LEVEL] [Category] message
// and, when source is visible for the level, a following indented line:
//       file:line
// `use_color` toggles ANSI SGR sequences around the level field.
void format_console_line(const LogRecordView& record, bool use_color, std::string& buffer);

// The current thread's human-readable name (spec section 29). Defaults to
// "Main" for the process's first-seen thread and "Thread-<id>" otherwise until
// set explicitly.
[[nodiscard]] std::string_view current_thread_name() noexcept;

// The current thread's stable numeric id (a small monotonic counter, not the OS
// tid, so log output is stable and readable).
[[nodiscard]] std::uint32_t current_thread_id() noexcept;

// Wall-clock nanoseconds since the Unix epoch.
[[nodiscard]] std::uint64_t now_nanoseconds() noexcept;

} // namespace ludus::foundation::logging::internal
