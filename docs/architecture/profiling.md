# Profiling Subsystem — Baseline Architecture & Design

> Status: **Draft / independent baseline.** This document is a first-principles design
> produced *before* consulting any historical profiling references, so that the
> architecture is grounded in Ludus's actual code and constraints rather than in a
> particular prior art. A later pass may reconcile it against external references.
>
> Audience: engine engineers. Assumes familiarity with the logging redesign
> (`docs/decisions/0004-*`, `.kiro/specs/logging-redesign`), the assertion design
> (`docs/architecture/assertions.md`), the STL policy (`docs/decisions/0003-*`), and
> the build-flavor system (`cmake/EngineBuildFlavor.cmake`).

---

## 0. Executive summary

Ludus does not need "a profiler." It needs **three small, independent subsystems** with
almost no shared machinery, plus a thin shared clock:

| Subsystem | What it answers | Ships now? |
| --- | --- | --- |
| **Trace** (CPU scope + event stream) | Where did frame time go? Which thread/scope caused a hitch? | **Yes — Phase 1** |
| **Counters** (named scalar metrics) | How many draw calls / jobs / bytes this frame? | **Yes — Phase 2** |
| **GPU trace** | How expensive is a render pass on the GPU? | **Deferred until an RHI exists (Milestone 2+)** |
| **Memory tracking** | Which sites/tags allocate? | **Deferred until the allocator exists** |

The unifying insight: these are **different data models with different lifetimes and
different hot-path costs**, and forcing them into one "diagnostics framework" would make
each worse. The only thing they legitimately share is a **monotonic timestamp source**
and the **event-sink/backend transport pattern** already proven by logging.

The recommended baseline is a **continuous, ring-buffered, per-thread trace** with a
compile-time-gated RAII scope macro whose enabled hot path is *timestamp + one relaxed
store into a thread-local buffer* (no lock, no allocation, no string work), and whose
disabled hot path is *nothing* (the macro expands to `((void)0)` and the name string is
never emitted). Collection, tree reconstruction, aggregation, and visualization all
happen **off the hot path** — mostly **off-process**, by exporting a standard trace
format (Perfetto/Chrome JSON first; a compact Ludus binary later) rather than building an
in-engine viewer.

We deliberately **do not** build: an in-engine flame-chart UI (delegate to
Perfetto/Tracy), a sampling profiler (delegate to `perf`/VTune), or a GPU capture tool
(delegate to RenderDoc/PIX/Nsight). We build only the instrumentation that carries
**semantic engine context** external tools cannot know (scope names, job identity, frame
boundaries, pass names, counter values).

---

## 1. Goals and non-goals

### 1.1 Problems the profiler must solve

1. **CPU frame attribution.** Given a frame, show where wall-clock time went, as a
   per-thread hierarchy of named scopes.
2. **Hitch diagnosis.** When a frame exceeds a budget, retain enough recent trace data to
   see *what ran* during the spike, on *which thread*, with minimal perturbation of the
   spike itself.
3. **Per-pass / per-system cost.** Attribute time to a render pass, a system update, or a
   job, by name, cheaply enough to leave enabled in Development builds.
4. **Frame-to-frame workload change.** Cheap always-on scalar counters (draw calls,
   visible objects, pending jobs, transient bytes) sampled once per frame so trends are
   visible without a full trace.
5. **Cross-thread / cross-job correlation.** Follow one logical unit of work even when it
   is a scope on the render thread, later a job on a worker (once a job system exists).
6. **Historical inspection.** Export a capture to a file the team can open later in a
   standard viewer and diff against another capture.

### 1.2 Explicit non-goals

- **Not a sampling profiler.** We do not periodically sample call stacks. Statistical
  hot-function attribution is `perf`/VTune/Instruments/Superluminal territory; they see
  inlined frames and libc we cannot instrument. Our value is *semantic* scopes.
- **Not an in-engine flame-chart/timeline UI (initially).** Rendering a timeline requires
  the very renderer/UI we are trying to profile and would be a large maintenance surface.
  Export to Perfetto/Tracy instead. A minimal *text/overlay* readout of counters and top
  scopes is in scope; a full interactive UI is not.
- **Not a GPU capture/debug tool.** No shader debugging, no pixel history, no resource
  inspection. Engine-side GPU timing (queue timestamps around named passes) only, and only
  once an RHI exists.
- **Not a general telemetry/metrics platform.** No time-series database, no aggregation
  server, no tags-cardinality engine. Counters are a fixed, low-cardinality set of named
  scalars, nothing more.
- **Not a replacement for the logging breadcrumb/crash path.** Logging already owns crash
  breadcrumbs; the profiler does not duplicate that.

---

## 2. Why three subsystems, not one

A single "diagnostics framework" is the wrong default. Decision, per candidate concept:

| Concept | Home | Rationale |
| --- | --- | --- |
| CPU scope profiling | **Trace** | The core. Nested begin/end timestamped events. |
| Hierarchical timing | **Trace** (derived off-line) | A *view* reconstructed from events, not a stored tree. |
| Frame profiling | **Trace** (frame markers) | A frame is just two special events + an epoch id. |
| Thread profiling | **Trace** (per-thread buffers) | Thread is the natural buffer partition. |
| Job-system profiling | **Trace** (logical-id field on events) | Same event stream + a `JobId` correlation field. |
| Event markers | **Trace** (instant events) | A zero-duration event variant. |
| Statistics (per-scope aggregates) | **Trace consumer** (off-line) | Aggregation is a post-process over the event stream. |
| Counters / gauges | **Counters** (separate) | Scalar, per-frame, no timestamps, no nesting — a different data model. |
| GPU profiling | **GPU trace** (separate) | Async result readback, query pools, N-frames-in-flight latency — a different lifetime model. |
| Memory / allocation tracking | **Memory** (separate) | Hooks the allocator, not the clock; overhead profile is entirely different. |
| Hitch detection | **Trace policy** (a consumer of frame time) | A trigger rule over frame duration, not a data type. |
| Frame capture / trace recording | **Trace transport** | The ring-buffer + export mechanism of Trace. |
| Sampling profiling | **External** | Out of scope (§1.2). |
| Telemetry | **External / out of scope** | Out of scope (§1.2). |

