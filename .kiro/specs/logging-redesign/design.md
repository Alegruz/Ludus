# FoundationLogging redesign — Design

**Status:** proposed. Traces to `requirements.md` (R-IDs) and
`docs/architecture/logging-review.md` (sections 6–14, F1–F12). Where the review
and the checked-out code diverge, the code wins and the deviation is noted.
Values marked `[MD]` are measurement-dependent; `[CD]` are reversible
conservative defaults. Nothing here is implemented yet.

---

## 0. Recommended architecture (one paragraph)

A lightweight, double-gated frontend captures bounded metadata and formats into
caller-owned fixed storage; a complete **owned** record is published to a bounded
MPSC queue; **one** backend thread owns the ordinary sinks (terminal, session
file, debugger), their buffers, rotation, and serialization. Selected producers
also copy a compact breadcrumb into a bounded producer-side ring. Intentional
breakpoint visibility uses an explicit **direct** synchronous diagnostic that
bypasses the (possibly parked) worker. Controlled fatal/emergency reporting uses
a small **independent primitive in FoundationBase**, invoked first, with normal
logging only a best-effort secondary destination. Structured envelope metadata
(session/build, monotonic time, severity/category, native thread, source,
sequence) is preserved past the sink boundary so later tools can correlate
without turning Logging into profiling or analytics infrastructure. A corrected
**synchronous** backend remains for tools and tests.

---

## 1. Header and dependency graph (R5, R6, R21, R22)

```
FoundationBase
  base/types.h            fixed-width aliases
  base/defines.hpp        build-type macros, LUDUS_INLINE
  base/diagnostic.hpp     <-- NEW: bounded emergency reporter (below Logging)
        ^                        (moved out of the logging module)
        |  (Logging depends on Base; Base never depends on Logging)
FoundationLogging (public headers — installed)
  logging/level.hpp             severities (unchanged shape)
  logging/category.hpp          descriptor + declare/define macros (no heavy incl)
  logging/log.hpp               COMMON call-site header: raw text + gating macros
                                + source metadata. NO <format>/<filesystem>/
                                <chrono>/<iostream>. Depends on level+category.
  logging/log_format.hpp        OPT-IN typed formatting: FormatArg packing +
                                restricted consteval literal checker. Depends on
                                log.hpp. Never included by log.hpp.
  logging/log_system.hpp        lifecycle/config/flush/health/statistics.
                                Directory as borrowed UTF-8 view or platform
                                adapter handle. NO public <filesystem>.
  logging/log_compat_format.hpp TEMPORARY std::format adapter (explicitly named,
                                excluded from the lightweight core; R53).
FoundationLogging (private — src/, NOT installed)
  src/internal/record.hpp       owned record + header layout
  src/internal/queue.hpp        bounded MPSC
  src/internal/backend.hpp      worker, lifecycle state machine
  src/internal/format_engine.hpp  numeric conversion (to_chars), grammar parser
  src/internal/sink.hpp         ILogSink (status-returning)
  src/sinks/*.hpp               console/file/debugger sinks
  src/internal/breadcrumb.hpp   producer-side ring
  src/internal/session.hpp      session identity, file naming, retention
```

**Rule checks.** `log.hpp` includes only `level.hpp`, `category.hpp`,
`<string_view>`, `<source_location>`, and small Base headers. The current
transitive `<format>`+`<filesystem>` exposure (F5, confirmed; `<filesystem>` ≈
64k preprocessed lines on the measurement host) is eliminated: `config.hpp` is
removed from the call-site path and the configuration surface moves to
`log_system.hpp` with no `<filesystem>` in its public signature.

**FoundationBase addition.** The emergency reporter currently lives in the
logging module (`src/emergency_logger.cpp`). It moves to
`FoundationBase::base/diagnostic.hpp` + a `.cpp`, so assertions (proposed,
absent — R30) and Base itself can report without an upward dependency on
Logging. Logging calls *into* it; it never calls Logging.

---

## 2. Public API examples (R11, R16, R17, R20, R27)

