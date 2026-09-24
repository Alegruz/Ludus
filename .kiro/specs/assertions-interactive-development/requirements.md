# Assertions — Interactive Development (resumable `ASSERT`) — Requirements

**Spec status:** proposed, not yet implemented. This document turns
[ADR 0006](../../../docs/decisions/0006-resumable-development-assertions.md) and
[`docs/architecture/assertions.md` §5.1](../../../docs/architecture/assertions.md)
into testable requirements for making enabled `ASSERT` / `ASSERT_F` resumable
through explicit developer action in local, non-CI Debug builds. It is
engineering input against the checked-out assertion runtime; it is **not** an
authorization to change runtime or UI code. Implementation is sequenced by
[`tasks.md`](tasks.md) and authorized separately.

**Baseline.** The shipped assertion runtime is terminal for every enabled fatal
kind (`ASSERT`/`REQUIRE`/`FATAL`): `assert.cpp` publishes the report, optionally
breaks if a debugger is attached, then `abort()`s ("debugger continuation never
escapes"). `CHECK` reports best-effort and returns `false`. The generated
`assert_config.hpp` exposes `LUDUS_ENABLE_ASSERTS`, `LUDUS_BREAK_ON_CHECK`,
`LUDUS_BUILD_FLAVOR_ID`, and `LUDUS_ASSERT_POLICY_VERSION`; the Linux backend
exposes `QueryDebugger`, `BreakForDebugger`, `NativeThreadId`,
`TerminateForAssertion`, and `TerminateImmediately`. No interactive-prompt or
CI-detection primitive exists yet.

**Authoritative conflict resolution.** Where this spec and the pre-amendment
"enabled invariant never returns" statement disagree, ADR 0006 and §5.1 win, and
only for `ASSERT`/`ASSERT_F`. `REQUIRE`/`FATAL` stay terminal; `CHECK` stays
recoverable.

---

## 1. Conventions

- **Requirement IDs** are `R<n>`, each with a verification method
  (`Test` / `Inspection` / `Measurement`).
- **MUST / SHOULD / MAY** are RFC-2119 strength.
- "Engine code" = `modules/` and `apps/`; only Catch2 test targets may re-enable
  exceptions via `ludus_enable_test_exceptions()`.
- **Eligible build** = a build whose generated policy sets
  `LUDUS_ASSERT_RESUMABLE == 1` (the Debug flavor only).
- **Resume-permitting run** = an execution of an eligible build in which, at the
  moment of an `ASSERT` failure, the process is not under CI *and* an explicit
  Continue-once action is obtained (debugger continue, or a prompt answered
  Continue once).
- **`[OPEN]`** marks a genuine open decision for a later prompt; **`[CD]`** marks
  a reversible conservative default adopted so work can proceed.

---

## 2. Repository-rule requirements (non-negotiable)

These bind every requirement below and restate the standing rules so this
feature is not "solved" by violating them.

- **R1** — All new/changed engine code MUST compile under C++23 with the pinned
  Clang/LLVM 18 + lld toolchain, warning-clean, warnings-as-errors in CI.
  *Inspection + Measurement (build).*
- **R2** — No engine code introduced by this feature may use C++ exceptions or
  depend on an exception-throwing API on any path; it builds with
  `-fno-exceptions`. *Inspection.*
- **R3** — `noexcept` MUST NOT disguise a failing/allocating operation. The new
  failure-path code either cannot fail or converts failure to Terminate.
  *Inspection.*
- **R4** — Fixed-width Ludus aliases MUST be used instead of `std::` primitive
  spellings. *Inspection (clang-tidy).*
- **R5** — `FoundationBase` MUST NOT gain a dependency on any higher module.
  Interactive-I/O and CI-detection primitives live at the existing private Base
  OS boundary (`diagnostics_*.cpp`), below Logging/Platform. *Inspection (CMake +
  include graph).*
- **R6** — No private implementation header (prompt, CI detection, platform
  backend) may be installed in the SDK or reachable from a public header.
  *Inspection (installed file set + SDK-consumer build).*
- **R7** — Build-time budgets in `config/build_budget.json` MUST NOT be raised to
  absorb this feature; `assert.hpp` / `assert_format.hpp` line and include-graph
  targets are unchanged. *Measurement + Inspection.*

## 3. Scope and taxonomy requirements

- **R10** — Only enabled `ASSERT` and `ASSERT_F` MAY resume. `REQUIRE`,
  `REQUIRE_F`, `FATAL`, and `FATAL_F` MUST remain terminal in every build and on
  every run: a debugger continue past their inspection break MUST still fall
  through to termination. *Test (subprocess death) + Inspection.*
- **R11** — `CHECK` / `CHECK_F` behavior MUST be unchanged: evaluate the
  condition exactly once, report best-effort under the existing budget, return
  `false`. This feature MUST NOT alter the `CHECK` macro, budget, or return
  contract. *Test + Inspection.*
- **R12** — The public macro expansions of `ASSERT` / `ASSERT_F` MUST be
  unchanged. New behavior lives entirely in the runtime `.cpp` reached through
  the existing `FinishFatal*` / `FinishFatalRendered` entry points. `assert.hpp`
  MUST gain no include and no success-path work. *Inspection (header diff +
  optimized assembly).*
- **R13** — The condition of a failing `ASSERT` MUST be evaluated exactly once
  whether or not the failure later resumes; a resumed `ASSERT` MUST NOT
  re-evaluate its condition or its diagnostic arguments. *Test.*

## 4. Build-time eligibility requirements

- **R20** — The generated `assert_config.hpp` MUST add a numeric
  `LUDUS_ASSERT_RESUMABLE`, set to `1` for the Debug flavor and `0` for
  Development, Profile, and Release/MinSizeRel. *Test (config fixtures per
  preset) + Inspection.*
- **R21** — `LUDUS_ASSERT_RESUMABLE`, like the existing policy macros, MUST be
  generated from `LUDUS_BUILD_FLAVOR`, MUST reject predefinition/override (CMake
  `FATAL_ERROR` and header `#error`), and MUST NOT be inferrable from `NDEBUG` or
  `defines.hpp`. *Test (override rejection) + Inspection.*
- **R22** — Adding this policy value MUST bump `LUDUS_ASSERT_POLICY_VERSION` and
  record `enable_resumable` (or equivalently named) in the SDK manifest and
  package metadata. Installed-consumer fixtures MUST observe the value matching
  the SDK variant. *Test (manifest + consumer) + Inspection.*
- **R23** — Multi-config generators MUST remain rejected; per-variant generated
  include roots and manifests MUST stay separate. This feature MUST NOT weaken
  the existing single-config / separate-prefix install contract. *Test +
  Inspection.*
- **R24** — Development MUST be **report-only**: with `LUDUS_ENABLE_ASSERTS == 1`
  and `LUDUS_ASSERT_RESUMABLE == 0`, an enabled `ASSERT` failure MUST report,
  break if a debugger is attached, then terminate — identical to today. It MUST
  NOT present a prompt or resume to the caller. *Test (Development subprocess).*
- **R25** — Profile and Release/MinSizeRel MUST NOT compile `ASSERT`/`ASSERT_F`
  (unchanged), so resumability is moot there; `REQUIRE`/`CHECK`/`FATAL` are
  unaffected. *Test + Inspection.*

## 5. Runtime decision requirements (eligible Debug builds)

- **R30** — In an eligible build, a failed enabled `ASSERT`/`ASSERT_F` MUST
  resume (return to its caller) **only** when *all* hold at failure time:
  (a) the process does not detect a CI environment; (b) exactly one explicit
  Continue-once action is obtained; (c) that action's I/O channel is available.
  Otherwise the outcome MUST be the terminal fatal path. *Test.*
- **R31 (CI veto)** — When the process detects a CI environment, the runtime
  MUST NOT present a prompt and MUST NOT resume, even for a locally built Debug
  binary executed under CI; it MUST take the terminal fatal path. The CI
  detection MUST be queried only on the failure path, never on success.
  *Test (CI env var set) + Inspection.*
- **R32 (debugger continue)** — When a debugger is attached in an eligible,
  non-CI run, the inspection breakpoint MUST be the resume point for `ASSERT`:
  continuing past it returns to the caller. For `REQUIRE`/`FATAL` the same
  continue MUST fall through to termination (R10). *Test (native GDB/LLDB) +
  fake-backend Test.*
- **R33 (detached prompt)** — When no debugger is attached in an eligible,
  non-CI run with usable interactive I/O, the runtime MUST present a **separate**
  decision offering exactly **Continue once** and **Terminate**. "Continue once"
  MUST return to the caller for that single hit; "Terminate" MUST take the
  terminal fatal path. There MUST be no third "ignore always" option and no
  persistent per-site state. *Test (scripted stdin) + Inspection.*
- **R34 (no default-continue)** — If the prompt cannot be presented — no
  controlling terminal, headless/detached run, closed or non-interactive
  stdin/stdout, EOF on the decision channel, or a delegated helper that is
  absent or has exited — the outcome MUST be **Terminate**. The runtime MUST
  never default to Continue and MUST never silently skip a failed `ASSERT`.
  *Test (closed stdin, headless, absent helper).*
- **R35 (single explicit action)** — Resume MUST require one explicit action per
  hit. Malformed, ambiguous, or empty input MUST resolve to Terminate (or
  re-prompt a bounded number of times, then Terminate) — never to Continue.
  There MUST be no timeout that auto-continues. *Test.*
- **R36 (direct-executable startup)** — The behavior MUST be correct for a
  binary launched directly (no launcher/harness). CI detection and I/O
  availability MUST be determined from the actual process environment/descriptors
  at failure time, not from a launcher handshake. A direct non-CI interactive
  Debug run prompts; a direct headless or CI run terminates. *Test (direct
  child, TTY vs pipe vs CI).*

## 6. Failure-path safety requirements

- **R40 (ordering)** — The resume/terminate decision MUST be made only *after*
  the complete owned report is constructed and published and the primary
  emergency write attempt has occurred, i.e. at the current break-then-terminate
  point. A prompt that is interrupted or faults MUST leave the full committed
  report intact. *Test + Inspection.*
- **R41 (owned-report lifetime)** — Because `ASSERT` can now return, its report
  storage MUST follow the recoverable-`CHECK` lifetime (owned bytes valid only
  for the synchronous report; no retained borrowed pointers) and MUST NOT be
  published into the single-incident terminal fatal crash packet. A resumed
  `ASSERT` MUST NOT consume, release, or reuse the `REQUIRE`/`FATAL` terminal
  packet. *Test (ASan lifetime) + Inspection.*
- **R42 (slot/TLS release on resume)** — A resumed `ASSERT` MUST release the
  reporting-owner slot and clear its per-thread entry state exactly like a
  completed `CHECK`, so a later failure on any thread can acquire the slot. A
  resumed `ASSERT` MUST NOT leave the process in the "fatal owner committed"
  state. *Test (resume then trigger another failure).*
- **R43 (recursion/contention never resumes)** — A recursive `ASSERT` on the
  same thread, or an `ASSERT` failing while another failure owns the reporting
  slot, MUST take the immediate secondary/recursive termination path with no
  prompt and no resume, preserving the first committed evidence. *Test
  (barrier-start contention; recursive diagnostic).*
- **R44 (presentation budget)** — A resumed `ASSERT` MUST count against a
  presentation budget analogous to `CHECK` so a hot resumed `ASSERT` cannot
  flood diagnostics or prompt storms; budget exhaustion MUST suppress
  *presentation only* and MUST NOT change whether the condition is evaluated or
  whether an unsuppressed hit can still resume. `[OPEN]` whether the resumed-
  `ASSERT` budget shares the `CHECK` slot budget or uses a separate counter.
  *Test.*
- **R45 (transport/helper loss)** — Loss of the diagnostic transport, or of a
  prompt helper/channel, MUST degrade to Terminate for `ASSERT` and MUST NOT
  block termination for any kind. A full/closed transport or a dead helper MUST
  NOT cause a blocking write or hang; report delivery status MUST be recorded
  as it is today. *Test (full/closed socket; killed helper).*
- **R46 (success path unchanged)** — The success path MUST remain free of
  runtime coordination: no CI query, no debugger query, no I/O, no atomics, no
  TLS read introduced by this feature. Verified in optimized assembly for
  passing `ASSERT`. *Measurement (assembly) + Inspection.*
- **R47 (no exceptions/allocation on approved path)** — The resume decision path
  MUST allocate nothing in the approved minimal path and MUST NOT use exceptions,
  `longjmp`, coroutine suspension, or thread cancellation between Begin and the
  decision. *Test (allocation interception) + Inspection.*

## 7. Documentation & policy requirements

- **R50** — `docs/architecture/assertions.md` MUST state the resumable-`ASSERT`
  contract precisely (done by the §5.1 amendment) and MUST NOT retain a
  contradictory blanket "enabled invariant never returns" claim for `ASSERT`.
  *Inspection.*
- **R51** — [ADR 0006](../../../docs/decisions/0006-resumable-development-assertions.md)
  MUST record the decision, scope, matrix, and alternatives, and MUST be
  referenced from the design. *Inspection.*
- **R52** — The build/execution matrix (flavor × config × debugger × CI ×
  interactive-I/O → outcome) MUST be documented in the design and covered by
  behavior tests (see [`design.md`](design.md) §Matrix and `tasks.md`).
  *Inspection + Test.*

## 8. Open decisions (for later prompts)

- **`[OPEN]` D1 — Prompt channel.** Whether the detached Continue-once prompt
  reads from the controlling TTY directly (`/dev/tty`), from stdin/stdout, or is
  delegated to a small out-of-process helper. Each interacts differently with
  headless detection (R34), direct-executable startup (R36), and datagram
  transport reuse (R45). The requirements above are channel-agnostic; the design
  states the trade-offs and the tasks defer selection with a measured basis.
- **`[OPEN]` D2 — CI detection signal.** Which signals define "CI" (the `CI`
  env var alone, a broader allow-list, or an explicit Ludus opt-out). Must be
  cheap, failure-path-only (R31), and not misfire for a developer who happens to
  set a common variable.
- **`[OPEN]` D3 — Resume budget sharing.** Whether resumed `ASSERT` shares the
  64-report `CHECK` slot budget or uses a distinct counter (R44).
- **`[RESOLVED]` D4 — Source plan.** The authoritative
  `docs/architecture/assertions-interactive-development-plan.md` is now present in
  the repository (it was added on `main` after this branch first diverged, and
  has since been merged in). The spec and the implemented startup/report-delivery
  layer have been reconciled against it: the external helper is the Python tool
  under `tools/diagnostics/`, the startup integration is a target **above** Base
  (`Ludus::DiagnosticsIntegration`, Base does not depend on it), and the control
  protocol uses an explicit byte encoding rather than a padded C++ struct.

## 9. Non-goals (explicit)

- Resumable `REQUIRE`/`FATAL`; a general dialog/handler-chain framework;
  per-site "ignore always" registries; automatic debugger attachment; remote
  continue commands.
- Any Windows/macOS prompt or CI-detection backend (Linux/Clang reference only;
  other backends must fail configuration rather than no-op).
- Changing `CHECK`, the formatter, the terminal crash packet, the transport
  contract, logging integration, or the SDK packaging model beyond adding the
  one new policy value and manifest field.
- Concrete acceptance tests: this milestone specifies *what* must be verified;
  the test implementations are deferred to a later prompt (see `tasks.md`).