**Shared:** a monotonic clock (§5) and the sink/backend transport pattern proven by
logging. **Not shared:** data records, buffers, aggregation, or configuration. Each
subsystem compiles independently; a TU that only needs counters must not pay for trace.

---

## 3. Overall architecture

The conceptual flow in the brief is broadly right for **Trace**, with the crucial
constraint that everything from "central aggregation" onward happens **off the hot path,
and preferably off-process**:

```text
                          HOT PATH (per-thread, lock-free)          OFF PATH (one collector thread / off-process)
  instrumentation site ─┐
   PROFILE_SCOPE(name)   │  timestamp + relaxed store
                         ▼
              ┌───────────────────────┐   frame flip     ┌──────────────────────┐   export     ┌─────────────────┐
              │ thread-local ring      │ ───────────────▶ │ collector (drains,   │ ───────────▶ │ Perfetto/Chrome │
              │ (fixed POD events)     │   (publish full  │ owns string table,   │  (file/      │ JSON  ·  Ludus  │
              │  + string interner     │    chunks)       │ builds capture)      │   socket)    │ binary  ·  Tracy│
              └───────────────────────┘                  └──────────────────────┘              └─────────────────┘
                         ▲                                          │
                         │ counters snapshot once per frame         └── in-engine overlay (counters + top scopes only)
                         └── frame markers (begin/end frame)
```

### 3.1 Components and responsibilities

- **Front-end (public header, ~no cost to include).** Macros `LUDUS_PROFILE_*`, an RAII
  `ScopedZone`, `constexpr` name-hash + static `ZoneDescriptor` registration, counter
  handles. Contains *no* `<chrono>`, `<format>`, `<vector>`, `<string>`; only `types.h`,
  `defines.hpp`, `source_location`, `<atomic>` forward use. This is the header included by
  thousands of TUs (§18).
- **Recording layer (per-thread).** A thread-local `ThreadTraceBuffer`: a chunked ring of
  fixed-size POD `TraceEvent` records. Producer-only writes, no atomics on the common push
  (the buffer is owned by exactly one thread). Chunks are handed to the collector via a
  lock-free SPSC/MPSC handoff (reusing the DV-queue pattern) only when full or at frame
  flip.
- **String / identity layer.** Zone names are **compile-time-hashed** to a 32-bit id and
  registered once via a static initializer into a process-wide `ZoneRegistry` (id → name,
  file, line, color). Events store only the id (§13/§14). Dynamic names go through a
  bounded runtime interner (rare, opt-in).
- **Collector (one thread, or piggy-backed on the logging worker).** Drains per-thread
  chunks, owns the authoritative string table, assembles a **capture** (a bounded window
  of events across all threads), applies the hitch trigger, and drives export. Mirrors the
  logging `AsyncBackend` lifetime discipline (§21).
- **Export / tooling boundary.** Writers that serialize a capture to Perfetto/Chrome JSON
  or a compact Ludus binary; optionally a live Tracy connection. The collector never
  *renders* anything.
- **Overlay (optional, tiny).** Reads counter snapshots and a small top-N scope aggregate
  for an on-screen HUD. Strictly a consumer; owns no data.

### 3.2 Ownership & memory model

- Per-thread buffers are owned by the thread (TLS) and registered with the collector at
  thread start (explicit `RegisterThreadForTrace`, mirroring `SetCurrentThreadName`).
- The collector owns all cross-thread state (string table, assembled capture, export
  buffers). Producers never touch collector state except through the lock-free handoff.
- All buffers are **fixed-capacity, pre-reserved at capture start**. No allocation on the
  hot path, ever. Over-capacity → drop with a counted `DroppedEvents` stat (§21), never
  block, never grow.

---

## 4. CPU scope profiling (the core)

### 4.1 Shape

```cpp
void RenderShadowMaps()
{
    LUDUS_PROFILE_SCOPE(ShadowMaps);          // RAII, name is an identifier -> compile-time id
    // ...
    {
        LUDUS_PROFILE_SCOPE(CascadeSplit);
        // ...
    }
}

void Foo() { LUDUS_PROFILE_FUNCTION(); /* uses source_location for the name */ }
```

### 4.2 Design decisions

- **RAII, not begin/end calls.** A stack `ScopedZone` guarantees the end event even on
  early `return` (there are no exceptions under `-fno-exceptions`, but early returns are
  everywhere). Begin/end free functions are also exposed for the rare non-lexical case.
- **Macro boundary.** The macro exists to (a) compile out entirely below the configured
  level, (b) build a `static constexpr ZoneDescriptor` with a compile-time-hashed id and
  `source_location`, and (c) declare the RAII object with a unique name. The *logic* lives
  in a tiny inline function so codegen is trivial and debuggable.
- **String handling.** The `name` token is stringized once (`#name`) into a
  `static constexpr` descriptor; the hot path stores only the 32-bit id + timestamps.
  **No string is copied or formatted at the site.** (Contrast logging, which must copy the
  message; a zone name is a literal with static lifetime, so we store the id and let the
  registry hold the name — like `LogCategory`.)
- **`source_location`.** Captured into the descriptor at compile time (constexpr), not at
  runtime, so `PROFILE_FUNCTION` costs nothing extra vs `PROFILE_SCOPE`.
