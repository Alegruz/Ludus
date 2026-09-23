#pragma once

// -----------------------------------------------------------------------------
// FoundationBase emergency diagnostic primitive.
//
// This formats controlled emergency reports in FoundationBase so Base clients
// and FoundationLogging's fallback can report without depending upward on
// FoundationLogging. Assertions build their own owned reports and use only the
// nonblocking TryWriteEmergencyBytes transport; they do not call this API
// (.kiro/specs/logging-redesign/design.md sections 1, 10; requirements
// R5, R31, R32).
//
// Deliberate properties:
//   * No heap allocation: the line is built in a fixed stack buffer.
//   * No engine locks, no category maps, no logging state or TLS strings.
//   * Never throws (engine builds -fno-exceptions).
//   * A fixed literal header ("[LUDUS:...]") so a corrupted caller still yields
//     a trustworthy marker; truncation is flagged, never silent.
//   * A first-entry/reentry guard: a failure that occurs WHILE reporting emits at
//     most a minimal literal and increments a counter instead of recursing
//     (requirements R32).
//
// It is NOT async-signal-safe and its stderr fallback can block. It is the
// "controlled diagnostic failure" path, not a crash-handler
// writer (requirements R33). A real signal/crash handler is a separate,
// narrowly-audited facility owned by the crash subsystem.
// -----------------------------------------------------------------------------

#include <ludus/foundation/base/types.h>

#include <source_location>
#include <string_view>

namespace ludus::foundation::base
{

// Small severity vocabulary for the emergency primitive. It is intentionally
// independent of FoundationLogging's LogLevel so Base never depends on Logging;
// the logging module maps its LogLevel onto these when it uses the primitive.
enum class DiagnosticSeverity : uint8
{
    Note,
    Warning,
    Error,
    Fatal,
};

// Report one emergency diagnostic. `message` is already-final text; the primitive
// performs no formatting of its own beyond composing the fixed header and the
// source suffix, so it stays allocation-free. Uses Base's emergency byte writer:
// a configured diagnostic socket, otherwise potentially blocking stderr.
//
// Reentry: if called again while a previous call on this thread is still in
// progress (e.g. a fault during the write), it emits a minimal literal and
// returns, so it can never recurse or grow the stack.
// category then message, matching the logging call convention; always passed as
// named/literal args, so a swap is not a realistic hazard.
void EmergencyReport(DiagnosticSeverity severity,
                     std::string_view categoryName, // NOLINT(bugprone-easily-swappable-parameters)
                     std::string_view message,
                     const std::source_location& location = std::source_location::current()) noexcept;

// Convenience for internal notes that have no meaningful caller site. Uses the
// current source location and the "Diagnostic" category.
void EmergencyNote(std::string_view message,
                   const std::source_location& location = std::source_location::current()) noexcept;

// Number of times a reentrant (nested) emergency call was suppressed. Exposed
// for tests/diagnostics so recursion suppression is observable (requirements
// R32); it never triggers further reporting.
[[nodiscard]] uint64 EmergencyReentryCount() noexcept;

} // namespace ludus::foundation::base
