#pragma once

// -----------------------------------------------------------------------------
// Profiling clock (final design Phase 0 / §5, gate C7).
//
// The engine's monotonic timestamp source for tracing. This header is
// deliberately cheap: it declares a non-template `noexcept` function and pulls
// in NO <chrono>. The <chrono> machinery lives in clock.cpp so that the
// ubiquitous instrumentation header (profiling.hpp) can obtain a timestamp
// without paying <chrono> parse cost in every translation unit (ADR 0004/0005,
// AGENTS.md build-time hygiene).
//
// Contract:
//   * Monotonic and non-decreasing on a given thread.
//   * Unit is integer nanoseconds (never floating-point seconds — the design
//     rejects R's single-precision-seconds timestamps because subtracting large
//     rounded values loses short-interval precision).
//   * Backed by std::chrono::steady_clock today. A TSC backend is deferred
//     behind this same declaration until gate C7 (ordering / frequency /
//     migration contract) is measured; instrumentation never depends on which
//     backend is active — it only calls NowTicks().
// -----------------------------------------------------------------------------

#include <ludus/foundation/base/types.h>

namespace ludus::foundation::profiling
{

// Current monotonic timestamp in nanoseconds. Cheap, allocation-free, lock-free.
// This is the single timestamp entry point used on the instrumentation hot path.
[[nodiscard]] uint64 NowTicks() noexcept;

// Nanoseconds per tick for the active backend. With the steady_clock backend
// this is 1 (ticks already are nanoseconds); it exists so downstream analysis
// can convert ticks to a duration without assuming the backend. Constant for a
// process run.
[[nodiscard]] uint64 TicksPerNanosecondDenominator() noexcept;

} // namespace ludus::foundation::profiling