- **Nesting / recursion.** Reconstructed off-line from begin/end ordering per thread
  (§4-hierarchy). Recursion is fine: each entry is its own begin/end pair; the same id
  simply appears nested under itself.
- **Thread ownership.** A scope belongs to the thread it began on. A zone must begin and
  end on the same thread (enforced by RAII lexical scope). Work that hands off to another
  thread is modeled as a *new* zone on that thread plus a `JobId` correlation (§7), not as
  a single zone spanning threads.
- **Cross-frame scopes.** Allowed. A zone open across a frame boundary is attributed by
  its begin frame; the frame-flip does not close open zones (§8).
- **Compile-out.** Below `LUDUS_TRACE_LEVEL`, `LUDUS_PROFILE_SCOPE(x)` expands to
  `((void)0)`; the descriptor is not emitted and `x` is not evaluated.

### 4.3 Expected generated code (enabled path)

For `LUDUS_PROFILE_SCOPE(ShadowMaps)` the enabled expansion is conceptually:

```cpp
static constexpr ludus::profiling::ZoneDescriptor ludus_zone_desc_42{
    /*id=*/ ludus::profiling::HashZone("ShadowMaps"),   // compile-time constant
    /*name=*/ "ShadowMaps", /*loc=*/ std::source_location::current()};
ludus::profiling::ScopedZone ludus_zone_42{ludus_zone_desc_42};   // ctor: begin event
// ... dtor at scope end: end event
```

`ScopedZone::ScopedZone` inlines to roughly:

```asm
    ; tls = &t_trace_buffer   (one TLS load, cached per function by the compiler)
    ; if (!tls->enabled) return;         ; single predicted-not-taken branch when capturing
    rdtsc / clock_gettime → ticks        ; §5: one timestamp read
    mov  slot.id,    imm32                ; store descriptor id
    mov  slot.ticks, ticks               ; store timestamp
    mov  byte slot.kind, BEGIN
    add  tls->head, sizeof(TraceEvent)   ; bump ring head (relaxed, no atomic on owner thread)
```

That is **one TLS access, one timestamp, ~3–4 stores, one branch** — no lock, no
allocation, no atomic RMW, no string. The destructor is the same minus the id store
(kind = END). Target: **≤ ~20–40 ns** per scope on the enabled path, dominated by the
timestamp (§5, §19). Disabled path: **zero instructions**.

---

## 5. Timing

- **Clock:** reuse the logging precedent — a monotonic source. Promote logging's private
  `internal::GetMonotonicTicks()` (currently `std::chrono::steady_clock` in ns, in
  `formatter.cpp`) into a **shared foundation clock** so trace and logging agree on an
  epoch. This is the safe, portable baseline and already trusted in the codebase.
- **Fast path option (later, measured):** on x86-64, `rdtsc`/`rdtscp` is materially
  cheaper than `clock_gettime` and is what most engine profilers use, but it requires:
  (a) an invariant-TSC check at startup, (b) a measured ticks→ns calibration, and (c)
  awareness that TSC is *not* guaranteed synchronized across sockets. **Decision:** ship on
  `steady_clock` ns; add a `rdtscp` backend behind the clock abstraction only after
  measuring that `steady_clock` overhead dominates the hot path (§26 open question). The
  instrumentation site must not care which backend is used — it calls one inline
  `NowTicks()`.
- **Cross-core sync:** store raw per-thread ticks; do **not** attempt cross-core
  correction on the hot path. If a TSC backend is adopted, pin calibration to one reference
  and record per-thread base offsets in the collector for display alignment only.
- **Wall-clock:** derive human-readable time off-line via the existing
  `SessionClockAnchor` pattern; never on the hot path.

---

## 6. Threading model

Follow the logging module's proven concurrency shape, adapted for far higher event rates:

- **Per-thread ring buffers in TLS.** Each thread that opts in (`RegisterThreadForTrace`)
  gets a `ThreadTraceBuffer`. Pushing an event is single-writer, so the common push needs
  **no atomics** — just a bump of a plain `head` index into the current chunk.
