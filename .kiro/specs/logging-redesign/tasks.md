# FoundationLogging redesign — Tasks

Dependency-ordered implementation batches for the core program (review phases
0–4 + brought-forward file/session fixes). Each batch lists requirement IDs
(`requirements.md`), affected areas, regression tests, acceptance gates, and
migration steps. This document is a **plan**; nothing here is executed yet.
Implementation is authorized separately (handoff Prompt 2).

**Global gate for every batch** (from AGENTS.md; pinned Clang 18 + lld):
warning-clean build, `./scripts/test <preset>`, `./scripts/check --all`
(format+tidy), `./scripts/build linux-clang-asan-ubsan` + tests clean. Concurrency
batches add a separate **TSan** configuration. Public-header changes add SDK
install/consumer validation (`./scripts/install-sdk`). No engine exceptions; no
budget raises (R1, R2, R7).

---

## Phase 0 — Evidence and contracts (blocks nothing; enables everything)

### T0.1 Promote defects to real regression tests on the pinned toolchain
- **Reqs:** R50, R51. **Findings:** F1, F3, F6, F8.
- **Areas:** `modules/foundation/logging/tests/` (+ a concurrency test target).
- **Do:** add tests that FAIL on today's code — TSan concurrent-producer race
  (F1), zero-arg `"escaped {{braces}}"` (F6), direct-`Log()` filter bypass (F6),
  abrupt-exit lost-tail (F3), Error-visibility (baseline). Verify actual compiler
  flags first: the review notes test `-fexceptions` may precede `-fno-exceptions`
  in the compile DB — confirm the test-exception override order before relying on
  exception-based test behavior; never enable exceptions in engine targets.
- **Regression tests:** the five reproducers above; a TSan target wired into CI.
- **Gate:** each reproducer fails as expected on baseline; harness separates
  gating / formatting / queue / I/O timing; baseline build + code-size retained.
- **Note:** logic-level repros already exist (`decision-log.md`); this batch is
  the *real-engine, pinned-toolchain* version (sandbox lacks Clang 18/`<format>`).

### T0.2 Measurement harness + workload inventory
- **Reqs:** R52. **Findings:** F5, F12.
- **Do:** inventory message sizes/rates/categories/argument types across call
  sites; record compiler/STL/host/load; agree visible/durable/emergency
  definitions; capture baseline build + `.text`/`.rodata`/debug-info sizes.
- **Gate:** harness prevents optimizer elision, reports median/p95/p99/max,
  documents that include timings are diagnostic not gating.

---

## Phase 1 — Repair the synchronous core (justified by reproduced defects)

### T1.1 Serialize sink dispatch/flush (fix the race)
- **Reqs:** R27(sync), R36, R41. **Findings:** F1.
- **Areas:** `logger.cpp` dispatch, `sink.hpp`, all sinks.
- **Do:** replace the shared-lock dispatch with an **exclusive** dispatch/flush
  mutex so no sink scratch is mutated concurrently; keep sink lifetime protected.
- **Tests:** TSan concurrent producers + concurrent flush → clean.
- **Gate:** TSan clean; single-thread behavior unchanged.
- **Migration:** temporary serial I/O contention is acceptable and documented;
  removed when the async backend lands (Phase 4).

### T1.2 Lifecycle admission recheck
- **Reqs:** R40. **Findings:** F4.
- **Areas:** `logger.cpp` Initialize/Shutdown/dispatch.
- **Do:** recheck state under the dispatch lock; define
  `Uninitialized/Accepting/Draining/Stopped` transitions (synchronized admission,
  not a bare boolean); reinit cannot move an in-flight record into a new session.
- **Tests:** shutdown-races-submission; rapid reinit; TSan.
- **Gate:** no lost/misattributed record under the race harness; no lock-order
  deadlock.

### T1.3 Filtering, brace, exact-once category, callable admission
- **Reqs:** R12, R13(partial), R15, R16, R17, R20. **Findings:** F6.
- **Areas:** `log.hpp` macros + template, `logger.cpp`.
- **Do:** resolve category once into a hidden local (R15); route zero-arg format
  through brace processing or require raw text (R17); add a raw-text surface
  (R16); remove/adm-check the public unfiltered `Log()` (R20).
- **Tests:** `{{braces}}` correct; exact-once category (enabled + runtime-
  disabled); raw text with literal braces untouched; direct-submit rejected.
