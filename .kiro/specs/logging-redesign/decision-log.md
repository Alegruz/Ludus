# FoundationLogging redesign — Decision log

Separates **measured evidence** from **hypotheses**, records the checked-out
revision and toolchain reality, and classifies every review finding against the
actual code. This is the honesty ledger for the spec.

---

## 1. Revision and environment

- **Checked-out revision:** `068618c` on branch `main`. The review inspected
  `269b6118`. The **only** commit between them is the docs commit that added the
  review + evidence; `git diff 269b6118..HEAD -- modules/foundation/logging` is
  **empty**. ⇒ the logging implementation is byte-identical to the reviewed
  source, so every source-level finding applies to the current tree. *(measured:
  `git log`/`git diff`.)*
- **Verification-environment limitation (important):** the pinned toolchain
  (Clang/LLVM 18 + lld + a `<format>`-capable libstdc++ ≥ 13) is **not available**
  in this sandbox. Present: `clang++ 15.0.7` (Amazon Linux) with `libstdc++-devel`
  **11** headers (no `<format>`), and `g++ 11.5`. Consequences:
  - I could **not** run `./scripts/build`/`./scripts/test`, nor reproduce the
    race against the real engine binary with the pinned compiler.
  - I reproduced the defects **faithfully at the structural/logic level** instead
    (below). The pinned-toolchain, real-engine reproduction is **Phase-0 work**
    (task T0.1), not something I can claim done here.
- **Host/load at measurement:** Linux 6.1 x86_64, 8 cores, loadavg ≈ 0.76/0.50/
  0.31. Artifacts: `/projects/sandbox/.scratch/logging-evidence/`
  (`behavior_model.*`, `race_model.*`, `environment.txt`) — scratch outside the
  repo tree; **not committed**.

---

## 2. Measured evidence (reproduced or deterministic)

| # | Claim | Method | Result |
| --- | --- | --- | --- |
| E1 | **F6 zero-arg brace bug** | `behavior_model.cpp` mirrors `log.hpp Log<>()` `if constexpr(sizeof...==0)` branch | zero-arg `Log("escaped {{braces}}")` → `"escaped {{braces}}"` verbatim; format path would yield `"escaped {braces}"`. **Reproduced.** |
| E2 | **F6 direct-call filter bypass** | same model, no ShouldLog recheck in `Log()` | direct `Log(Info,…)` delivered=1 while `ShouldLog(Info)` under Warning threshold = false. **Reproduced.** |
| E3 | **F1 concurrent sink-scratch race** | `race_model.cpp` = 8 producers, `shared_lock` + one sink-owned `std::string` `clear()/append()`, GCC-11 TSan | data race writing `basic_string::_M_set_length` inside `clear()` from `Sink::Write` under **read-held** rwlock (`mutexes: read M13`). **Signature matches** review `tsan.txt` (which shows the same write reached via `FormatConsoleLine → FileSink::Write → DispatchMessage`, `mutexes: read M0`). **Reproduced structurally.** |
| E4 | **F5 heavy transitive includes** | `clang -E` preprocessed line counts (deterministic; wall-time too noisy) | empty TU = 8 lines; `<chrono>` = 11,554; `<filesystem>` = 63,690. `log.hpp` includes `<format>` **and** `config.hpp`→`<filesystem>`, so every logging consumer pays this. **Confirmed** (the `<format>` leg itself is not measurable here → rely on CI numbers below). |

**Historical / CI numbers used as context, NOT re-measured here:**
- `build_budget.json` calibration: `log.hpp` ≈ 2240 ms avg CI parse (budget
  3200 ms), frontend ≈ 45 s (GitHub ubuntu-24.04 runner, ~2× a laptop).
- ADR 0004: erasure moved frontend 52.5 → 22.5 s, `log.hpp` aggregate ~19 → ~9 s
  (earlier measurement, explicitly not a fresh before/after).
- The review's own include timings are noisy and **not calibrated gates** (its
  own caveat); only its deterministic preprocessed sizes are durable — matching
  my approach in E4.

---

## 3. Findings F1–F12 classification (vs the actual code)

All twelve are **confirmed defects** in the current tree (code-read; several
also reproduced). None is obsolete.