- **Chunked handoff, not per-event MPSC.** A per-event MPSC queue (logging's model) is too
  slow for millions of events/sec. Instead, each thread fills fixed-size **chunks**
  (e.g. 64 KiB of POD events); when a chunk fills or at frame-flip, the full chunk is
  published to the collector via the DV-style bounded MPSC queue (reused from logging) and
  the thread grabs a fresh chunk from a pre-allocated free-list. Only the *chunk pointer*
  crosses the queue, so cross-thread synchronization is amortized over thousands of events.
- **Double-buffering at frame flip.** `RegisterThreadForTrace` pre-allocates ≥2 chunks per
  thread so a thread never stalls waiting for the collector.
- **Thread naming & dynamic threads.** Reuse `SetCurrentThreadName`; the trace registers
  the TLS thread id/name (logging already assigns a `uint32 ThreadId`). Threads created
  later just register on first scope. Thread destruction flushes the partial chunk (§21).
- **Render thread / worker threads:** all identical from Trace's view — each is a producer
  with its own buffer. No special case until a real render thread exists.

---

## 7. Job-system profiling

There is **no job system yet** (confirmed: `docs/architecture/milestone-1.md` defers it).
So we do **not** build job correlation now. We only reserve the seam:

- Every `TraceEvent` carries an optional **`uint64 JobId`** field (0 = none). When a job
  system lands, the scheduler stamps the current job id into TLS on dispatch; scopes read
  it into their events. Off-line, the collector groups events by `JobId` to show logical
  work independent of which worker executed it:

  ```text
  Job 821 (AnimationUpdate)
    ├─ PoseEvaluation      [worker 3]
    └─ SkinningPreparation [worker 5]
  ```

- **Cost:** one extra `uint64` field in the record and one TLS read at scope begin. That is
  cheap enough to include from day one so the format is stable, but the *correlation logic*
  and the scheduler stamping are deferred until the job system exists. Do not build a task
  graph model speculatively.

---

## 8. Frame model

There is **no engine frame loop yet**. The profiler therefore *defines* a minimal frame
API the future Application layer will call, rather than depending on one:

- `LUDUS_PROFILE_FRAME()` (or `Profiler::FrameMark()`) emits a **frame-boundary event** on
  the calling thread and increments a monotonic **frame epoch** (`uint64`).
- A frame is **not** a container that owns events; it is just a pair of boundary markers
  plus an epoch id stamped as metadata. This deliberately handles the awkward cases the
  brief warns about:
  - Zones open across a boundary are attributed to their begin epoch, not split.
  - **Multiple frames in flight / async GPU:** the CPU frame epoch and the GPU frame epoch
    are independent counters; GPU events carry their own epoch and are correlated to CPU
    frames by submission id (§9), not by wall-clock.
  - **Editor / non-game workloads:** frame marks are optional. A tool that never calls
    `FrameMark` still produces a valid, if frameless, trace.
- The collector uses frame boundaries only to (a) drive per-frame counter snapshots,
  (b) evaluate the hitch trigger, and (c) chunk exports into per-frame spans for viewers.

---

## 9. GPU profiling — deferred, but designed

**Do not build until an RHI/render thread exists (Milestone 2+).** When it does:

- **Mechanism:** timestamp queries written into the command buffer at named pass
  begin/end, resolved from a per-queue query pool. Never a synchronous readback — results
  arrive **N frames later** and are matched back to the originating pass by a monotonically
  increasing **query id** + the CPU frame epoch captured at record time.
- **No GPU stalls:** query pools are sized for `frames_in_flight × max_passes`; the CPU
  reads results only for frames whose fence has already signaled. If results aren't ready,
  the pass is reported as "pending" and filled in later — never waited on.
- **API mirrors CPU:** `LUDUS_PROFILE_GPU_SCOPE(cmdbuf, ShadowPass)` — an RAII object that
  writes begin/end timestamp-query commands into `cmdbuf` and records a CPU-side pending
  entry. Graphics vs compute vs async-compute queues each get their own timeline lane; the
  event carries a `QueueId`.
- **Nested GPU scopes** reconstruct hierarchically just like CPU scopes, per queue.
- **Boundary with vendor tools:** built-in GPU timing answers "how long did *ShadowPass*
  take on the graphics queue?" It does **not** replace RenderDoc/PIX/Nsight for
  per-draw/pixel/resource analysis. We also emit vendor debug markers
  (`vkCmdBeginDebugUtilsLabel` / PIX events) from the same GPU-scope macro so captures in
  those tools show our pass names — near-zero cost, high value.

---

## 10. Memory profiling — deferred until the allocator exists

There is **no allocator yet** (`docs/decisions/0003-*` defers it). Memory profiling is
**not** part of Trace; it hooks the allocator, not the clock, and has a completely
different overhead profile. Design it *with* the allocator, not now. Requirements to carry
forward:

- **Tiers (opt in per build/capture):**
  - *Always available (all builds incl. Release):* aggregate per-allocator/per-tag totals
    and high-water marks — a handful of atomics updated on alloc/free. Strategically cheap;
    do **not** compile out (§20).
  - *Development-only:* per-frame allocation counts/bytes per tag (counter snapshots).
  - *Capture-only / opt-in:* per-allocation records with **call-site** attribution
    (captured cheaply as a return-address, symbolized off-line — never symbolize on the hot
    path). Lifetime tracking, fragmentation, and timelines are derived off-line from
    alloc/free records.
- **Overhead reality:** recording *every* allocation with a stack is expensive and must be
  a capture-only mode. Tag/category totals are cheap enough to always keep.
- **Integration:** the allocator takes a `MemoryTag` at every allocation (a
  compile-time-hashed id, same scheme as zones/categories). The memory profiler is a
  consumer of allocator events, feeding the *same* counter and capture infrastructure —
  reuse, don't duplicate.

---

## 11. Counters and metrics (Phase 2, separate subsystem)

A small, fixed-cardinality set of named scalars — **not** a metrics platform.

- **Kinds:**
  - **Counter** — accumulates within a frame, reset (or delta-reported) at frame mark
    (e.g. `DrawCalls`, `Triangles`).
  - **Gauge** — instantaneous last-write value (e.g. `PendingJobs`, `TransientMemory`).
  - (Histograms are **off-line** derivations from the trace/counter stream if ever needed;
    not a hot-path type.)
- **Representation:** each counter is a process-static handle (compile-time id + name, like
  a `LogCategory`) backed by one `std::atomic<int64>` (or a padded per-thread shard summed
  at frame mark to avoid contention on hot counters). Update = one relaxed atomic add /
  store. No timestamps, no nesting, no strings on the hot path.
- **Snapshot:** at `FrameMark`, the collector copies the counter array into the frame
  record for history and overlay. That is the only cross-thread step.
- **API:** `LUDUS_PROFILE_COUNT(DrawCalls, 1);` and `LUDUS_PROFILE_GAUGE(PendingJobs, n);`
  Compiled out below the configured level like scopes.

---

## 12. Hitch detection (a Trace policy, not a data type)

- The collector tracks a **rolling ring of recent trace chunks** continuously (the
  always-on capture window, e.g. last N frames).
- A **trigger rule** — default `frame_time > budget` (configurable multiplier over a
  moving median) — arms a **pre/post capture**: keep the pre-trigger buffer already in the
  ring plus M post-trigger frames, then export the window automatically (to file or Tracy).
- **Minimum disturbance:** the trigger fires on the collector thread, *not* the hot path.
  The spiking thread does nothing extra — its events were already in the ring. Export
  happens after the fact. This is the whole point of continuous ring recording: you can
  diagnose a hitch you didn't know was coming.

---

## 13. Storage model

- **Fixed-size POD `TraceEvent`**, packed and cache-friendly. Sketch (subject to
  measurement; target 16–24 bytes):

  ```cpp
  struct TraceEvent            // POD, trivially copyable, no ctor work
  {
      uint64 Ticks;            // monotonic timestamp (§5)
      uint32 ZoneId;           // compile-time hash -> ZoneRegistry (§14)
      uint16 Kind;             // Begin | End | Instant | FrameMark | GpuBegin | ...
      uint16 Flags;            // truncation, has-jobid, queue id, etc.
      // optional trailing JobId (§7) stored in a parallel stream when Flags say so,
      // to keep the common event small.
  };
  ```

- **Why fixed-size:** trivial ring math, predictable memory, branch-free push, easy SPSC
  handoff. Variable-length records (for dynamic strings/payloads) go to a **separate
  side-channel stream**, keeping the common event tiny and hot.
- **String table:** ids only in events; names live once in the `ZoneRegistry`. The
  collector serializes the table once per export. **No strings allocated or copied at the
  site.**
- **Write speed / cache:** one contiguous append per event; chunk-aligned; the producer
  touches only its own current chunk cache lines.
- **Serialization:** off the hot path, in the collector/exporter. Perfetto JSON is
  verbose but universal (Phase 1); a compact binary is a later optimization once volumes
  justify it.

---

## 14. Instrumentation identity

- **Static names → compile-time FNV-1a hash** to a `uint32` id, reusing the exact pattern
  proven for `LogCategory` (`HashLogCategory`, `consteval`). A `static constexpr
  ZoneDescriptor{ id, name, file, line }` is registered into the process `ZoneRegistry`
  via a static initializer; events carry only `id`.
- **Collisions:** 32-bit FNV over a few thousand zone names has negligible but nonzero
  collision probability. Mitigation: the registry stores the full name per id and asserts
  (Development builds) on a *distinct name mapping to an existing id*. If it ever bites,
  widen to 64-bit — the event field is the only change.
- **Dynamic names (rare):** an opt-in bounded runtime interner
  (`Profiler::InternName(std::string_view)`), fixed-capacity, drop-with-count on overflow.
  Discouraged on the hot path; meant for e.g. per-asset pass names resolved once.
- **Binary size / compile time:** ids are integers; descriptors are `constexpr` PODs in a
  dedicated section. No per-site template instantiation, no formatting machinery pulled in
  (§18).

---

## 15. Recording strategy

**Hybrid, defaulting to continuous ring recording:**

- **Continuous ring (default in Development/Profile):** always recording into a bounded
  rolling window; cheap; enables hitch capture with zero warning.
- **Explicit capture session:** `Profiler::BeginCapture()/EndCapture()` widens the window
  and pins export for a bounded interval — for deliberate investigations.
- **Triggered capture:** the hitch policy (§12) auto-exports a window.
- **Frame snapshots:** counters snapshot every frame regardless (they're tiny), so
  frame-trend data is always available even when full trace is off.

This hybrid gives the best ROI: always-on cheap counters + always-on bounded trace ring +
on-demand/triggered full export.

---

## 16. Tooling and visualization

**Boundary decision: the engine records; external tools visualize.** Ranked by ROI:

1. **Export to Perfetto / Chrome trace JSON (Phase 1, highest ROI).** Universal, free,
   powerful timeline/flame view, no engine UI code, opens in any browser
   (`ui.perfetto.dev`). This is the primary deliverable of Phase 1.
2. **Tracy live/connected (Phase 3, high ROI).** Purpose-built for exactly this
   instrumentation style (scopes, frames, counters, lock-free client), superb UI, minimal
   integration. Strong candidate as an *optional* live backend behind the same macros.
3. **Compact Ludus binary + tiny converter (later).** Only if JSON volume becomes a
   bottleneck.
4. **In-engine overlay (small, Phase 2).** Counters + top-N scopes as text/HUD only. High
   value for at-a-glance dev feedback; low maintenance. **Not** a full interactive timeline
   — that would require the renderer/UI under test and is a large surface.

Comparison of the four viewer options:

| Option | Cost to build | Fidelity | When |
| --- | --- | --- | --- |
| In-game overlay (counters/top-N) | Low | Low | Phase 2 |
| Editor-integrated viewer | High | High | Not until an editor exists; likely never (use Perfetto) |
| External desktop viewer (Perfetto/Tracy) | ~Zero (reuse) | High | Phase 1/3 |
| Exported trace format | Low | High (in external viewer) | Phase 1 |

Do **not** build an editor viewer speculatively; there is no editor and Perfetto covers it.

---

## 17. External profiler integration

Determine what *must* be engine-native vs delegated:

| Capability | Owner | Why |
| --- | --- | --- |
| Named CPU scopes, frames, counters, job/pass semantics | **Engine (native)** | Only the engine knows these names/relationships. This is our core value. |
| Timeline / flame-chart visualization | **External (Perfetto/Tracy)** | Solved, excellent, free; no reason to rebuild. |
| Statistical hot-function sampling | **External (perf/VTune/Superluminal)** | Sees inlined/library frames we can't instrument. |
| GPU per-draw / pixel / resource capture | **External (RenderDoc/PIX/Nsight)** | Deep vendor integration we can't match. |
| GPU pass *timing* + debug-marker names | **Engine (native)** feeding external | Cheap engine-side; also annotate vendor captures via debug labels. |

**Format strategy:** export **Perfetto/Chrome JSON** first (universal), add a **Tracy**
backend behind the same macros later (best live UX). Keep the front-end macros
tool-agnostic so switching/adding a backend never touches instrumentation sites.

---

## 18. Compile-time performance (a first-class constraint)

The profiling header will be included in ~every TU, so it must be as cheap as `log.hpp`
became after ADR 0004 (CI ~491 ms) — ideally cheaper. Rules:

- **Public header includes only:** `types.h`, `defines.hpp`/`compiler.hpp`,
  `<source_location>`, and forward-use of `<atomic>`. **Forbidden in the public header:**
  `<chrono>`, `<format>`, `<string>`, `<vector>`, `<unordered_map>`, iostreams,
  `<filesystem>`.
- **No template instantiation per site.** The macro creates a `constexpr` POD descriptor
  and a non-template RAII object. All heavy logic (timestamp source, queue, collector) is
  type-erased behind the `.cpp` — the same discipline ADR 0004 mandates.
- **Compile-time hashing** is a tiny `constexpr` loop (already proven for categories);
  negligible cost.
- **Budgeted in CI** via `config/build_budget.json` + `-ftime-trace` (ADR 0005). The
  profiling public header gets its own budget line, calibrated on the CI runner, so a heavy
  include sneaking in fails the gate.
- **Static metadata** (`ZoneDescriptor`s) lives in a dedicated section; it adds a small,
  bounded amount to binary size proportional to the number of *distinct* zones, not to the
  number of *sites*.

---

## 19. Runtime overhead (estimates; to be validated in §25)

| Path | Expected cost | Notes |
| --- | --- | --- |
| **Disabled instrumentation** | **0** | Macro → `((void)0)`; nothing emitted, name not evaluated. |
| **Enabled CPU scope (begin+end)** | ~2× timestamp + ~6–8 stores + 2 branches | Dominated by the clock read (§5). ~40–80 ns/scope on `steady_clock`; less with `rdtscp`. |
| **Counter update** | 1 relaxed atomic add | ~few ns; shard hot counters to avoid contention. |
| **Memory alloc tracking (tag totals)** | 1–2 atomics on alloc/free | Cheap enough to keep in all builds. |
| **Memory alloc tracking (per-alloc capture)** | return-address capture + record push | Capture-only; not always on. |
| **GPU marker** | 2 query writes into cmdbuf + 1 CPU pending-entry push | No stall; result readback is deferred. |

Hot-path considerations: **TLS lookup** is one load (compiler caches per function);
**branches** are the enabled check (predicted not-taken when idle, taken when capturing);
**memory writes** touch only the producer's current chunk (cache-friendly); **no
synchronization** on the common push (chunk handoff amortizes it). **Always-on vs
capture-only:** counters and memory *tag totals* stay active continuously; full trace event
recording and per-allocation capture are enabled only during a capture window (default
continuous-but-bounded in Development/Profile, off in Release — §20).

---

## 20. Build configurations

Integrate with the existing four flavors (`cmake/EngineBuildFlavor.cmake`), gated by a new
generated policy value `LUDUS_TRACE_LEVEL` (mirroring the `assert_config.hpp.in` pattern,
SDK-owned, no per-TU override):

| Flavor | CPU scopes | Counters | Mem tag totals | Full trace capture | Overlay |
| --- | --- | --- | --- | --- | --- |
| **Debug** | On | On | On | On (bounded ring) | Available |
| **Development** | On | On | On | On (bounded ring) | Available |
| **Profile** | On | On | On | On (wider window; this flavor exists to profile) | Available |
| **Release** | **Compiled out** | Cheap gauges only (opt-in) | **Kept** (a few atomics) | Off | Off |

- A dedicated **Profile** flavor already exists and is exactly the right home for wide
  capture windows (asserts already off there, RelWithDebInfo). **Naming caution:** there is
  already a `Profile` *build flavor* and a `linux-clang-profile` preset — the runtime
  subsystem must be named **Trace/Profiling subsystem** in code to avoid conflation.
- **Do not blindly strip everything from Release.** Aggregate memory tag totals and a few
  strategic gauges are ~free and valuable for field diagnostics; keep them behind an opt-in
  `LUDUS_TRACE_LEVEL` tier rather than a hard `#if 0`.

---

## 21. Failure behavior

Profiling must never destabilize the engine (same philosophy as logging/assertions):

- **Buffer overflow / saturation:** drop events, increment `DroppedEvents`; never block,
  never allocate, never grow. A saturated ring degrades resolution, not stability.
- **Dropped events / malformed nesting:** the off-line tree builder tolerates unmatched
  begin/end (a begin with no end → synthesized end at chunk boundary; an end with no begin
  → discarded with a counted warning). Crashes mid-scope thus still yield a usable partial
  trace.
- **Thread destruction:** the thread flushes its partial chunk on exit (a `thread_local`
  destructor / explicit `UnregisterThreadForTrace`), copying any producer-lifetime data
  (mirrors the logging thread-name-dangling fix found under ASan).
- **OOM / init failure:** all buffers are pre-reserved at capture start; if reservation
  fails, tracing stays disabled and the engine runs normally (report once via logging).
- **Collector lifetime:** mirror `AsyncBackend::Stop` — signal stop, join with a deadline,
  and **refuse to free buffers while a producer/consumer might still touch them**. Never
  dangle.
- **Crash during capture:** partial chunks already handed to the collector are exportable;
  the in-flight producer chunk is lost — acceptable. Optionally flush the ring on the fatal
  path (after the assertion emergency report, never before it).

---

## 22. Proposed API (small and orthogonal — six concepts)

```cpp
#include <ludus/profiling/profiling.hpp>   // cheap to include (§18)

// 1. CPU scope (RAII, compile-time id from the identifier token)
LUDUS_PROFILE_SCOPE(ShadowMaps);

// 2. Function scope (name derived from source_location)
LUDUS_PROFILE_FUNCTION();

// 3. Frame marker (defines a frame boundary + advances the frame epoch)
LUDUS_PROFILE_FRAME();

// 4. Counter / gauge (scalar metric; Count accumulates, Gauge sets)
LUDUS_PROFILE_COUNT(DrawCalls, 1);
LUDUS_PROFILE_GAUGE(PendingJobs, pending);

// 5. Instant event / marker (zero-duration annotation)
LUDUS_PROFILE_MARK(LevelStreamedIn);

// 6. GPU scope (deferred; RAII over a command buffer, mirrors CPU scope)
LUDUS_PROFILE_GPU_SCOPE(cmd, ShadowPass);

// Lifecycle / control (non-macro, called by the platform/app layer):
namespace ludus::profiling {
    void Initialize(const TraceConfig&);   // pre-reserve buffers
    void Shutdown();                        // AsyncBackend-style deadline join
    void RegisterThreadForTrace(std::string_view name);
    void UnregisterThreadForTrace();
    void BeginCapture(); void EndCapture(); // explicit session
    void ExportPerfetto(std::string_view path);
    TraceHealth GetHealth();                // dropped events, high-water, etc.
}
```

Six macros for six concepts. No `PROFILE_SCOPE_COLORED`, `PROFILE_SCOPE_N`,
`PROFILE_SCOPE_CATEGORY`, etc. — optional attributes (color, category, dynamic name) are
overloads/optional trailing args on the same macro, not new macros.

---

## 23. Target architecture (concrete)

```text
┌──────────────────────────────────────────────────────────────────────────────────────┐
│ FRONT-END  (public header: profiling.hpp — types.h, defines.hpp, source_location only) │
│  LUDUS_PROFILE_SCOPE/FUNCTION/FRAME/COUNT/GAUGE/MARK/GPU_SCOPE                          │
│  → constexpr ZoneDescriptor (compile-time FNV-1a id) + non-template ScopedZone RAII     │
└───────────────────────────────┬────────────────────────────────────────────────────────┘
     inline, type-erased into    │  NowTicks()  (shared foundation clock, §5)
     ludus_profiling.cpp         ▼
┌──────────────────────────────────────────────────────────────────────────────────────┐
│ RECORDING  (per producer thread, TLS)                                                   │
│  ThreadTraceBuffer: chunked ring of POD TraceEvent (single-writer, no atomics on push)  │
│  ZoneRegistry (process-static id→name/file/line)   ·   per-thread counter shards        │
│  full chunk / frame-flip ──► DV-style bounded MPSC chunk queue (reused from logging)     │
└───────────────────────────────┬────────────────────────────────────────────────────────┘
                                 ▼
┌──────────────────────────────────────────────────────────────────────────────────────┐
│ COLLECTOR  (one thread; may piggy-back logging worker; AsyncBackend lifetime discipline)│
│  drains chunks · owns string table · maintains rolling ring window · counter snapshots  │
│  hitch trigger (§12) · assembles Capture                                                │
└───────────────┬───────────────────────────────────────────────┬────────────────────────┘
                ▼                                                 ▼
      ┌───────────────────────┐                        ┌────────────────────────────┐
      │ EXPORT (off-process)  │                        │ OVERLAY (in-engine, tiny)   │
      │ Perfetto/Chrome JSON  │                        │ counters + top-N scopes HUD │
      │ · Tracy · Ludus binary│                        │ (read-only consumer)        │
      └───────────────────────┘                        └────────────────────────────┘
```

- **Hot-path behavior:** timestamp + relaxed stores into TLS chunk; no lock/alloc/string.
- **Thread interactions:** producers ↔ collector only via the lock-free chunk queue.
- **Ownership:** thread owns its buffer; collector owns all shared/export state.
- **Memory model:** fixed, pre-reserved; drop-on-overflow; never grows on the hot path.
- **Tooling boundary:** collector emits data; visualization is external (+tiny overlay).

---

## 24. Implementation roadmap

| Phase | Functionality | Dependencies | Value | Complexity | Perf risk | Tests / benchmarks |
| --- | --- | --- | --- | --- | --- | --- |
| **0. Shared clock** | Promote `NowTicks()` to foundation; ticks↔ns conversion | none | Enables everything | Low | Low | Micro-bench clock read; monotonicity test |
| **1. CPU Trace MVP** | `PROFILE_SCOPE/FUNCTION/FRAME`, TLS chunk ring, ZoneRegistry, collector, **Perfetto export** | Phase 0, clock, MPSC queue (exists) | **Highest** — answers "where did frame time go" | Medium | Medium (hot-path push) | Overhead micro-bench; multi-thread event flood; tree-reconstruction unit tests; build-budget line |
| **2. Counters + overlay** | `PROFILE_COUNT/GAUGE/MARK`, per-frame snapshot, text HUD | Phase 1 (frame marks) | High — cheap always-on trends | Low–Med | Low | Counter contention bench; snapshot correctness |
| **3. Hitch capture + Tracy** | Rolling ring, trigger policy, auto-export; optional Tracy backend | Phase 1 | High — catch spikes you didn't expect | Medium | Low (off hot path) | Synthetic-hitch trigger test; pre/post window correctness |
| **4. GPU Trace** | `PROFILE_GPU_SCOPE`, query pools, deferred readback, vendor labels | **RHI/render thread (Milestone 2+)** | High once GPU exists | High | Med (query mgmt, no stalls) | No-stall assertion; N-in-flight correctness |
| **5. Memory profiling** | Tag totals (all builds), per-frame counts (dev), per-alloc capture (opt-in) | **Allocator (Milestone ?)** | High for mem bugs | High | Med (per-alloc mode) | Alloc/free tag accounting; overhead of capture mode |

Ship Phases 0–1 first; they deliver most of the value. 4 and 5 are gated on subsystems
that do not yet exist and must be co-designed with them.

---

## 25. Validation (benchmarks & gates)

- **Scope overhead micro-bench:** enabled vs disabled `PROFILE_SCOPE` in a tight loop;
  report ns/scope for `steady_clock` vs (later) `rdtscp`. Target enabled ≤ ~80 ns, disabled
  = 0 (assembly-verified).
- **Millions of events:** generate ≥10M events/sec across threads; verify no allocation on
  the hot path (ASan/heaptrack), bounded memory, measured drop rate at saturation.
- **Multithreaded generation:** N producer threads flooding; verify collector keeps up or
  degrades by dropping (never blocking); TSan-clean.
- **Buffer saturation:** drive past capacity; assert stability + correct `DroppedEvents`
  accounting.
- **Frame capture:** produce a known synthetic workload, export Perfetto JSON, validate it
  loads in `ui.perfetto.dev` and hierarchy matches expectations.
- **Memory footprint:** measure per-thread buffer + collector window at default config;
  document the fixed cost.
- **Build-time impact:** `-ftime-trace` self-parse of `profiling.hpp` under the CI budget
  (ADR 0005); regression fails the gate.
- **Executable-size impact:** measure `.text`/metadata growth per 1000 zones and in a
  Release build (should be near-zero when compiled out).

---

## Final deliverable

### 1. Minimal profiler architecture to build now
A **continuous, ring-buffered CPU Trace** with: a shared monotonic clock (`steady_clock`
ns); a cheap-to-include front-end (`PROFILE_SCOPE/FUNCTION/FRAME`) whose enabled hot path
is *timestamp + relaxed stores into a TLS chunk ring* and whose disabled path is *nothing*;
compile-time FNV-1a zone ids into a `ZoneRegistry`; per-thread chunked buffers handed to a
single collector (reusing logging's DV-MPSC queue and `AsyncBackend` lifetime discipline)
via lock-free chunk publication; and **export to Perfetto/Chrome JSON**. Add **counters +
a tiny text overlay** (Phase 2) and **hitch auto-capture** (Phase 3) immediately after.

### 2. Capabilities deliberately postponed
- **GPU Trace** — until an RHI/render thread exists (Milestone 2+); designed (§9) but not built.
- **Memory profiling** — until the allocator exists; designed in tiers (§10) but not built.
- **Job correlation** — the `JobId` field is reserved in the record now; correlation logic waits for a job system (§7).
- **In-engine interactive timeline UI** — likely never; use Perfetto (§16).
- **Compact binary trace format** — only if JSON volume becomes a bottleneck.

### 3. Capabilities delegated to external tools
- Timeline/flame-chart **visualization** → Perfetto/Tracy.
- **Statistical sampling** → perf/VTune/Superluminal/Instruments.
- **GPU per-draw/pixel/resource capture** → RenderDoc/PIX/Nsight (we feed them debug-marker names).

### 4. Expected instrumentation costs
- Disabled scope: **0**. Enabled scope: **~40–80 ns** (`steady_clock`; dominated by the
  clock read, less with `rdtscp`). Counter update: **~few ns** (one relaxed atomic).
  Memory tag total: **1–2 atomics**. GPU marker: **2 cmdbuf query writes, no stall**.
  Build cost: `profiling.hpp` budgeted alongside `log.hpp` in CI. Binary size: bounded by
  distinct-zone count, near-zero when compiled out.

### 5. Implementation phases
0 Shared clock → **1 CPU Trace MVP + Perfetto export** → 2 Counters + overlay →
3 Hitch capture (+ optional Tracy) → 4 GPU Trace (needs RHI) → 5 Memory (needs allocator).
See §24.

### 6. Architectural questions requiring measurement before committing
1. **Clock backend:** is `steady_clock` overhead a material fraction of the hot path? Only
   adopt `rdtscp` (with invariant-TSC check + calibration + cross-socket caveats) if measured
   to matter (§5).
2. **Event size vs richness:** does adding `JobId`/color inline regress cache/throughput
   enough to justify a side-channel stream? Measure at target event rates (§13).
3. **Collector topology:** dedicated collector thread vs piggy-backing the logging worker —
   decide by measured drain throughput under a 10M-events/sec flood (§6, §23).
4. **Counter sharding:** do hot counters (e.g. `DrawCalls`) contend enough to need
   per-thread shards, or is a single relaxed atomic fine? Measure (§11).
5. **Chunk size / ring depth:** the sweet spot balancing handoff frequency, drop rate under
   spikes, and memory footprint — tune empirically (§6, §25).
6. **Export format volume:** Perfetto JSON size/throughput at realistic capture windows —
   decide when (if) the compact binary is worth building (§13, §16).
```

Now let me verify the document is well-formed and check whether the repo has a session branch to push to.


<invoke name="execute_bash">
<parameter name="command">cd /projects/sandbox/Ludus && echo "branch: $(git branch --show-current)" && echo "--- wc ---" && wc -l docs/architecture/profiling.md && echo "--- CONTRIBUTING? ---" && ls CONTRIBUTING.md .github/CONTRIBUTING.md docs/CONTRIBUTING.md 2>/dev/null; echo "--- default branch ---" && git symbolic-ref refs/remotes/origin/HEAD 2>/dev/null | sed 's@refs/remotes/origin/@@'; echo "--- status ---" && git status --short