- **Gate:** brace + filter reproducers now pass; compiled-out arg-non-eval test
  still passes.

### T1.4 Honest mode / flush interval / statistics
- **Reqs:** R28(partial), R41, R46, R48. **Findings:** F3, F8, F10.
- **Areas:** `config.hpp`/`log_system.hpp`, `logger.cpp`, `file_sink.cpp`.
- **Do:** implement the timed flush interval in the sync backend or remove the
  setting until implemented; return `Unsupported` for async on a sync build
  (replace the note-only `SetMode`); fix counters so `Written` counts real
  deliveries (not zero-sink / short-write) and drop/attempt/deliver are distinct
  (R48); document visible-vs-durable.
- **Tests:** fake-clock timed flush; abrupt-exit subprocess; statistics semantics
  (a failed write does not inflate `Written`).
- **Gate:** abrupt-exit lost-tail reproducer behaves per documented contract;
  statistics test passes.
- **Migration:** `LogStatistics` shape stays stable; `Dropped` gains meaning.

### T1.5 File-session safety, retention, write integrity
- **Reqs:** R42, R43, R44, R45. **Findings:** F7, F8, F9.
- **Areas:** `file_sink.cpp` → new `session.cpp`/`session.hpp`.
- **Do:** exclusive session creation (no `wb` truncation); gather timestamps
  before a strict-weak-ordering sort; whole-inactive-session-group retention that
  never touches active/unrelated/other-process files; total byte + optional age
  caps with explicit zero semantics; check short/interrupted/flush/close/rotation
  errors and mark the sink unhealthy instead of silent `mFile=nullptr`; error-
  code `directory_iterator` with audited increment.
- **Tests:** rapid reinit same sec/PID; unrelated files preserved; active session
  not pruned; segment groups counted as one session; unreadable-entry sort;
  rollover failure → degraded not silent; disk-full/short-write injection; long-
  run byte bound.
- **Gate:** all file-lifecycle tests pass; no engine exceptions; TSan clean.

### T1.6 Refresh docs/tests to real contracts
- **Reqs:** R51, R53(partial). **Findings:** F12.
- **Do:** remove "synchronous durability" language; document what sync delivery
  actually guarantees; ensure tests assert contracts, not labels.
- **Gate:** docs match behavior; no misleading comments remain.

**Phase-1 exit:** TSan concurrent producers/flush/control clean; subprocess exit
tests; file fault/rotation/reinit tests; no engine exceptions; budgets unchanged.

---

## Phase 2 — Reduce frontend and formatting cost

### T2.1 Split headers; remove heavy includes from the call-site path
- **Reqs:** R5, R6, R21, R22. **Findings:** F5.
- **Areas:** new `log.hpp` (raw+gating), `log_format.hpp`, `log_system.hpp`,
  `log_compat_format.hpp`; `category.hpp` declare/define macros; CMake install set.
- **Do:** remove `config.hpp`/`<format>`/`<filesystem>` from `log.hpp`; move
  configuration to `log_system.hpp` with a borrowed UTF-8 path or platform
  adapter (no public `<filesystem>`); keep no sink class public.
- **Tests:** preprocessed-include check proves no forbidden transitive includes;
  SDK-consumer build without private headers/PCH.
- **Gate:** call-site compile matrix (0/100/1,000/10,000 sites) shows a material
  reduction; fresh build-budget + binary comparison on the calibrated runner;
  `log.hpp` budget **lowered**; ADR 0004 revised (execution vs parse cost).

### T2.2 Lock-free category descriptors
- **Reqs:** R13, R15. **Findings:** F10, F11.
- **Areas:** `category.hpp`, registry in `.cpp`, `Gate()`.
- **Do:** replace `shared_lock`+`unordered_map` filtering with atomic override +
  global threshold scalar loads; explicit registration validates hash/name
  collisions and assigns dense ids; reserved non-hash id for internal/emergency
  category (fixes the id-0 collision nuance).
- **Tests:** runtime-disabled call does no alloc/lock/map (instrumented +
  assembly); collision validation; concurrent threshold updates under TSan.
- **Gate:** disabled-path scalar-only (R13) verified; TSan clean.

### T2.3 Bounded typed formatter + owned record (measured choice)
- **Reqs:** R18, R19, R23, R24, R25, R26, R53. **Findings:** F2(alloc), F9.
- **Areas:** `log_format.hpp` (packing + consteval checker), `format_engine.cpp`
  (`to_chars`, parser), `record.hpp`.
