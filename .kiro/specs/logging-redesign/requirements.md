# FoundationLogging redesign — Requirements

**Spec status:** proposed, not yet implemented. This document turns
`docs/architecture/logging-review.md` (findings F1–F12, sections 6–14) into
testable requirements for Ludus's C++23 logging infrastructure. It is
engineering input, revalidated against the checked-out revision; it is not an
authorization to rewrite the engine. Implementation is authorized separately
(the review's phases 0–4 are the core program).

**Revalidation basis.** The review inspected revision `269b6118`. The current
checkout is `068618c`; the only change since the review baseline is the docs
commit that added the review itself, so `modules/foundation/logging` is
byte-identical to the reviewed source and every source-level finding still
applies. See the decision log (`decision-log.md`) for per-finding
classification and reproduction evidence.

**Missing coordination inputs (requested, not assumed).**
- `docs/architecture/assertions.md` and
  `docs/architecture/assertions-rabin-review.md` are **absent** from the
  checkout. They are treated as *proposed* designs. No requirement below assumes
  an assertion API exists; requirements that must coordinate with assertions
  state the boundary contract only (see R30). **Please supply these documents**
  if they should constrain the fatal/emergency boundary.
- GPG5 (Duquette) remains unavailable, as noted in the review. No requirement
  depends on its contents. Remote-transport requirements stand on engine needs
  only and are out of core scope (R42).

---

## 1. Conventions

- **Requirement IDs** are `R<n>`. Each has a verification method
  (`Test` / `Inspection` / `Measurement`).
- **MUST / SHOULD / MAY** are RFC-2119 strength.
- "Engine code" = `modules/` and `apps/`. Tests may re-enable exceptions via
  `ludus_enable_test_exceptions()` only.
- **Conservative default** markers `[CD]` flag a reversible choice adopted so
  work can proceed; each is listed in §8 and in `decision-log.md`.
- **Measurement-dependent** markers `[MD]` flag a value or choice that must be
  set from calibrated measurement, not asserted now.

---

## 2. Repository-rule requirements (non-negotiable constraints)

These bind every requirement below; they are called out because the redesign
must not "solve" logging by violating them.

- **R1** — All engine logging code MUST compile under C++23 with the pinned
  toolchain (Clang/LLVM 18 + lld; `config/tool_versions.json`), warning-clean
  under the project warning set, with warnings-as-errors in CI. *Inspection +
  Measurement (build).*
- **R2** — No engine logging code may use C++ exceptions (`throw`/`try`/`catch`)
  or depend on an API that requires exceptions on the normal path. It builds
  with `-fno-exceptions`. *Inspection.*
- **R3** — `noexcept` MUST NOT be used to disguise an allocating or otherwise
  failing operation that would terminate the process on failure. A `noexcept`
  boundary either cannot fail, or converts failure to a status/flag/counter
  before returning. *Inspection.*
- **R4** — Fixed-width Ludus aliases (`uint32`, `usize`, `float32`, …) MUST be
  used instead of `std::` primitive spellings in new code. *Inspection
  (clang-tidy).*
- **R5** — FoundationLogging MUST NOT acquire a dependency on any higher module
  (platform/windowing). FoundationBase MUST NOT depend on Logging. Any shared
  emergency/diagnostic primitive lives in FoundationBase or a module strictly
  below Logging. *Inspection (CMake + include graph).*
- **R6** — Private implementation headers (sinks, records, formatter internals)
  MUST NOT be installed in the SDK or reachable from public headers.
  *Inspection (installed file set + SDK-consumer build).*
- **R7** — Build budgets in `config/build_budget.json` MUST NOT be raised to
  accommodate this redesign, and engine exceptions MUST NOT be enabled to make
  anything pass. After the frontend split, representative instantiated logging
  TUs (not only include-only headers) SHOULD be gated, and the `log.hpp` budget
  SHOULD be *lowered* once measured on the calibrated runner. *Measurement.*
- **R8** — No new general-purpose allocator, string, or container library may be
  invented for logging. Retained standard-library facilities MUST each be
  justified individually (see `design.md` §"standard-library ledger"). *Inspection.*

---

## 3. Frontend, taxonomy, and filtering

### 3.1 Severities and categories (preserve)

- **R9** — Exactly the six existing severities (`Trace, Debug, Info, Warning,
  Error, Fatal`) MUST be preserved with their current names, numeric ordering,
  and `Trace < … < Fatal` comparison semantics. No seventh severity and no
  parallel mandatory "channel"/"subsystem" concept may be added. *Test +
  Inspection.*
- **R10** — The category model (compile-time-hashable descriptor with a stable
  id and borrowed name) MUST be preserved. `LOG_CORE` and `LOG_TEMP` remain.
  Delivery policy (async/direct/emergency) MUST remain **orthogonal** to
  severity and to category. *Inspection.*
- **R11** — Existing macro spellings (`LUDUS_LOG_TRACE/DEBUG/INFO/WARN/ERROR/
  FATAL`) MUST be preserved as the migration facade where their semantics remain
  sound. New surfaces (raw-text, typed-format, direct-sync) are *added*, not
  substituted, and use clearly distinct spellings. *Inspection.*

### 3.2 Compile-time and runtime gating (correctness)

- **R12** — A compiled-out severity (below `LUDUS_COMPILED_LOG_LEVEL`) MUST
  expand so that: message arguments are not evaluated, the category expression
  is not evaluated, and no formatting template is instantiated and no call-site
  code for that statement remains in optimized output. This already holds for
  argument evaluation (`category_tests.cpp`); the requirement extends it to
  category evaluation and to *instantiation/code-size* absence. *Test
  (side-effect) + Inspection (assembly/symbols) + Measurement (code size).*
- **R13** — A runtime-disabled call (compiled in but filtered out at runtime)
  MUST NOT: allocate, lock, search a hash map, format, capture a timestamp,
  register/allocate a thread name, or evaluate its message arguments. It costs
  at most a small bounded set of scalar loads/branches. *Test + Measurement.*
  - This is a **behavior change**: today `ShouldLog` for an initialized
    non-Fatal call takes a `shared_lock` and an `unordered_map` lookup (F10/F11).
    The target replaces that with lock-free scalar reads of a category
    descriptor override + global threshold.
- **R14** — Fatal MUST remain never-compiled-out and always runtime-eligible so
  it can always reach reporting. *Test.*
- **R15** — The category expression in an accepted, compiled-in call MUST be
  evaluated **exactly once** (today it is evaluated twice: once in `ShouldLog`,
  once in the dispatch call — F6). Tests MUST cover exact-once evaluation for
  both enabled and runtime-disabled calls (not only compiled-disabled).
  *Test.*

### 3.3 Raw text vs typed formatting (correctness)

- **R16** — There MUST be a **raw-text** submission surface that treats its
  argument as literal text and never interprets it as a format string. Dynamic
  or third-party strings (driver messages, `errno` text) use this surface and
  cannot trigger format parsing. *Test.*
- **R17** — There MUST be a **typed-format** surface whose escaped-brace handling
  is consistent for all argument counts, **including zero arguments**. The
  current zero-argument fast path that forwards the literal verbatim
  (so `"escaped {{braces}}"` stays doubled — F6, reproduced) MUST be fixed:
  either route zero-argument format calls through the same brace-processing
  path, or require raw text for literals with no substitutions. *Test (the
  exact `{{braces}}` case).*
- **R18** — Unsupported argument types MUST be rejected at **compile time** by
  the typed-format surface. The supported grammar (R19) is closed; feeding an
  unsupported type is a compile error, not a runtime fallback. *Test (compile-
  fail fixtures).*
- **R19** — The typed-format grammar is intentionally narrow: string/`string_view`,
  `bool`, Ludus fixed-width integers, pointer-as-address, `float32`/`float64`;
  `{}` plus a documented small set of decimal/hex/precision specifiers and brace
  escaping. No locale, no chrono formatting, no arbitrary ranges, no recursive
  formatting, no implicit custom-object callbacks. Width/precision and argument
  count have hard caps. *Test + Inspection.* Whether this grammar covers the
  real call-site inventory is **[MD]** (R38).
- **R20** — The public callable frontend MUST re-check eligibility before
  formatting; a public function that dispatches an *un-filtered* record MUST NOT
  exist as ordinary API. The current public `Log(...)` that bypasses filtering
  (F6, reproduced) MUST be removed or made admission-checked. The backend's
  internal submit accepts an already-admitted record only. *Test + Inspection.*

### 3.4 Lightweight common header (build cost)

- **R21** — The common call-site header MUST NOT transitively expose
  `<format>`, `<filesystem>`, `<chrono>`, or `<iostream>` machinery. Today
  `log.hpp` includes `<format>` and `config.hpp` (which includes `<filesystem>`)
  — F5, confirmed; `<filesystem>` alone expands to ~64k preprocessed lines on
  the measurement host. The redesign separates:
  - a **raw-text + gating** header (severity, category, macros, source
    metadata) with no heavy includes;
  - an opt-in **typed-format** header (argument packing + a restricted literal
    checker) that depends on the raw header, never the reverse;
  - a **lifecycle/configuration** header (init, flush, health, shutdown) whose
    directory input is a borrowed UTF-8 path or a narrow platform adapter — no
    public `<filesystem>` requirement.
  *Inspection (preprocessed-include check) + Measurement (build).*
- **R22** — Category **declaration** (in a header) MUST be separable from
  category **definition** (one TU, constant-initialized), so declaring a
  category does not pull heavy machinery. *Inspection.*

---

## 4. Records, metadata, and clocks

- **R23** — Every record MUST carry structured envelope metadata: monotonic
  timestamp (with a session anchor for wall-clock conversion), severity,
  category identity, native thread id (plus optional engine thread id), source
  file/function/line, and a sequence number. The envelope MUST survive past the
  sink boundary (sinks receive structure, not only a flattened line). *Test +
  Inspection.*
  - **Behavior changes vs today:** timestamps move from wall-clock
    `system_clock` to a **monotonic** domain with a wall anchor (F11); thread id
    gains the **native** OS id alongside the readable counter (F11); a
    **sequence** number is added; the currently-unused `LogRecordHeader` is
    either implemented for the queue or removed (no dead ownership contract).
- **R24** — An owned record used for asynchronous delivery MUST copy its message
  bytes and all borrowed strings into bounded record storage before the
  producer's submit call returns. No `std::string` per queued record and no
  borrowed `string_view` may outlive the producer call. Synchronous delivery MAY
  keep borrowing for the duration of `Write()` only. *Test (producer-lifetime:
  temporary strings, thread exit, rename).*
- **R25** — Message payloads MUST be bounded with an explicit maximum size and
  explicit truncation behavior (a truncation flag; avoid splitting a UTF-8 code
  point where feasible; never silently drop the newline/framing). *Test (bounds,
  exactly-full, overfull, control/NUL/newline bytes).* The maximum size value is
  **[MD]** (queue-slot sizing, R33).
- **R26** — On-wire/on-disk serialization (when added) MUST NOT be a verbatim
  memory dump of a C++ struct: it MUST have a version, explicit lengths/types,
  bounded sizes, and a parser tolerant of a truncated final record. Text output
  MUST still carry enough identifiers to correlate records. *Test (decoder
  round-trip/truncation).* JSON/binary serialization is deferred (R41).

---

## 5. Delivery paths and flush contracts

- **R27** — Three delivery paths MUST be distinct and independently testable:
  1. **Ordinary** diagnostics: cheap gating, bounded producer formatting, owned
     record, bounded MPSC queue, one sink-owning backend thread.
  2. **Direct debugger-critical** delivery: an explicit synchronous operation
     that delivers to selected debugger/stderr endpoints before returning,
     independent of the backend worker, and returns a result. This is a
     *delivery policy*, not a new severity (e.g. a Debug-severity direct call).
  3. **Fatal/emergency** reporting: an independent primitive below Logging,
     invoked first, with normal logging only a best-effort secondary
     destination.
  *Test (each path in isolation, incl. worker parked for path 2).*
- **R28** — Delivery/flush operations MUST return explicit status drawn from a
  closed set: `Ok, Filtered, NotInitialized, TimedOut, SinkFailed, Unsupported,
  Incomplete` (as applicable). A method returning MUST NOT be interpreted as
  success. A flush returns the acknowledged sequence and selected sink health.
  *Test.* Precise completion semantics per operation are specified in
  `design.md` §"delivery & flush contracts" and MUST match this table:
  - ordinary async log "completes" = record copied/published **or** its drop
    counted; NOT sink visibility/persistence.
  - `Flush(Visible, timeout)` = all accepted records through a captured fence
    processed and selected local sinks visibility-flushed or returned failure.
  - `Flush(Durable, timeout)` = visibility **plus** a platform durable-file
    request (`fsync`/`fdatasync`/`FlushFileBuffers`) for selected files; NOT a
    guarantee against hardware failure.
  - direct debug-critical = sent to selected direct endpoints before return,
    with result; does NOT drain earlier async messages.
- **R29** — A flush timeout MUST be honest: it makes a *missing publication*
  observable; it MUST NOT claim to cancel an in-progress blocking OS write
  (`fwrite`/`fsync`/debugger API). Ordinary producers stay bounded because they
  never perform those operations. A flush requested from the backend thread or
  recursively MUST return a defined result, not wait on itself. *Test (fake
  clock; stalled publication; self-flush).*

- **R30** — **Fatal reporting MUST NOT silently become process termination.**
  `LUDUS_LOG_FATAL` remains a *reporting* operation. Termination is owned by an
  explicit non-returning failure primitive / the caller's policy. Coordination
  with the (currently absent, proposed) assertion subsystem is a **boundary
  contract only**: the shared low-level emergency reporter lives in
  FoundationBase and MUST NOT gain an upward dependency on Logging; assertions,
  if/when implemented, reuse it. *Inspection + Test.*
  - **Behavior change:** today initialized Fatal formats, locks, writes all
    normal sinks, *then* calls emergency (F2). The target reports via the
    independent emergency path **first**, then attempts bounded healthy-backend
    enrichment.

---

## 6. Emergency-first reporting, recursion, breadcrumbs

- **R31** — A bounded, allocation-free emergency reporting primitive MUST live
  in FoundationBase (or below Logging). It writes a fixed literal header plus
  bounded copied text; it MUST NOT consult category maps, instantiate logger
  state/TLS strings, allocate, walk the normal sink list, or acquire engine
  locks. It marks truncation rather than silently dropping content (today
  truncation is silent — F8-adjacent). *Test + Inspection.*
  - **Behavior change:** the emergency logger currently lives *inside* the
    logging module; it MUST move to a lower boundary so assertions and Base can
    share it without depending on Logging (R5).
- **R32** — A first-entry/reentry guard MUST prevent recursive enrichment: a
  failure while reporting emits at most a minimal literal or a counter and MUST
  NOT recurse or grow the stack. Ordinary producer recursion depth is initialized
  before invoking any formatter/adapter. The crash path uses a separately
  audited reentry mechanism, not casual reuse of arbitrary C++ TLS. *Test
  (recursive formatter/sink failure; no deadlock/stack growth).*
- **R33** — The emergency path MUST NOT be claimed signal-safe. Two environments
  are distinguished and documented:
  1. **controlled diagnostic failure** (process still runs): bounded conversion,
     recursion guard, direct output, optional timed backend request.
  2. **asynchronous fault/crash handler**: only audited async-signal-safe
     platform operations; no C stdio, no ordinary formatter, no container
     access, no worker join. This is a crash-subsystem responsibility, not a
     claim made about ordinary logging. *Inspection + platform audit (may be
     unexecutable here — mark so).*
- **R34** — A bounded **producer-side breadcrumb ring** MUST be specified:
  fixed memory, per-slot exclusive writer ownership + publication generation,
  writers skip busy slots without waiting, and a crash reader never waits on a
  slot being written. A `seqlock` counter over non-atomic payload bytes is **not**
  a legal concurrent read under the C++ memory model; use atomic payload words
  with generation verification, a proven reader-ownership protocol, or an
  external frozen-snapshot read. The ring is process-local and survives
  termination **only** if a dump/external capture preserves it — this limit MUST
  be documented, not papered over. Ring size, record size, and captured-severity
  coverage are **[MD]** (review's 2,048×256 B are prototype values). *Test
  (interrupted write; concurrent read model) + Measurement (atomic-copy cost).*

---

## 7. Bounded asynchronous backend, ownership, lifecycle

- **R35** — A bounded **MPSC** backend MUST be specified with: fixed slots and
  capacity; per-slot generation/publication state; nonblocking `TryEnqueue`
  (failed attempts do not advance the position or create holes); release/acquire
  publication; no slot reuse before consumer release; bounded CAS retries with
  contention exhaustion reported as a drop reason (not infinite spin). The design
  MUST include happens-before reasoning and adversarial interleavings, and MUST
  explicitly handle **a producer preempted after reservation** without unsafe
  slot reclamation. Wraparound MUST be validated with reduced-width/model tests
  and real 64-bit counters. It MUST NOT be claimed wait-free. *Test (TSan;
  wrap/full/stalled-producer; deterministic barriers).* Slot size/count are
  **[MD]** (R33/R25).
- **R36** — Exactly one backend thread MUST own the ordinary sinks, their
  buffers, rotation, and serialization. Producers never touch sink state. A slot
  is owned by its producer between reservation and publication, then exclusively
  by the consumer until release. Sinks receive views valid only during `Write()`;
  optional exporters copy before returning. *Test (TSan) + Inspection.*
- **R37** — Per-thread order MUST be guaranteed for one thread's successful
  submissions, and backend consumption MUST be in reservation order. The stream
  MUST NOT be re-sorted by wall time or imply a causal total order; correlation
  via sequence/session/optional job id. Different endpoints (stdout/stderr/
  emergency/export) MAY interleave differently; sequence/session ids make that
  honest. *Test.*
- **R38** — Overflow policy MUST be explicit and bounded:
  - ordinary Trace/Debug/Info: drop without waiting; count by reason + severity;
    never overwrite an owned slot.
  - Warning/Error: may use reserved headroom; on exhaustion preserve a compact
    breadcrumb and attempt a rate-limited **nonblocking** emergency indication;
    always account loss; never turn an error storm into unbounded producer
    blocking.
  - Fatal and direct-sync use their own paths and do not depend on a free slot.
  - the backend emits periodic loss **summaries** (interval + counter delta) when
    it has capacity; it MUST NOT log one warning per dropped record.
  No finite design promises lossless, bounded-memory, nonblocking logging against
  a permanently blocked consumer; the tradeoff is visible in API + tooling.
  *Test (saturation; drop accounting; no torn entries).*
- **R39** — Wakeups MUST be lost-wakeup-safe (event/condition with a timed flush
  deadline); the worker drains bounded batches to avoid starving flush/control
  work; idle CPU stays low. Normal producers do not invoke the job scheduler or
  wait for I/O. *Test (empty→nonempty races; sparse vs sustained; timed flush
  with no new records).*
- **R40** — Lifecycle MUST use explicit states
  `Uninitialized → Accepting → Draining → Stopped`, with a `Degraded` health
  flag independent of lifetime. A producer obtains an **admission token** only
  while `Accepting`; shutdown closes admission and waits for admitted operations
  to publish/drop before capturing a final fence. An initialization boolean alone
  is insufficient — a synchronized admission (double-check or equivalent) is
  required. This fixes F4 (today a producer can pass the init check, pause, then
  take its shared lock after shutdown cleared the sinks, losing the record;
  reinit can move an in-flight record into a different session). On shutdown
  timeout, report incomplete shutdown and **retain** the worker's referenced
  storage; MUST NOT detach the thread and free its queues/sinks. *Test (shutdown
  races; reservation-interruption; reinit).*
- **R41** — A **corrected synchronous** backend MUST remain available for tools
  and tests, with the same corrected semantics (serialized sink access, honest
  status). Async is selected at initialization and the *effective* mode is
  returned; live `SetMode` is removed until a real user needs it. The current
  `SetMode(Asynchronous)` that only emits a note while staying synchronous, and
  initialization with `Asynchronous` that does not even note (F10), MUST be
  replaced by either honoring async or rejecting it with `Unsupported`. *Test.*

---

## 8. Files, sessions, persistence

- **R42** — Session identity MUST be created by **exclusive creation** (UTC time
  + PID + nonce/counter with collision retries), not by a name that merely looks
  unique. This fixes the current `fopen("wb")` that truncates a same-second/
  same-PID file on rapid reinit (F7, confirmed). *Test (rapid reinit; simulated
  collision).*
- **R43** — Retention MUST operate on whole **inactive session groups** created
  by *this* logger, never on an active session and never on unrelated files. It
  MUST NOT count rotation segments as sessions, MUST NOT delete another running
  process's file, and MUST use an open OS ownership handle/lock (accounting for
  PID reuse). Timestamps for ordering MUST be gathered **before** sorting (not
  inside the comparator), and the comparator MUST satisfy strict weak ordering
  — the current comparator returns `false` on read error, violating SWO with
  mixed entries (F7/F9, confirmed). Error-code iteration MUST be used throughout;
  `directory_iterator` increment paths MUST be audited for throwing overloads
  under `-fno-exceptions` (F9). *Test (unrelated files; active session; segment
  groups; unreadable entries).*
- **R44** — Storage MUST be bounded by a configurable **total byte cap** and
  optional **age cap** in addition to session count; zero-values semantics MUST
  be explicit (no implicit "0 = unlimited"). A long-running process MUST be
  bounded by a segment/byte cap, not startup pruning alone. Rotation opens the
  replacement segment successfully before relinquishing the old handle where the
  platform allows; on rollover failure it reports degraded storage and follows
  the cap policy (stop or bounded fallback) rather than silently disabling file
  output (today rotation failure sets `mFile = nullptr` silently — F8). Directory
  scanning/pruning MUST NOT happen on a producer/render submission. *Test
  (rollover failure; caps; long-run byte bound).*
- **R45** — Write/flush/close/rotation errors MUST be checked: short writes,
  interrupted writes, flush/close failures. A failed sink is marked unhealthy,
  retained counters track it, retries are bounded where permitted, and a record
  written partially during a crash is identifiable as incomplete. Single-writer
  ownership prevents ordinary interleaving but does NOT guarantee filesystem-
  atomic writes or survival of a partial last record; readers ignore/quarantine
  an incomplete trailing line. *Test (open/short-write/flush/close/rotation/
  permission/disk-full injection).*
- **R46** — The timed flush interval MUST be **implemented** or the setting
  removed until it is. Today `FlushIntervalMilliseconds` is stored but never used
  (F3, confirmed; the abrupt-exit probe observed a zero-byte file). The
  distinction between **visible** (`fflush` → OS) and **durable** (`fsync` → the
  storage device) MUST be explicit; `fflush` alone is not durability. *Test
  (fake-clock timed flush; abrupt-exit subprocess).*
- **R47** — File output MUST remain useful with no viewer/server: readable text
  by default, fresh session per run, source/context identifiers preserved.
  Directory is injected by the app/platform bootstrap (Logging never depends
  upward). Platform-specific writable-location resolution (XDG state on Linux;
  per-user local data on Windows via the platform adapter; explicit override for
  tools/CI, never a silent read-only-cwd or shared system dir fallback) is
  specified in `design.md`. UTF-8 paths convert in the Windows adapter to the
  native file API; `path.string()` + narrow `fopen` is not a portable Unicode
  contract. *Test + Inspection.*

---

## 9. Statistics and observability

- **R48** — Counters MUST have defined, distinct meanings for attempt / admit /
  drop / deliver / fail, and a failed delivery MUST NOT inflate a success
  counter. Today `Written` increments unconditionally after the sink loop even
  with zero sinks or a short/failed write (F8, confirmed), and `Dropped` never
  changes. Counters MUST distinguish "no activity" from "loss" (loss summaries
  carry interval + delta). *Test.*
- **R49** — Sink health MUST be observable (per-sink status), and backend
  progress/backlog MUST be exposable so a stalled worker is detectable (with the
  caveat that a debugger pause or descheduling is not proof the thread died).
  *Test.*

---

## 10. Validation requirements

- **R50** — Phase-0 regression tests MUST reproduce, against the real engine on
  the pinned toolchain: the concurrent-producer race (TSan), the zero-argument
  brace bug, the direct-call filter bypass, the abrupt-exit lost-tail behavior,
  and the Error-visibility behavior. The behavioral bugs and a structural model
  of the race have already been reproduced here at the logic level (see
  `decision-log.md`); the **pinned-toolchain, real-engine** reproduction is a
  Phase-0 deliverable because this sandbox lacks Clang 18 + `<format>`. *Test.*
- **R51** — Tests MUST include concurrent, subprocess-based termination/fault,
  and deterministic-barrier race-window tests — not only single-threaded,
  flush-before-read tests (today's suite is exactly that — F12). ASan/UBSan
  success MUST NOT be read as race-freedom; TSan is a separate configuration.
  *Test.*
- **R52** — Measurement MUST record revision, OS, CPU/cores, power policy,
  compiler/STL/linker versions, flags, LTO/PCH/cache, sink destinations, and
  message distributions; report median/p95/p99/max; and prevent the optimizer
  from deleting timed work. "Messages/second" alone is insufficient. The review's
  noisy include timings are **not** calibrated gates. Numeric per-call latency
  limits are **[MD]** and derived from call volume ÷ budget on reference
  hardware; hard correctness/allocation/memory-bound gates do not depend on that
  number. *Measurement.*

---

## 11. Compatibility policy

- **R53** — Existing severity macros stay as the migration facade. Existing
  `std::format`-style behavior moves into an **explicitly named temporary
  adapter**, excluded from the new lightweight core and *not* presented as
  compliant with the final public-header rule; ADR 0004 is revised to distinguish
  *erasure of execution* from *elimination of parse cost*. New call sites adopt
  the bounded grammar after a format-string/argument-type audit. *Inspection.*
- **R54** — Existing Error-flush visibility MUST NOT be silently weakened. Call
  sites relying on it are identified; they either move to explicit critical
  delivery or keep a documented temporary Error-flush compatibility policy until
  migrated. *Inspection + Test.*
- **R55** — Async is enabled in Development first (collect queue/latency/loss
  data), then Profile; Release severity removal stays configurable. The final
  meaning of an ordinary call, once migration completes, MUST be documented.
  *Inspection.*

---

## 12. Scope and deferred capabilities

**In core scope (review phases 0–4 + brought-forward file/session fixes):**
synchronous correctness repair; lightweight frontend split; bounded typed
formatting + owned records; six-severity/category preservation; three delivery
paths; emergency-first + recursion + bounded breadcrumbs; bounded MPSC backend +
lifecycle + flush fences; structured envelope + minimal session metadata; file
correctness/retention/caps.

**Deferred (separately scoped follow-ups; MUST NOT block core repair):**
- **R56** — Optional bounded typed fields and JSON-lines serialization
  (opt-in header; never in the producer/common header). *Deferred.*
- **R57** — In-editor console subscriber (bounded history; no engine-thread
  blocking on UI scroll). *Deferred.*
- **R58** — Remote export/transport (separate module, own bounded queue + sender
  + reconnect/backoff + spool; local-only default; auth for remote; never in
  FoundationBase or the common header; no unauthenticated listener; no combined
  log-viewing + command execution). *Deferred.*
- **R59** — Crash-reporter integration, native stack capture, offline
  symbolization, build-symbol retention. *Deferred to crash subsystem.*
- **R60** — Per-thread SPSC queues or deferred-object formatting — only if Phase-4
  MPSC measurements miss the producer budget. *Deferred, measurement-gated.*

**Explicit non-goals (MUST NOT be implemented, only bounded by integration
contracts in `design.md`):** remote transport/databases/web servers/dedicated
log viewer as prerequisites; HTML as the persistent record model; profiler/
telemetry/spatial-visualization frameworks; automatic stack walking/symbolization
on every error; general plugin/hot-reload machinery solely for logging;
public template-policy matrix or stream (`operator<<`) logger; a global
synthetic call stack.

---

## 13. Conservative defaults adopted `[CD]` (all reversible)

- **CD1** — Recommended core formatter direction is the **narrow typed
  formatter** (R19), *conditional on* the Phase-2 comparison against a
  library-backed erased alternative (R38-format / `design.md`). If the library
  option wins the measured no-alloc/no-terminate + build contract, adopt it
  behind the same erasure boundary. Reversible: the public grammar is the stable
  surface, not the implementation.
- **CD2** — Default overflow policy = drop-low-severity + reserved-headroom for
  Warning/Error (R38). Reversible via config.
- **CD3** — Keep the review's prototype starting values (1,024 B slot, 4,096
  slots; 1 s flush interval; 2,048×256 B breadcrumbs) **only as starting points**
  for measurement, explicitly not as accepted budgets (R33/R25/R46).
- **CD4** — Keep the private virtual sink interface (return compact status);
  do not template the sink graph.
- **CD5** — Text-only file output first; JSON-lines/fields deferred (R56).
- **CD6** — Emergency reporter default coverage = Warning+ and configured
  categories for breadcrumbs; broaden only after measuring producer cost (R34).
