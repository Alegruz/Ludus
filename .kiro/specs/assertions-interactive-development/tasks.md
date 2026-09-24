# Assertions — Interactive Development (resumable `ASSERT`) — Tasks

Dependency-ordered implementation batches for making enabled `ASSERT`/`ASSERT_F`
resumable in local non-CI Debug builds. Each batch lists requirement IDs
(from [`requirements.md`](requirements.md)), affected areas, regression tests,
and acceptance gates. This document is a **plan**; nothing here is executed yet,
and concrete acceptance-test implementations are deferred to a later prompt
(they are named as gates, not written here).

**Global gate for every batch** (from `AGENTS.md`; pinned Clang 18 + lld):
warning-clean build, `./scripts/test <preset>`, `./scripts/check --all`
(format + tidy), `./scripts/build linux-clang-asan-ubsan` + tests clean. Batches
that touch concurrency add the separate **TSan** tree. Public-config changes add
SDK install/consumer validation (`./scripts/install-sdk`). No engine exceptions;
no budget raises (R1, R2, R7).

**Sequencing rule.** Policy/config lands before any runtime branch, and the
runtime branch lands before call-site or QA work — mirroring the M0→M4 ordering
already used in the assertion program. Docs (this milestone) are already done.

---

## T0 — Documentation & policy (this milestone) — DONE except review

- **Reqs:** R50, R51, R52. **Areas:** `docs/architecture/assertions.md`,
  `docs/decisions/0006-resumable-development-assertions.md`, this spec.
- **Done:** §5.1 amendment, taxonomy/matrix/§12 updates, ADR 0006, spec
  requirements/design/tasks. Old fatal-only contract resolved for `ASSERT` only.
- **Gate:** Markdown link/anchor check; no contradictory "enabled invariant
  never returns" claim remains for `ASSERT`; `git diff --check` clean. No engine
  build is claimed for a docs-only change.
- **Non-goals:** any runtime/config/UI code.

## T1 — Generated policy value and SDK identity (config only)

- **Reqs:** R20, R21, R22, R23. **Findings:** design §2.
- **Areas:** `cmake/EngineBuildFlavor.cmake`, `cmake/assert_config.hpp.in`,
  `cmake/LudusSdkManifest.json.in`, `scripts/python/engine.py` (SDK verification
  only), `tests/build_contract/` fixtures, `.github/workflows/ci.yml`
  (assertion-policy matrix), `docs/development/building.md`.
- **Do:** add `LUDUS_ASSERT_RESUMABLE` (1 for Debug, else 0), forbidden-override
  guard, bump `LUDUS_ASSERT_POLICY_VERSION` to 2, add `enable_resumable` manifest
  field. No runtime consumer of the value yet.
- **Regression tests (deferred impl):** per-preset config fixture asserts
  expected `RESUMABLE`; override/predefinition rejected (CMake + header);
  manifest/header agreement; installed consumer observes the value; multi-config
  still rejected; separate-prefix install unaffected.
- **Gate:** all five presets produce the expected policy; SDK consumers for
  Debug/Development/Profile/Release build and report matching policy; existing
  unit/ASan/UBSan/consumer checks green; `assertion-policy` CI job green.
- **Non-goals:** any behavior change; the value is inert until T2.

## T1.5 — Startup / report-delivery layer (IMPLEMENTED)

Establishes the channels and delivers reports so a later explicit decision has
something to talk over. It changes **no** assertion action. Traces to
`docs/architecture/assertions.md` §5.2.

- **Reqs:** R5, R6 (no SDK leak), R31 (CI detection, startup use), R34/R45
  (degrade/never block), R36 (direct-executable startup), plus the milestone's
  own delivery/handshake/helper-cleanup requirements.
- **Areas:** `modules/foundation/base/include/ludus/foundation/base/diagnostic_output.hpp`
  (versioned control endpoint), `.../diagnostic_startup.hpp` (new public startup
  API), `modules/foundation/base/src/{diagnostic_output.cpp,diagnostic_startup.cpp,diagnostics_linux.cpp}`,
  `apps/diagnostic_helper/` (external helper), `apps/smoke/main.cpp` (init before
  workers/logger), `.github/workflows/ci.yml` (explicit report-only),
  Base `CMakeLists.txt` + root `CMakeLists.txt`, `tests/assertions/startup_tests.py`,
  `modules/foundation/base/tests/diagnostic_startup_child.cpp`.
- **Done:**
  - Versioned control endpoint (`ConfigureControlEndpoint` + Hello/HelloAck,
    `SOCK_SEQPACKET`); `DecisionRequest`/`DecisionReply` frames defined but not
    sent (reserved for T3).
  - `InitializeDiagnostics()` reads inherited fds, configures the report
    (`SOCK_DGRAM`, reused transport) and control transports, resolves mode, and
    reports failed interactive setup visibly; never launches a helper/UI.
  - Startup-only CI detection + terminal/live-display probing at the private OS
    boundary (validate capability, not a found executable).
  - External `ludus_diagnostic_helper`: owns collector ends, launches the engine
    with fds via env, drains report datagrams to its own output, answers the
    handshake, and cleans up on child exit.
  - Smoke initializes diagnostics before workers/logger; CI sets
    `LUDUS_DIAGNOSTIC_INTERACTIVE=0` explicitly.
- **Tests (implemented):** `ludus_diagnostic_startup` CTest — helper handshake +
  pre-init/post-shutdown report drain independent of Logging; helper cleanup on
  child exit; CI forces report-only (control unconfigured); direct/no-helper
  report-only; malformed handshake ⇒ control `Failed`; stderr-closed still
  delivers via datagram; full report socket bounded (no block).
