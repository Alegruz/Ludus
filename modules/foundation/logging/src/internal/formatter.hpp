#pragma once

#include "internal/log_record.hpp"

#include <ludus/foundation/base/types.h>

#include <string>
#include <string_view>

namespace ludus::foundation::logging::internal
{

// Whether a sink should append the "file:line" source suffix for a given level.
// Warning and above always show source; lower levels hide it in normal output
// even though the record still carries it.
[[nodiscard]] bool IsSourceVisibleFor(LogLevel level) noexcept;

// Render one record as a full console/file line (without trailing newline) into
// `buffer`, reusing its capacity to avoid per-call allocation where possible.
// Layout:
//   HH:MM:SS.mmm [Thread] [LEVEL] [Category] message
// and, when source is visible for the level, a following indented line:
//       file:line
// `use_color` toggles ANSI SGR sequences around the level field.
void FormatConsoleLine(const LogRecordView& record, bool use_color, std::string& buffer);

// The current thread's human-readable name. Defaults to "Main" for the first
// thread that logs and "Thread-<id>" otherwise until set explicitly.
[[nodiscard]] std::string_view GetCurrentThreadName() noexcept;

// The current thread's stable readable id (a small monotonic counter, not the
// OS tid, so log output is stable and readable).
[[nodiscard]] uint32 GetCurrentThreadId() noexcept;

// The current thread's native OS thread id (requirements R23; enables debugger
// correlation, unlike the readable counter).
[[nodiscard]] uint64 GetNativeThreadId() noexcept;

// Monotonic steady-clock ticks (nanoseconds) captured on the producer. A
// wall-clock correction never moves this backwards (requirements R23; F11).
[[nodiscard]] uint64 GetMonotonicTicks() noexcept;

} // namespace ludus::foundation::logging::internal
