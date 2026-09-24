#pragma once

// -----------------------------------------------------------------------------
// Trace lifecycle & capture control (final design §15, §21, §22).
//
// This is the NON-ubiquitous control surface: the platform/app layer calls it to
// arm a capture, register threads, and export. Ordinary code never includes it;
// it only uses the macros in profiling.hpp. Kept off the hot path, so it may use
// slightly heavier facilities than the instrumentation header (still no
// exceptions, still no iostreams).
// -----------------------------------------------------------------------------

#include <ludus/foundation/base/types.h>

#include <string_view>

namespace ludus::foundation::profiling
{

// Snapshot of recorder health (§21). All counters are best-effort relaxed reads.
struct TraceHealth
{
    uint64 EventsRecorded = 0;
    uint64 EventsDropped = 0; // over-capacity drops during capture
    uint64 ThreadsRegistered = 0;
};

// Register the calling thread with the tracer so its events are attributed and
// its partial chunk is flushed on exit. Assigns a stable profiling-local thread
// id and records the human-readable name for export. Safe to call before
// BeginCapture. Idempotent per thread. (Profiling keeps its own thread identity
// rather than depending on the logging module, preserving the FoundationBase-only
// dependency direction — AGENTS.md.)
void RegisterThreadForTrace(std::string_view threadName) noexcept;

// Unregister the calling thread (flushes its partial chunk). Optional — thread
// destruction does this automatically.
void UnregisterThreadForTrace() noexcept;

// Arm a capture session. `maxChunks` bounds total trace memory
// (maxChunks * ~96 KiB). Returns false if a capture is already active or memory
// reservation failed (in which case tracing stays disabled and the engine runs
// normally — §21). No-op / returns false in a profiling-disabled build.
bool BeginCapture(usize maxChunks = 64) noexcept;

// End the active capture session. After this, ExportPerfettoTrace() can be
// called to serialise the recorded events.
void EndCapture() noexcept;

// Serialise the most recent capture to a Chrome/Perfetto trace-event JSON file
// (final design §16 primary viewer path). Off the hot path. Returns false on I/O
// failure or when nothing was captured. Safe no-op in a disabled build.
bool ExportPerfettoTrace(std::string_view path) noexcept;

// Health snapshot.
[[nodiscard]] TraceHealth GetTraceHealth() noexcept;

} // namespace ludus::foundation::profiling