| F | Confirmation method | Verdict |
| --- | --- | --- |
| F1 | code (`DispatchMessage` shared_lock → `sink->Write` → `FormatConsoleLine` on shared `mScratch`) + E3 | confirmed defect |
| F2 | code (`DispatchMessage`: initialized Fatal formats, takes shared_lock, writes all sinks, flushes, **then** `EmergencyLog`; no recursion guard) | confirmed defect |
| F3 | code (`FlushIntervalMilliseconds` stored in `LogConfig`, referenced nowhere; no timer/worker exists) | confirmed defect |
| F4 | code (`Initialize`/`Shutdown` take unique_lock, clear sinks; a producer past `Initialized` check can take its shared_lock after shutdown cleared sinks) | confirmed defect |
| F5 | code (`log.hpp` includes `<format>` + `config.hpp`; `config.hpp` includes `<filesystem>`) + E4 | confirmed defect |
| F6 | code (`Log<>()` zero-arg branch; public `Log()` no recheck; macro evaluates category in both `ShouldLog` and `Log`) + E1/E2 | confirmed defect |
| F7 | code (`fopen("wb")` truncates; `pruneOldSessions` matches `*_pid-*.log`, sorts in comparator, counts segments, can delete other-process files) | confirmed defect |
| F8 | code (`Written.fetch_add` runs after the sink loop regardless of sink count / short write; `rotateIfNeeded` sets `mFile=nullptr` on failure silently; `Dropped` never changes) | confirmed defect |
| F9 | code (`std::filesystem`/`std::vformat`/`std::string` allocate under `-fno-exceptions`; `directory_iterator` increment can select throwing overloads despite error-code construction) | confirmed risk |
| F10 | code (`unordered_map<uint32,LogLevel>` keyed by hash only → collision merges categories; `SetMode(Async)` note-only, init-with-async silent; `DebuggerSink` samples `IsDebuggerPresent()` once at ctor) | confirmed defect |
| F11 | code (`GetNowNanoseconds` uses `system_clock` (wall); thread id is a monotonic counter not the OS tid; `ThreadLocalIdentity` id 0 → "Main" regardless of whether it is the main thread; no sequence) | confirmed defect |
| F12 | code (all three test files single-threaded; `logging_tests`/`formatting_tests` call `Flush()` before reading; no TSan/subprocess/barrier tests) | confirmed gap |

### Corrections / nuances added with evidence (not in, or refined from, the review)
1. **`Written` with zero sinks — confirmed and slightly stronger:** `Written`
   increments unconditionally after the sink loop for any initialized non-pre-
   init record, so it counts even with an empty `Sinks` vector or a short/failed
   `fwrite`. (Supports F8; the review said "can"; it always does when
   initialized.)
2. **`source_location::current()` placement is *better* than a naive reading:**
   it sits inside the ternary true-branch, so a **macro** call that is runtime-
   disabled does not capture source. The residual F6 problems are the **double
   category evaluation** and the **direct `Log()`** path — both real (E2).
3. **Compiled-out arg non-evaluation already holds and is tested**
   (`category_tests.cpp` raises `LUDUS_COMPILED_LOG_LEVEL=4`). The redesign must
   *preserve* this (R12) and extend the guarantee to category evaluation and to
   instantiation/code-size absence.
4. **`EmergencyNote` category id collision:** it builds `LogCategory{0u,
   "Logging"}`; id `0` collides with any real category whose FNV-1a hash is `0`.
   Minor addition to F10; the redesign reserves a non-hash internal id.
5. **The `log.hpp` comment "id + integer comparison" for `ShouldLog` is
   inaccurate today** — it takes a `shared_lock` + `unordered_map` lookup. The
   redesign makes the comment true (R13).
6. **`LogRecordHeader` is dead code** (defined, `static_assert`'d ≤ 32 bytes,
   never used; its comment conflates steady/wall time). The redesign implements
   it for the queue or deletes it (R23) — no dead ownership contract.

---

## 4. Hypotheses (NOT claimed as validated)

These are design directions or expectations that require the Phase-0/2/4
measurements before being asserted as fact:

- **H1** — The narrow typed formatter will meet the no-alloc/no-terminate + build
  contract at acceptable maintenance cost. *Recommended `[CD]`, pending the
  Phase-2 prototype comparison against a `{fmt}`-erased alternative.* Deciding
  now would be "selection on reputation," which the review forbids.
- **H2** — Splitting `<format>`/`<filesystem>` out of `log.hpp` materially lowers
  build cost. *Strongly indicated by E4 + ADR 0004 history, but the fresh
  before/after on the calibrated runner is Phase-2 work.*
- **H3** — A bounded MPSC with one worker lowers producer tail latency without
  losing critical delivery. *Architectural expectation; Phase-4 benchmarks decide;
  per-thread queues only if MPSC misses the budget (R60).*
- **H4** — `std::to_chars` covers the numeric-conversion needs allocation-free.
  *Candidate; measure output/support (R8/T2.3).*
- **H5** — Queue slot/count, breadcrumb ring size, and per-call ns budgets. *All
  `[MD]`; the review's 1,024 B/4,096-slot/2,048×256 B/1 s values are prototype
  starting points, explicitly not accepted budgets.*

**Not claimed:** that any of the proposed architecture "meets benchmarks."
Nothing is implemented; no throughput/latency number is asserted for the target
system.

---

## 5. Missing inputs (requested, not fabricated)

- `docs/architecture/assertions.md` and `assertions-rabin-review.md` — **absent**
  from the checkout (`file_search` found none). Treated as *proposed*; the spec
  only defines the FoundationBase reporter boundary (R30) and does not assume any
  assertion API. **Please supply them** if they should constrain fatal/termination
  ownership.
- GPG5 (Duquette) — unavailable, as the review already stated. No requirement
  depends on it; remote transport is deferred (R58) and non-goal for core.

---

## 6. Conservative defaults (all reversible) — see requirements §13

CD1 formatter = narrow typed (pending measurement) · CD2 overflow = drop-low +
Warning/Error headroom · CD3 keep review prototype sizes only as measurement
starting points · CD4 keep private virtual sinks (status-returning) · CD5 text-
only files first · CD6 emergency/breadcrumb default coverage = Warning+ +
configured categories. Each is flagged in `requirements.md` and can be revisited
without changing the public API surface.