```cpp
// --- category declaration/definition (build-cheap) ---
// render_categories.hpp
LUDUS_DECLARE_LOG_CATEGORY(LOG_RENDER);            // no dynamic registration
// render_categories.cpp
LUDUS_DEFINE_LOG_CATEGORY(LOG_RENDER, "Render");   // constant-initialized, one TU

// --- common header: raw text + gating (no <format>) ---
#include <ludus/foundation/logging/log.hpp>
LUDUS_LOG_TEXT(LOG_RENDER, Info, "Swapchain recreated");     // literal text
LUDUS_LOG_TEXT(LOG_RENDER, Warning, driverMessage);          // dynamic string, NOT a format string

// --- opt-in typed formatting (bounded grammar, consistent brace handling) ---
#include <ludus/foundation/logging/log_format.hpp>
LUDUS_LOG_INFO (LOG_RENDER, "Created texture {}", textureName);
LUDUS_LOG_WARN (LOG_RENDER, "Fallback format {}", formatId);
LUDUS_LOG_ERROR(LOG_RENDER, "Upload failed for {}: {}", resourceId, status);
LUDUS_LOG_INFO (LOG_RENDER, "escaped {{braces}}");           // -> "escaped {braces}" (R17)

// --- direct debugger-critical delivery (bypasses a parked worker) ---
const DeliveryResult v = LUDUS_LOG_DEBUG_SYNC(LOG_RENDER, "Before submit {}", submitId);
// inspect v.status before LUDUS_DEBUG_BREAK();  (Debug severity + direct policy)

// --- acknowledged flush of the earlier ordinary stream ---
const FlushResult f = LogSystem::Flush(FlushKind::Visible, timeoutMs);

// --- lifecycle ---
LogInitResult r = LogSystem::Initialize(config);   // returns EFFECTIVE mode + status
LogSystem::Shutdown();                             // closes admission, drains, joins
```

Notes:
- `LUDUS_LOG_DEBUG_SYNC` = Debug severity + *direct* delivery policy, not a new
  severity; still subject to its compile/runtime gates (R9/R10/R27).
- `LUDUS_LOG_FATAL` remains a *reporting* op; termination is a separate explicit
  primitive (R30). Assertion/fatal reporting must not ride an optional Debug gate.
- Every macro resolves the category expression **once** (R15), gates before
  building arguments (R12/R13), and — for compiled-out levels — expands to
  `((void)0)` with no instantiation (R12).

### 2.1 Macro shape (exact-once category, single gate point)

The current macro evaluates the category twice (F6). The corrected shape
resolves the category into a single hidden local before gating, so the user
expression runs once and both the gate and dispatch see the same value:

```cpp
#define LUDUS_LOG_TEXT(category, level_name, text)                              \
    do {                                                                        \
        const ::ludus::foundation::logging::LogCategory ludus_cat_ = (category);\
        if (::ludus::foundation::logging::detail::Gate(                         \
                ::ludus::foundation::logging::LogLevel::level_name, ludus_cat_))\
        {                                                                       \
            ::ludus::foundation::logging::detail::SubmitText(                   \
                ::ludus::foundation::logging::LogLevel::level_name, ludus_cat_, \
                ::std::source_location::current(), (text));                     \
        }                                                                       \
    } while (false)
```

`Gate()` is the lock-free scalar predicate (R13). `source_location::current()`
stays inside the taken branch (as today), so runtime-disabled calls never
capture it. Compiled-out levels wrap the whole `do{...}while(false)` in
`((void)0)` selection exactly as today, preserving the verified argument-non-
evaluation guarantee (`category_tests.cpp`). The typed `LUDUS_LOG_INFO(...)`
family expands similarly but calls `detail::SubmitFormat(...)` with a packed
`FormatArgs` (R19) instead of `SubmitText`.

---

## 3. Category and filter model (R10, R13, R15, R22)

```cpp
enum class CategoryOverride : uint8 { Inherit = 0xFF };  // else a LogLevel value

struct LogCategory {                 // constant-initialized descriptor
    uint32 Id;                       // FNV-1a of Name (unchanged)
    std::string_view Name;           // borrowed, process-lifetime for constants
    // Override lives in a separate registry slot, not in the constexpr object,
    // so the descriptor stays literal and header-defined categories work before
    // registration.
};
```

