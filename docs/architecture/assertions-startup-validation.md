# Assertion startup / report-delivery — validation record

Scope: the startup/report-delivery milestone (external diagnostic helper,
versioned control endpoint, startup integration, smoke wiring, CI report-only
policy). See `assertions.md` §5.2, the authoritative
[`assertions-interactive-development-plan.md`](assertions-interactive-development-plan.md)
(Prompt 2), and the Kiro spec task T1.5. This milestone **does not change any
assertion's fatal action**.

This record states exactly what was executed and what was **not**, honestly,
given the environment the change was authored in.

## Architecture (reconciled with the plan)

- The external helper is the **Python** tool
  `tools/diagnostics/ludus_diagnostic_helper.py` (standard library only this
  milestone; the Zenity dialog is a later milestone), not a C++ app.
- The startup integration is a target **above** FoundationBase,
  `Ludus::DiagnosticsIntegration` (`tools/diagnostics/`, header
  `ludus/diagnostics/session.hpp`, `InitializeDiagnosticSession`). It depends on
  Base; **Base does not depend on it**.
- The control channel uses an **explicit little-endian byte encoding** (16-byte
  header + payload), not a compiler-padded C++ struct. The report channel reuses
  Base's existing nonblocking `SOCK_DGRAM` transport.
- CI detection is a shared Base primitive (`IsContinuousIntegration`), reused by
  the future failure-path veto; the terminal/display probes live in the
  integration layer.

## Environment limitation

The authoring sandbox does **not** have the pinned Ludus toolchain: it provides
Clang 15 only (no `-std=c++23`), no Conan, no Catch2, and no bootstrapped
`out/host-tools/`. The full pinned pipeline (Clang 18 + lld, Conan, Catch2,
`./scripts/*`, ASan/UBSan/TSan, `install-sdk`) therefore could not be run here.
Those gates run in CI (`.github/workflows/ci.yml`) on the pinned toolchain and
remain the authoritative acceptance path.

## What WAS executed (Clang 15, C++20, `-fno-exceptions`)

The new/changed sources are POSIX + C++20-compatible, so they were compiled and
run standalone with the available compiler:

- **Compilation, strict warnings.** Base `diagnostic_output.cpp` and
  `diagnostics_linux.cpp`, the integration `tools/diagnostics/src/session.cpp`,
  and the startup test child compiled clean under
  `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -fno-exceptions`. The smoke
  app's diagnostics usage was compiled against the new
  `ludus/diagnostics/session.hpp` API.
- **End-to-end behavior** — `tools/diagnostics/tests/startup_tests.py` run
  against a Clang-15 build of the startup child + the real Python helper. All
  seven cases passed (twice, including after clang-format):
  1. Helper end-to-end: the explicit byte-encoded Hello/HelloAck handshake
     completes (`control=1`); pre-init and post-shutdown reports are drained to
     the helper's own stdout, independent of Logging.
  2. Helper cleanup on child exit: helper returns the child's code, no hang.
  3. CI forces report-only: `ci=1`, control endpoint left unconfigured, report
     still delivered.
  4. Direct launch without a helper: report-only, no transport, no crash.
  5. Malformed handshake (bad-magic ack): control endpoint resolves `Failed`,
     process keeps running, no hang.
  6. `stderr` closed: report still delivered via the datagram transport.
  7. Full report socket: startup and reporting are bounded, never block.
- **Formatting:** `clang-format` applied and re-verified clean against the
  repository `.clang-format`. NOTE: the sandbox `clang-format` is **v22**, not
  the pinned **v18**; CI's pinned formatter is authoritative and may adjust minor
  layout.
- **CI workflow YAML** validated to parse (PyYAML); all three jobs carry
  `LUDUS_DIAGNOSTIC_INTERACTIVE=0`.
- **Python** helper and driver `py_compile`-clean.

## What was NOT executed here (runs in CI on the pinned toolchain)

- Pinned **Clang 18 + lld** engine build of FoundationBase/Logging/Platform, the
  `Ludus::DiagnosticsIntegration` target, and the smoke app; warnings-as-errors
  under the project warning set.
- **Catch2** unit tests and the existing assertion death/concurrency CTest suite.
- **ASan/UBSan** (and TSan) runs.
- `./scripts/check --all` with the pinned **clang-tidy 18**.
- `./scripts/install-sdk` and the SDK-consumer checks (the integration header and
  the installed Python helper; unchanged private-header non-leak).
- Real interactive TTY / live Wayland-X11 display probing on a desktop session
  (the sandbox is headless; the display probe was exercised only in its negative,
  no-display path).

## Explicitly unchanged / not covered

- No assertion fatal action changed; `assert.hpp` and the success path are
  untouched. No native death behavior was faked.
- The ASSERT continue-once **decision** (prompt + decision-frame exchange + the
  `FinishFatalImpl` branch) is **not** implemented here; the reserved
  `DecisionRequest`/`DecisionReply` kinds are defined in the wire layout but
  never sent, and no `LUDUS_ASSERT_DIALOGS_AVAILABLE` capability macro or policy
  version bump is introduced yet (that is the later decision milestone).
- Windows/macOS backends are not provided (unsupported backends fail
  configuration, per the existing rule).