- **Do:** implement `FormatArg`/`FormatInto` with `{BytesWritten,Truncated,
  FormatError}`; consteval grammar validation (R19); owned `RecordHeader`+bytes
  (or delete the dead `LogRecordHeader`); monotonic clock + session anchor + native
  thread id + sequence in the envelope. **Prototype the narrow formatter AND a
  library-backed erased alternative and compare** (CD1) before finalizing.
- **Tests:** allocation-failure injection; raw/format Unicode/brace/precision/
  bounds/oversize; unsupported-type compile-fail fixtures; producer-lifetime
  (temporary strings, thread exit/rename).
- **Gate:** no common-path heap alloc; no throw/abort on bad format; the measured
  winner meets the no-alloc/no-terminate + build contract; decision recorded in
  `decision-log.md`; ADR update.
- **Migration:** un-migrated sites use `log_compat_format.hpp` (named temporary);
  audit format strings/arg types before mechanical switch.

**Phase-2 exit:** call-site compile matrix; allocation-failure injection; raw/
format fuzz; fresh build-budget + binary comparison; ADR 0004 updated.

---

## Phase 3 — Emergency independence

### T3.1 Move emergency reporter into FoundationBase
- **Reqs:** R5, R31. **Findings:** F2, (F8 truncation).
- **Areas:** new `FoundationBase/base/diagnostic.{hpp,cpp}`; logging calls into it.
- **Do:** fixed literal header + bounded copied text; no maps/locks/alloc/logger
  state; mark truncation; no upward dependency on Logging.
- **Tests:** pre-init, post-shutdown, during-teardown emergency; truncation
  flagged.
- **Gate:** include-graph inspection (Base ⇏ Logging); tests pass.

### T3.2 Emergency-first controlled fatal + recursion guard
- **Reqs:** R30, R32. **Findings:** F2.
- **Do:** report emergency/breadcrumb **first**, then optional bounded healthy-
  backend enrichment; first-entry/reentry guard before format/dispatch; fatal
  reports, does not terminate.
- **Tests:** recursive formatter/sink failure (no deadlock/stack growth);
  allocator-failure fatal; fatal does not terminate.
- **Gate:** reentry tests pass; TSan clean.

### T3.3 Direct debugger-critical delivery + bounded breadcrumb ring
- **Reqs:** R27(direct), R33, R34. **Findings:** F10 (debugger snapshot).
- **Do:** `LUDUS_LOG_DEBUG_SYNC` direct path returning `DeliveryResult`;
  attach-aware Windows debugger adapter (no forever-cached presence); producer-
  side breadcrumb ring with atomic-word/generation reads (no seqlock-over-plain-
  bytes); document process-local survivability limit.
- **Tests:** breakpoint with worker parked (direct still delivers); interrupted
  ring write; concurrent-read model; platform audit (mark unexecutable checks).
- **Gate:** direct path independent of the worker; ring reader never reads
  partial; ring size/coverage chosen from measurement (CD3/CD6).

**Phase-3 exit:** reentry/pre-init/post-shutdown/allocator-failure/interrupted-
publication/blocked-worker/breakpoint tests; platform audit; measured ring
coverage.

---

## Phase 4 — Bounded asynchronous backend

### T4.1 MPSC queue + owned records
- **Reqs:** R24, R35, R36, R37. **Findings:** F1 (final), F11.
- **Do:** fixed slots + generation/publication; nonblocking `TryEnqueue`; release/
  acquire; no reuse before release; bounded CAS retries; handle producer-paused-
  after-reservation without reclamation; 64-bit + reduced-width wrap tests.
- **Tests:** TSan; wrap/full/stalled-producer; deterministic-barrier interleavings;
  reservation-interruption.
- **Gate:** TSan clean; no torn entries; not claimed wait-free.

### T4.2 Worker, admission/drain, wakeups, overflow/health
- **Reqs:** R38, R39, R40, R48, R49. **Findings:** F3, F4, F8.
- **Do:** one worker owns sinks; lost-wakeup-safe wakeups + bounded batches;
  admission tokens; drain + final fence; overflow policy + loss summaries; sink
  health + backlog exposure.
- **Tests:** saturation/drop accounting; empty→nonempty wakeup races; timed flush
  with no records; shutdown timeout retains storage (no detach/free).
- **Gate:** bounded memory + producer work; loss explicit; TSan clean.