- **Runtime read (Gate):** one relaxed atomic load of the category's override
  (Inherit ⇒ one relaxed load of the global threshold) plus one relaxed load of
  the lifecycle admission word. All scalar, lock-free, no map, no allocation
  (R13). This replaces today's `shared_lock` + `unordered_map` lookup (F10/F11)
  and makes the header comment ("id + integer comparison") finally true.
- **Override storage:** a fixed-capacity, process-lifetime table of atomic
  levels indexed by a dense **registered id**, plus a small fallback for
  unregistered categories used before startup registration (direct descriptor
  filtering still works, just without named configuration).
- **Registration** is explicit startup/module work (cold path): it validates
  name/hash **collisions** (a hash collision must not silently merge two
  categories — F10), assigns the dense id, and records the full name for any
  wire/disk dictionary. It never happens lazily on a log call.
- **Plugin unload** (if it ever exists — out of core scope, R60/non-goal): copy/
  intern descriptors into process-lifetime registry storage and drain references
  before unload; pointers into unloaded modules are not valid async metadata.

**Correction to review nuance:** `EmergencyNote` currently constructs
`LogCategory{0u, "Logging"}` whose id `0` collides with any real category that
hashes to `0`. The redesign gives the emergency/internal category a reserved,
non-hash id outside the registered range.

---

## 4. Record layout and lifetimes (R23, R24, R25, R26)

Two representations, one owned:

```cpp
// Fixed header for a queued/owned record (private). Trivially copyable.
struct RecordHeader {
    uint64 Sequence;         // successful reservation position (NOT event time)
    uint64 MonotonicTicks;   // producer steady_clock ticks; session anchors -> UTC
    uint32 NativeThreadId;   // OS thread id (R23); EngineThreadId optional/companion
    uint32 CategoryRegId;    // dense registered id (serialize dictionary name, not ptr)
    uint32 SourceId;         // interned file/function/line descriptor id
    uint32 Line;
    LogLevel Level;          // uint8
    uint8   Flags;           // Truncated | FormatError | EmergencyMirror | FieldsOmitted
    uint16  MessageSize;     // bytes of inline UTF-8 message following the header
    // optional bounded typed fields follow (deferred surface, R56)
};
```

- **Ownership timeline (async):** producer formats into a **caller/producer-owned
  stack buffer**, then `TryEnqueue` copies `header + message bytes` into the
  reserved slot. Borrowed strings live only during formatting; nothing borrowed
  escapes the submit call (R24). The slot is producer-owned reservation→publish,
  then consumer-owned until release (R35/R36).
- **Sink view:** sinks receive a `RecordView` over the slot bytes valid only for
  `Write()`; exporters copy first (R36).
- **Serialization:** never a struct memcpy to disk/wire; versioned, length/type-
  tagged, truncation-tolerant (R26). Text output still prints session/sequence/
  thread/source so records correlate without a viewer.
- **Cleanup of dead contract:** today's `LogRecordHeader` is defined but unused
  and its comment conflates steady/wall time. The redesign either implements it
  as `RecordHeader` above (async) or deletes it (sync-only interim) — no dead
  ownership contract remains.

**Clocks (R23, F11):** capture a **monotonic** `steady_clock` tick on the
producer; record a per-session anchor `{steady_epoch, system_clock_at_epoch,
tick_frequency}` once; the backend converts to wall time for text. A wall-clock
correction must never reverse monotonic ordering (validated by test).

---

## 5. Formatter decision and evidence (R18, R19, CD1)

**Decision:** *recommend* the **narrow typed formatter** as the core direction,
**conditional** on a Phase-2 prototype comparison against a realistically erased
library-backed alternative (`{fmt}` behind the same non-template erasure
boundary). This is a `[CD]`; the public grammar is the stable surface, so the
implementation can be swapped without touching call sites.