- **Gate:** the standard build/test/check/ASan-UBSan/SDK matrix on the pinned
  Clang 18 toolchain; no assertion action changed; `assert.hpp` unchanged.
- **Non-goals:** the ASSERT continue-once **decision** (T2/T3); any prompt UI;
  Windows/macOS.

## T2 — Detached backend primitives (Base, private)

- **Reqs:** R5, R6, R31 (query only), R34 (degrade), R35, R45, R47.
  **Findings:** design §1, §5. **Depends on:** T1.5 (control endpoint + CI
  detection already exist; T2 adds the `PromptAssertDecision` prompt and the
  decision-frame exchange over the T1.5 control channel).
- **Areas:** `modules/foundation/base/src/internal/diagnostic_platform.hpp`
  (add `ResumeDecision`, `DetectContinuousIntegration`, `PromptAssertDecision`),
  `modules/foundation/base/src/diagnostics_linux.cpp` (Linux impls),
  Base `CMakeLists.txt` if new TUs are added, Base tests.
- **Do:** implement CI detection (failure-path-only) and the interactive
  Continue-once / Terminate prompt behind the private boundary. Resolve `[OPEN]`
  D1 (prompt channel) and `[OPEN]` D2 (CI signal) with a short measured/justified
  note in the PR. Both `noexcept`, allocation-free on the approved path, no
  public-header exposure, unsupported platforms fail configuration.
- **Regression tests (deferred impl):** CI-var set/unset → detection result;
  scripted-TTY prompt returns Continue vs Terminate; closed/EOF/non-interactive
  stdin → Terminate; absent/killed helper (if the helper channel is chosen) →
  Terminate; no-allocation interception on the prompt path.
- **Gate:** private headers not installed (R6); no upward Base dependency (R5);
  ASan/UBSan clean; format/tidy.
- **Non-goals:** wiring into `FinishFatalImpl` (that is T3); Windows/macOS.

## T3 — Resumable `ASSERT` runtime branch

- **Reqs:** R10, R11, R12, R13, R30, R31, R32, R33, R36, R40, R41, R42, R43,
  R44, R46. **Findings:** design §3.
- **Areas:** `modules/foundation/base/src/assert.cpp` (`FinishFatalImpl` and the
  `ASSERT` report ownership path), possibly `internal/diagnostic_record.hpp` /
  `internal/diagnostic_finish.hpp` for `ASSERT`-report ownership; Base tests
  (native death children + fake-backend + resume children).
- **Do:** insert the resumable branch gated on `LUDUS_ASSERT_RESUMABLE`, kind
  `Assert`, non-CI, and explicit action; make a resumed `ASSERT` use `CHECK`-style
  stack-owned reporting, release the slot, clear TLS, and return, without ever
  touching `gFatalPacket`. Keep `assert.hpp` and the success path byte-identical.
  Resolve `[OPEN]` D3 (budget sharing) with a note.
- **Regression tests (deferred impl):** subprocess matrix from design §4 — Debug
  non-CI attached-continue returns; detached-prompt Continue returns / Terminate
  terminates; headless/CI terminate; recursion/contention terminate; Development
  report-only terminates with no prompt; `REQUIRE`/`FATAL` continue-past-break
  still terminates; a resumed `ASSERT` followed by another failure re-acquires
  the slot; condition evaluated exactly once; ASan report-lifetime across resume;
  optimized-assembly success path unchanged (no new query/atomic/TLS); allocation
  interception on the resume path.
- **Gate:** all above plus TSan on the contention/slot cases; native GDB/LLDB
  continue/detach smoke; the standard build/test/check/ASan/UBSan/SDK matrix;
  `assertion-policy` CI job green (Debug prompts must not hang the runner — the
  CI veto R31 is exercised here).
- **Non-goals:** call-site adoption; QA-handoff; non-Linux backends.

## T4 — QA / deployment verification and call-site guidance

- **Reqs:** R36, R45, R52 (matrix in a real deployment). **Findings:** design
  §4, §5.
- **Areas:** `docs/architecture/` (a short verification note), optional selected
  call sites, CI documentation.
- **Do:** verify the matrix end-to-end for a directly launched Debug binary
  (interactive shell prompts; piped/headless terminates; CI terminates) and that
  transport/helper loss degrades to Terminate without hanging. Document that
  Development stays report-only and that resume never reaches shipping.
- **Gate:** documented manual/automated evidence; no regression in the T1–T3
  gates; no accidental resume in Development/Profile/Release.
- **Non-goals:** any new platform, collector, or async logging work.

---

## Traceability summary

| Requirement group | Batch |
| --- | --- |
| R50–R52 (docs/policy contract) | T0 |
| R20–R23 (generated policy, SDK identity) | T1 |
| R5/R6/R31/R34/R36/R45 (startup + helper + control endpoint) | T1.5 (implemented) |
| R31/R34/R35/R45/R47 (backend primitives) | T2 |
| R10–R13, R30–R33, R36, R40–R44, R46 (runtime branch) | T3 |
| R36/R45/R52 (deployment verification) | T4 |

## Open decisions carried into implementation

- `[OPEN]` D1 — prompt channel (`/dev/tty` vs stdin/stdout vs helper): resolve in
  T2.
- `[OPEN]` D2 — CI-detection signal set: resolve in T2.
- `[OPEN]` D3 — resumed-`ASSERT` budget sharing vs dedicated counter: resolve in
  T3.
- `[OPEN]` D4 — the referenced `assertions-interactive-development-plan.md` is
  absent from the repo; reconcile this spec with it if/when it is supplied.
