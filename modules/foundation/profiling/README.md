# FoundationProfiling

CPU trace instrumentation for the Ludus engine. Implements **Phase 0 (shared
clock)** and **Phase 1 (CPU Trace MVP + Perfetto export)** of the finalized
[profiling architecture](../../../docs/architecture/profiling-final.md).

## What this module provides now

- A shared monotonic clock (`NowTicks()`, `steady_clock` nanoseconds).
- Ubiquitous, cheap-to-include instrumentation macros:
  - `LUDUS_PROFILE_SCOPE(Name)` / `LUDUS_PROFILE_SCOPE(Name, Category)` — RAII CPU scope.
  - `LUDUS_PROFILE_FUNCTION()` — scope named from `__func__`.
  - `LUDUS_PROFILE_FRAME()` — frame boundary marker (advances the frame epoch).
  - `LUDUS_PROFILE_MARK(Name)` — zero-duration instant marker.
  - `LUDUS_PROFILE_FLOW_OUT(Name)` / `LUDUS_PROFILE_FLOW_IN(id)` — cross-context links.
- Per-thread lock-free recording into fixed, pre-reserved chunk buffers.
- Capture lifecycle (`BeginCapture` / `EndCapture`) and Chrome/Perfetto JSON
  export (`ExportPerfettoTrace`) — open the file in <https://ui.perfetto.dev>.

## Usage

```cpp
#include <ludus/foundation/profiling/profiling.hpp>      // macros (cheap)
#include <ludus/foundation/profiling/trace_system.hpp>   // control (app layer only)

using namespace ludus::foundation::profiling;

RegisterThreadForTrace("Main");
BeginCapture(/*maxChunks=*/64);

void RenderScene() {
    LUDUS_PROFILE_FUNCTION();
    LUDUS_PROFILE_SCOPE(ShadowMaps);
    // ... work ...
}
// per frame: LUDUS_PROFILE_FRAME();

EndCapture();
ExportPerfettoTrace("trace.json");
```

## Build flavors

`LUDUS_PROFILING_ENABLED` is SDK-owned (generated `profiling_config.hpp`) and
derived from the build flavor: **on** for Debug/Development/Profile, **off** for
Release. In a disabled build every macro expands to `((void)0)` and evaluates no
argument — verified by `tests/disabled_tests.cpp` and by inspecting generated
code (gate G1).

## Design boundaries (unchanged from the architecture)

- The event stream is the ground truth; the hierarchical tree, inclusive/
  exclusive time and calls-per-frame are **derived off-line** (by Perfetto or a
  future analysis pass), never stored on the hot path.
- Visualization is external (Perfetto now, Tracy later). No in-engine timeline.
- Counters/gauges (Phase 2), hitch capture (Phase 3), flow correlation logic
  (Phase 4), GPU trace (Phase 5, needs an RHI) and memory profiling (Phase 6,
  needs the allocator) are **not** in this module yet, by design.

## Implementation notes / deliberate MVP adjustments

These are the smallest defensible deviations from the architecture document,
recorded per the implementation brief:

1. **Synchronous drain instead of a dedicated collector thread.** The producer
   hot path is exactly the design's (timestamp + relaxed stores into a TLS chunk;
   full chunks published to a lock-free stack). For Phase 1 — explicit capture
   *sessions*, not the continuous rolling ring of Phase 3 — the collector drains
   on the calling thread at export time. A background collector thread buys
   nothing here and adds join/lifetime surface; it lands with Phase 3, where
   continuous capture needs it. No hot-path property changes.
2. **No dependency on FoundationLogging.** To keep the FoundationBase-only
   dependency direction (AGENTS.md), profiling keeps its own thread-identity
   table rather than reusing the logging TLS identity.
3. **`TraceEvent` is a single 24-byte record** with an `Aux` field carrying the
   optional flow id or category id. The design's separate side-channel stream is
   deferred until the event-size measurement (gates G4/G5) says it is worth it.
4. **`LUDUS_PROFILE_FUNCTION` builds its descriptor at runtime** (`constexpr`,
   not `consteval`) because `__func__` is not a constant expression; the cost is
   two `string_view` assignments, and site registration still happens once.

## Validation status

18 unit tests (`clock`, `scope`, `export`, `disabled`) cover the §11 correctness
matrix (single/nested/recursive/early-return/multi-thread/frame-boundary/
incomplete) and gates G1 (disabled = no evaluation), G3 (clock monotonicity),
and G13 (incomplete scopes exported as `"incomplete":true`). The pinned toolchain
(Clang 18 + Conan + Catch2) runs the authoritative warnings-as-errors, ASan/UBSan
and build-budget checks in CI.