**Why not decide purely now:** the sandbox lacks the pinned Clang 18 + a
`<format>`-capable libstdc++, so the calibrated build/runtime comparison the
review (and R52) require cannot be run here. Deciding on "reputation" is
explicitly disallowed (review §6.3). What *is* established:

| Evidence | Source | Status |
| --- | --- | --- |
| `<filesystem>` ≈ 63,690 preprocessed lines; `<chrono>` ≈ 11,554; empty TU = 8 | this spec, `decision-log.md` | **measured** (deterministic; host clang-15) |
| `log.hpp` ~2240 ms avg CI parse (budget 3200 ms); frontend ~45 s | `build_budget.json` calibration note | historical CI |
| ADR 0004 erasure: frontend 52.5→22.5 s, log.hpp ~19→~9 s | ADR 0004 | historical, not fresh |
| Zero-arg brace bug + direct-call filter bypass | `behavior_model` (reproduced) | **reproduced (logic)** |

**Design of the narrow typed formatter:**

```cpp
enum class ArgTag : uint8 { StrView, Bool, I64, U64, Ptr, F32, F64 };
struct FormatArg { ArgTag Tag; union { ... } V; };   // small POD, producer stack
// Packing is a shallow per-type conversion in log_format.hpp (cheap instantiation).
// A compact consteval scanner validates braces/count/specifiers vs arg tags (R18).
struct FormatOutcome { usize BytesWritten; bool Truncated; bool FormatError; };
FormatOutcome FormatInto(std::span<char> out, std::string_view fmt,
                         std::span<const FormatArg> args) noexcept;   // one TU
```

- Runtime parser + numeric conversion compile **once** in `format_engine.cpp`.
  `std::to_chars` is the candidate numeric backend (allocation-free, status via
  `errc`); *measure its output/support*, do not hand-roll floating point (R8).
- On failure: emit a bounded marker + usable literal prefix; **no** throw,
  allocation, abort, or recursive log. Output is UTF-8; truncation flagged;
  invalid bytes escaped/marked (R25).
- **Not** an engine-wide `std::format` replacement; grammar is closed (R19).
- **Library alternative** must meet the same no-allocation / no-terminate
  contract in the `-fno-exceptions` configuration and win *total* engineering
  cost (build + code size + maintenance) to be selected — a "compiled library"
  does not by itself prove a light call-site header.

**Legacy adapter (R53):** `log_compat_format.hpp` keeps the current
`std::format_string` + `std::make_format_args` → `vformat` path, explicitly
named as temporary, excluded from the lightweight-core budget, for un-migrated
call sites. ADR 0004 is revised to say erasure removed *execution* replication
but not `<format>` *parse* cost.

---

## 6. Delivery and flush contracts (R27, R28, R29, R30)

| Operation | Completion means | Does NOT mean |
| --- | --- | --- |
| Ordinary async log | record copied/published, or drop counted | sink visibility / disk / remote |
| Timed backend flush | selected buffers pushed to OS after a captured fence | every producer finished; power-loss safety |
| `Flush(Visible, t)` | all accepted records ≤ fence processed; selected local sinks visibility-flushed or returned failure | records still formatting at fence; GUI painted |
| Direct debug-critical | sent to selected direct endpoints before return, with result; breadcrumb retained | earlier async drained; device committed |
| `Flush(Durable, t)` | Visible + platform durable-file request succeeded for selected files | universal HW durability; remote durability |
| Controlled Fatal | emergency evidence attempted **first**; bounded healthy-backend enrichment/flush attempted after | functioning allocator/worker/unwinder |
| Normal shutdown | admission closed, accepted resolved, final fence consumed, sinks flushed/closed, worker joined | recovery of dropped records |
| Crash-handler report | minimal platform-safe capture attempted | full flush; safe normal-sink code |

**Status set (R28):** `Ok, Filtered, NotInitialized, TimedOut, SinkFailed,
Unsupported, Incomplete`. Flush returns acknowledged sequence + sink health. A
sink failure makes a fence *completed-with-error*, not successful.