### T4.3 Fence-based visible/durable flush
- **Reqs:** R28, R29. **Findings:** F3.
- **Do:** capture fence; separate control channel for flush requests; visible vs
  durable; deadline; self-flush returns defined result.
- **Tests:** empty/half/full queue; healthy/stalled publication; concurrent
  requesters; fake-clock; deadline honesty.
- **Gate:** fence covers all accepted ≤ fence; timeout/failure honest; no fence
  starvation.

### T4.4 Consumer migration + Error-visibility compatibility
- **Reqs:** R41, R54, R55. **Findings:** —.
- **Do:** async selected at init (return effective mode); enable Development
  first; identify Error-visibility-dependent sites → explicit critical delivery
  or documented temporary Error-flush policy; migrate `apps/smoke` and any other
  consumers; keep corrected sync backend for tools/tests.
- **Tests:** real file/terminal runs; thread-count scaling; overload benchmark.
- **Gate:** no silent Error-visibility weakening; consumers build/run; budgets
  unchanged.

**Phase-4 exit:** TSan; wrap/full/stalled-producer; fake-clock flush; deadline/
error results; overload + thread-count scaling; real file/terminal runs.

---

## Deferred (separately scoped; do NOT block core — R56–R60, non-goals)

- **D1** bounded typed fields + JSON-lines (opt-in header) — R56.
- **D2** in-editor console subscriber — R57.
- **D3** remote export module — R58.
- **D4** crash-reporter/native-stack/symbolization — R59.
- **D5** per-thread SPSC / deferred formatting — only if Phase-4 misses budget
  (R60).
Each requires an explicit use case/owner/budget and its own tests; none is
implemented speculatively.

---

## Findings F1–F12 → task map

| Finding | Summary | Tasks | Notes |
| --- | --- | --- | --- |
| **F1** | Shared-locked dispatch mutates shared sink scratch (race) | T0.1, T1.1, T4.1 | Reproduced structurally (TSan) + confirmed in code |
| **F2** | Fatal formats+locks+normal-sinks before emergency; no recursion guard | T3.1, T3.2 | Emergency-first + guard |
| **F3** | Flush interval unused; user-space buffering | T0.1, T1.4, T4.3 | Implement timed flush; visible/durable split |
| **F4** | Init check + sink lock not one admission protocol | T1.2, T4.2 | State machine + admission tokens |
| **F5** | `<format>` in `log.hpp`; `<filesystem>` via config | T0.2, T2.1 | Header split; `<filesystem>` ≈ 64k lines measured |
| **F6** | Zero-arg braces; direct `Log()` bypass; category twice | T1.3 | All three reproduced/verified |
| **F7** | Segment pruning, broad match, active-file delete, same-name truncate | T1.5 | Exclusive create + session-group retention |
| **F8** | Unchecked writes; silent file-disable; optimistic `Written` | T1.4, T1.5, T4.2 | Sink health + honest counters |
| **F9** | Allocating std APIs under `-fno-exceptions`; `directory_iterator` throw | T1.5, T2.3 | Full call-chain audit + status boundaries |
| **F10** | Hash-only category keys; stale debugger snapshot; nonfunctional async | T1.4, T2.2, T3.3 | Collision validation; attach-aware; reject async |
| **F11** | No native thread map, monotonic time, sequence, preserved fields | T2.3, T4.1 | Monotonic + native id + sequence in envelope |
| **F12** | Tests single-threaded; flush before testing visibility | T0.1, T0.2, T1.6 | Concurrent + subprocess + barrier tests |

**No finding is dismissed as no-longer-applicable** — the checked-out logging
source is byte-identical to the reviewed revision (`decision-log.md`), so all
twelve stand. Only *nuances* are corrected (see `decision-log.md`).

---

## Migration summary (compatibility)

1. Keep six severity macros as the facade (R11). 2. Move `std::format` behavior
into the named temporary `log_compat_format.hpp` (R53). 3. Audit arg types/format
strings before switching a site to the bounded grammar. 4. Distinguish raw text
from formatting (fix the brace bug, don't preserve it). 5. Keep corrected sync
backend for tools/tests; choose async at init with a returned effective mode;
remove live `SetMode`. 6. Enable async in Development → Profile; keep Release
severity removal configurable. 7. Do not silently weaken Error visibility;
migrate those sites explicitly or keep a documented Error-flush policy.
