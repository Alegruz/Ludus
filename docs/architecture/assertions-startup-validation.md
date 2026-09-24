# Assertion startup / report-delivery — validation record

Scope: the startup/report-delivery milestone (external diagnostic helper,
versioned control endpoint, startup integration, smoke wiring, CI report-only
policy). See `assertions.md` §5.2 and the Kiro spec task T1.5. This milestone
**does not change any assertion's fatal action**.

This record states exactly what was executed and what was **not**, honestly,
given the environment the change was authored in.

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

- **Compilation, strict warnings.** `diagnostic_output.cpp`,
  `diagnostic_startup.cpp`, `diagnostics_linux.cpp`, the helper
  (`apps/diagnostic_helper/main.cpp`), and the startup test child compiled clean
  under `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -fno-exceptions`.
- **End-to-end behavior** — `tests/assertions/startup_tests.py` run against a
  Clang-15 build of the real helper + a child linking the real
  `diagnostic_startup`/`diagnostic_output`/`diagnostics_linux` sources. All seven
  cases passed:
  1. Helper end-to-end: versioned Hello/HelloAck handshake completes
     (`control=1`); pre-init and post-shutdown reports are drained to the
     helper's own stdout, independent of Logging.
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
- **Python** driver `py_compile`-clean.

## What was NOT executed here (runs in CI on the pinned toolchain)

- Pinned **Clang 18 + lld** engine build of FoundationBase/Logging/Platform and
  the smoke app; warnings-as-errors under the project warning set.
- **Catch2** unit tests and the existing assertion death/concurrency CTest suite.
- **ASan/UBSan** (and TSan) runs.
- `./scripts/check --all` with the pinned **clang-tidy 18**.
- `./scripts/install-sdk` and the SDK-consumer checks (the new public header
  `diagnostic_startup.hpp` and unchanged private-header non-leak).
- Real interactive TTY / live Wayland-X11 display probing on a desktop session
  (the sandbox is headless; `HasGraphicalDisplay` was exercised only in its
  negative, no-display path).

## Explicitly unchanged / not covered

- No assertion fatal action changed; `assert.hpp` and the success path are
  untouched. No native death behavior was faked.
- The ASSERT continue-once **decision** (prompt + decision-frame exchange + the
  `FinishFatalImpl` branch) is **not** implemented here; the reserved
  `DecisionRequest`/`DecisionReply` frames are defined but never sent.
- Windows/macOS backends are not provided (unsupported backends fail
  configuration, per the existing rule).

## Open item

`docs/architecture/assertions-interactive-development-plan.md`, referenced by the
task prompt, does **not** exist in the repository (checked working tree, history,
and branches). This milestone was implemented from ADR 0006, `assertions.md`
§5.1/§5.2, the Kiro spec, and the prompt. If that plan document exists elsewhere,
it should be added and this work reconciled against it (spec open decision D4).