**Flush fence (R29):** capture the last successfully reserved queue position
after the caller's preceding submit returns; all completed calls lie ≤ fence.
Publish the flush *request* on a **separate control channel** so a full data
queue cannot block requesting a flush. The worker waits for publication of every
reserved position ≤ fence, performs the requested flush kind, acknowledges the
fence + result. A deadline makes a missing publication observable. Coalescing is
valid only if the strongest requested kind and each caller's boundary are
preserved. A self-flush (from the worker) returns a defined result, never waits
on itself. **A timeout cannot cancel a blocked `fwrite`/`fsync`/debugger call**
(R29) — producers stay bounded only because they never do those operations.

**The breakpoint case:** `LOG_INFO(...); BREAKPOINT;` on async cannot guarantee
visibility (an all-stop debugger may suspend the worker first) — no memory
ordering fixes this. Use `LUDUS_LOG_DEBUG_SYNC` immediately before the break for
that message; issue a successful `Flush(Visible)` first to also see the earlier
stream. A Development opt-in may mirror selected categories synchronously to the
debugger for unplanned breakpoints (opt-in because debugger output is
expensive); the logger does **not** auto-switch scheduling when a debugger
attaches. Windows debugger-presence must not be cached forever (today it is
sampled once at sink construction — F10); use a cold-path refresh or direct
emission in a private OS adapter; UTF-8→UTF-16 conversion alone is not a display
guarantee.

---

## 7. Concurrency: MPSC queue (R35, R36, R37, R38, R39)

### 7.1 Invariants and ownership

- **Slot states:** `Empty → Reserved(seq) → Published(seq) → Consumed → Empty`.
  Generation counter per slot distinguishes wraps.
- **Ownership:** producer owns a slot from successful reservation to publish;
  consumer owns it from observing Published to release (marks Empty with next
  generation). No other thread writes payload in between.
- **Producer index:** `head` (atomic `uint64`, fetch-add or CAS). **Consumer
  index:** `tail` (single consumer, plain read + release store on advance).

### 7.2 Reserve → publish → consume (happens-before)

1. Producer CAS/`fetch_add` reserves `seq = head++` (bounded retries; on
   capacity exhaustion → drop with `QueueFull` reason, no advance, no hole — R38).
2. Producer copies `header+bytes` into `slots[seq % N]`.
3. Producer `store(slot.state = Published(seq), release)`. — publication edge.
4. Consumer `load(slot.state, acquire)` sees Published ⇒ all payload writes in
   step 2 are visible (release/acquire happens-before). Consumes, then
   `store(state = Empty, gen+1, release)`. — reclamation edge.
5. A producer that wraps to the same physical slot must observe `Empty` with the
   expected prior generation (acquire) **before** reserving payload; if the slot
   is still `Reserved`/`Published` by a slow predecessor, it is **not** reclaimed
   (R35) — the producer treats capacity as full and drops instead.

### 7.3 Adversarial interleavings (explicitly handled)

- **Producer paused after reservation (step 1) before publish (step 3):** the
  slot stays `Reserved`; the consumer, reaching that index, sees not-yet-
  Published and **waits/moves on per policy but never reads partial payload and
  never reclaims** the slot (R35, "not automatically wait-free"). A later wrapping
  producer sees non-`Empty` and drops rather than corrupting the paused
  producer's slot. Flush past that sequence returns `TimedOut` if the pause
  outlives the deadline — honest, not a hang.
- **Two producers reserve adjacent seqs, publish out of order:** consumption is
  in `seq` order; the consumer processes `seq` only once its slot is Published,
  so out-of-order publication delays consumption of the *earlier* seq but never
  reorders delivery (R37).
- **Wraparound at 2^64 / reduced-width model:** validated with a narrow-width
  generation test and a real 64-bit counter test (R35).
- **Lost wakeup:** publication increments an atomic `published_count` and signals
  a condition/event; the worker re-checks `published_count` against its cursor
  under the wait predicate so a signal racing the wait is not lost (R39).

### 7.4 Overflow, ordering, wakeups

Overflow policy per R38 (drop-low / reserve-headroom-for-Warning+ / Fatal &
direct on own paths / periodic loss summaries). Ordering per R37 (per-thread
order + reservation-order consumption; no global wall-time sort). Wakeups per
R39 (lost-wakeup-safe, bounded batches, low idle CPU, producers never call the
scheduler/I/O). This is **not** wait-free and the docs say so.

