#pragma once

// -----------------------------------------------------------------------------
// Profiling clock (final design Phase 0 / §5, gate C7).
//
// The engine's monotonic timestamp source for tracing. This header is
// deliberately cheap: it declares a non-template `noexcept` function and pulls
// in NO <chrono>. The <chrono> machinery lives in FoundationTime so that the
// ubiquitous instrumentation header (profiling.hpp) can obtain a timestamp
// without paying <chrono> parse cost in every translation unit (ADR 0004/0005,
// AGENTS.md build-time hygiene).
//
// Contract:
//   * Monotonic and non-decreasing on a given thread.
//   * Unit is integer nanoseconds (never floating-point seconds — the design
//     rejects R's single-precision-seconds timestamps because subtracting large
//     rounded values loses short-interval precision).
//   * Delegates to FoundationTime, preserving its epoch and ns units. A TSC
//     backend is deferred behind this declaration until gate C7 (ordering / frequency /
//     migration contract) is measured; instrumentation never depends on which
//     backend is active — it only calls NowTicks().
// -----------------------------------------------------------------------------

#include <ludus/foundation/base/types.h>

namespace ludus::foundation::profiling
{

// Current monotonic timestamp in nanoseconds. No engine allocations or locks;
// units do not promise ns resolution.
// This is the compatibility timestamp entry point on the instrumentation hot path.
// Timestamps do not establish a happens-before relation between threads.
[[nodiscard]] uint64 NowTicks() noexcept;

// Compatibility denominator: always one because exported timestamps remain
// integer nanoseconds regardless of the underlying physical clock resolution.
[[nodiscard]] uint64 TicksPerNanosecondDenominator() noexcept;

} // namespace ludus::foundation::profiling