---

## 8. Lifecycle and worker state machines (R40, R41)

```
                Initialize(ok)              Shutdown()             final fence consumed,
 Uninitialized ───────────────▶ Accepting ───────────▶ Draining ───────────────────▶ Stopped
      ▲   ▲                        │  admit tokens         │ no new admits            │
      │   └──── Initialize(fail) ──┘  issued only here     │ wait admitted publish    │
      │            (Degraded, emergency-only)              │ /drop, then fence         │
      └───────────────────────────────────────────────────┴── join worker ───────────┘

Health (orthogonal): Healthy ⇄ Degraded   (sink failed / backlog stalled / rollover failed)
```

- **Admission (fixes F4):** a producer acquires an admission token only while
  `Accepting` (synchronized double-check or token-count, not a bare boolean).
  `Shutdown` transitions `Accepting→Draining` (closes admission), waits for
  outstanding admitted operations to publish or drop, captures the final fence,
  the worker flushes/closes/exits, then `join`. On timeout: report `Incomplete`
  and **retain** worker-referenced storage; never detach+free (R40).
- The engine should stop/join producers before logger shutdown, but the logger
  still safely **rejects** racing late submissions (returns `NotInitialized`/
  routes Warning+ to emergency).
- **Worker failure/stall:** exposed via backlog/progress; degrade/drop safely; do
  not spawn a second unsynchronized writer. No in-process recovery can make a
  SIGSEGV on the worker safe for the process — that is the crash subsystem's job.
- **Synchronous backend (R41):** same corrected semantics with an **exclusive**
  dispatch mutex (Phase-1 fix for F1). `Initialize` returns the *effective* mode;
  `SetMode` is removed. Requesting async on a sync-only build returns
  `Unsupported` (replaces the F10 note-only behavior).

---

## 9. Sinks and files (R42–R47, R6)

**Sink set (private, status-returning — CD4):** Terminal, Debugger, Session
file, Breadcrumb capture (producer-side, not a normal consumer sink); editor/
remote are deferred subscribers (R57/R58). `ILogSink::Write/Flush` return a
compact status; a shared plain-text prefix is rendered once, color is a terminal
presentation step, a future JSON serializer consumes metadata directly (never
reparses text).

**Concurrency fix (F1):** sink scratch buffers are owned by the single backend
thread (async) or protected by the exclusive dispatch mutex (sync). No sink
scratch `std::string` is mutated by two threads under a shared lock — the exact
defect reproduced (`race_model`, TSan signature matches `tsan.txt`).

**Session identity (R42, F7):** exclusive creation `O_EXCL`/`CREATE_NEW`
equivalent with UTC time + PID + nonce, retry on collision; record build ID/
revision/executable/config/platform/process+session ids/clock anchor at start.
Fresh session per run; append only when reopening a *verified* same-session
artifact under a documented recovery policy. Ownership via an open OS handle/
lock accounting for PID reuse.

**Retention (R43, F7/F9):** gather immutable successful metadata first, sort with
a strict-weak-ordering comparator (the current comparator returns `false` on read
error — not SWO — F7/F9), operate on whole inactive session groups created by
this logger, never active sessions or unrelated files, never another process's
file. Error-code `directory_iterator` throughout; audit increment for throwing
overloads under `-fno-exceptions`.

**Rotation/caps (R44, F8):** total byte cap + optional age cap + session count;
explicit zero semantics; open replacement before releasing old handle where
possible; on failure report degraded and follow cap policy (stop / bounded
fallback), not silent `mFile=nullptr`; never scan/prune on a producer thread.

**Write integrity (R45):** check short/interrupted writes, flush/close/rotation
errors; mark sink unhealthy + counters; bounded retries; partial trailing record
identifiable/quarantinable.

**Flush semantics (R46, F3):** implement the timed interval in the worker (or
remove the setting until implemented). Distinguish `fflush` (→OS, "Visible")
from `fsync`/`fdatasync`/`FlushFileBuffers` (→device, "Durable"); durable may
also need directory-metadata persistence; keep durable explicit and off the
ordinary path.

**Location (R47):** injected directory; Linux `$XDG_STATE_HOME` →
`$HOME/.local/state`; Windows per-user local data via platform adapter; explicit
tools/CI override; never silent read-only-cwd or shared system dir. UTF-8 paths
convert in the Windows adapter to native file APIs.

---

## 10. Emergency, recursion, breadcrumbs (R31–R34)

- **Base primitive (R31):** fixed literal header + bounded copied text; no maps,
  no logger state/TLS strings, no allocation, no engine locks; marks truncation.
  Lives in FoundationBase; Logging and (proposed) assertions call into it.
- **Controlled fatal (R30, F2):** emergency/breadcrumb **first**, then optional
  bounded healthy-backend enrichment/flush, then the caller's explicit break/
  termination policy. Never format+lock+all-normal-sinks before emergency (the
  current order).
- **Recursion guard (R32):** first-entry/reentry guard before formatting/
  dispatch; a nested failure emits a minimal literal/counter, never recurses,
  never grows the stack, never self-waits on the worker.
- **Signal safety (R33):** two environments (controlled vs async fault). Ordinary
  logging is **not** claimed crash-handler safe; the crash handler uses only
  audited async-signal-safe operations, preopened handles, an alternate signal
  stack, and an out-of-process reporter — a crash-subsystem responsibility.
- **Breadcrumb ring (R34):** producer-side, bounded, per-slot exclusive writer +
  publication generation; writers skip busy slots; a crash reader never waits on
  an in-write slot. **A seqlock over non-atomic payload bytes is a data race**
  under the C++ memory model — use atomic payload words + generation verification
  or an external frozen snapshot. Process-local: survives termination **only** if
  a dump/external capture preserves it (documented, not hidden). Size/coverage
  `[MD]` (CD3/CD6).

---

## 11. Failure matrix (R28, R38, R40, R45, F2/F7/F8)

| Failure | Target response |
| --- | --- |
| Queue full / publication attempts exhausted | drop per severity policy; Warning+ reserve/breadcrumb; counters + later summary |
| Worker stalled/unavailable | producers bounded; expose backlog; direct path independent; flush → TimedOut/Degraded |
| Alloc fails at init | return degraded status; fixed emergency path; do not publish a partial backend |
| Formatting invalid/too long | bounded fallback + Truncated/FormatError flags; no throw/abort; unsupported type is a compile error |
| Disk full / broken pipe / rotation/flush failure | mark only that sink unhealthy; one bounded note; other sinks continue |
| Logger recursively logs | guard before format/dispatch; minimal literal or counted suppression; no self-wait |
| Shutdown races submission | admission decides accept/reject; accepted → final fence; late Warning+ → emergency |
| Controlled fatal | emergency/breadcrumb first; optional bounded normal flush; then caller's policy |
| Crash while logger slot/lock held | crash path never acquires it; incomplete slot skipped in external snapshot; no repair of corrupt state |
| Abrupt uncatchable termination | only OS-visible file bytes + externally captured artifacts expected; loss documented |

---

## 12. Assertion / emergency integration boundary (R5, R30)

- Shared low-level reporter in FoundationBase; **no upward dependency** on
  Logging. Assertions (proposed `docs/architecture/assertions.md`, **absent** —
  request it) reuse it; they must not require the logging worker.
- `LUDUS_LOG_FATAL` reports; it does not terminate. A separate non-returning
  failure primitive (owned by assertions/caller) owns termination. If assertions
  land later, the only coupling is the shared Base reporter + optional breadcrumb
  attachment — specified here as a contract, not implemented.

---

## 13. Runtime and build budgets (R7, R52)

- **Build (R7):** do not raise any `build_budget.json` value. Target: `log.hpp`
  materially cheaper after dropping `<format>`+`<filesystem>` from the call-site
  path; lower its 3200 ms override once measured on the calibrated CI runner; add
  a gate for a representative instantiated logging TU (0/100/1,000/10,000 sites,
  repeated vs diverse packs) reporting parse / constant-eval / instantiation /
  optimization / `.text` / `.rodata` / debug-info separately. PCH/modules must
  not excuse a heavy SDK header; test standalone SDK consumers.
- **Runtime (R52, all `[MD]` where numeric):**

| Benchmark | Hard gate (numeric-independent) |
| --- | --- |
| Compiled-out call | no arg/category eval, no instantiation, no residual call-site work (assembly/symbol inspection) |
| Runtime-disabled call | no alloc/lock/map/timestamp/TLS-registration/arg-eval; scalar-only |
| Enabled producer | no common-path heap alloc, no sink I/O on producer; bounded format/publish; Truncated/FormatError honest |
| Saturation | bounded memory + producer work; specified loss counters; no deadlock/overwrite |
| Flush latency | fence covers all accepted ≤ fence; timeout/failure honest; no fence starvation |
| Wakeup/idle | no lost wakeups; low idle CPU |

Numeric p99 per-call limits derive from `logging CPU budget ÷ expected critical-
thread calls` on reference hardware (`[MD]`); the hard gates above do not depend
on that number.

---

## 14. Standard-library ledger (R8) — individually justified

| Facility | Where | Justification | Future |
| --- | --- | --- | --- |
| `<string_view>` | public + internal | non-owning boundary; ADR 0003 allowed | keep |
| `<source_location>` | `log.hpp` | call-site metadata; ADR 0003 allowed | keep |
| `<atomic>` | Gate, queue, ring | lock-free predicate + MPSC + ring | keep |
| `<mutex>/<condition_variable>` | control/flush cold path, sync backend | admission/flush bookkeeping; cold | keep |
| `<chrono>` (`steady_clock`) | **`.cpp` only** | monotonic capture; NOT in public header | keep (private) |
| `<charconv>` (`to_chars`) | `format_engine.cpp` | alloc-free numeric conv w/ status | measure; keep if adequate |
| `<cstdio>` | sinks + Base emergency | ADR 0003 allows here only | keep (scoped) |
| `<filesystem>` | **`session.cpp` only** | file ops; NOT in public header (fixes F5) | platform VFS later |
| `std::string`/`std::vector`/`std::unordered_map` | internal only, bounded roles | ADR 0003 "slated"; used narrowly | Ludus-owned later |
| `<format>` | **`log_compat_format.hpp` adapter only** | temporary migration; excluded from core budget (R53) | remove after migration |

No new general allocator/string/container is introduced (R8). `noexcept` is
applied only where the operation truly cannot fail or converts failure to status
(R3) — notably `ILogSink::Write/Flush` return status instead of relying on
`noexcept` to mask allocation, unlike today.

---

## 15. Explicitly unresolved decisions (need evidence or an engine-level call)

1. **Assertion coordination** — `assertions.md`/`assertions-rabin-review.md` are
   absent. The Base reporter boundary is designed to be reusable, but the exact
   assertion API/termination ownership is unresolved until those docs are
   supplied (R30).
2. **Formatter selection** — narrow typed vs library-backed erased, decided by
   the Phase-2 measured comparison on the pinned toolchain (CD1, R19, R38-format).
3. **Queue/ring/message sizing and per-call ns budget** — from the workload
   inventory (rates, size distribution, p99 budget) on reference hardware
   (`[MD]`, R25/R33/R35/R52).
4. **Delivery policy** — which sites need synchronous visibility; acceptable
   ordinary tail loss; which workflows need durable storage (R54).
5. **Lifetime model** — will modules/plugins unload or embedded engines re-init
   repeatedly while host threads log? Determines descriptor interning + shutdown
   obligations (R40).
6. **Crash ownership** — which component owns termination/native-context capture/
   external reporter/build-symbol retention (deferred crash subsystem, R59).
7. **Platform scope** — near-term devkit/editor consumer? Which Linux/Windows
   debugger + file-path guarantees to support/test first (R47/R58).

These are bounded; none blocks fixing the proven race, brace/filter bugs, fatal
ordering, unused flush interval, or heavy includes.
