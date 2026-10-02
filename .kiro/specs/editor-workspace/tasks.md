# Ludus Editor workspace implementation tasks

All tasks are initially incomplete. Execute E0.0-E0.5 in dependency order.
Requirements E01-E15 and design.md define behavior; the research supplies
self-contained rationale. Do not treat future illustrative paths or APIs as
already implemented. Preserve unrelated work and revalidate the current checkout.

## E0.0 Dependency and boundary spike

- [ ] Read AGENTS.md, steering, ADRs 0003/0004/0005/0007 and the package; inspect
      actual tooling/API status and record the starting revision/dirty paths.
- [ ] Introduce LUDUS_BUILD_EDITOR OFF and native-host gating. Find Qt only in
      the enabled app subdirectory; use target-local AUTOMOC if needed,
      QT_NO_KEYWORDS and typed signal connections. Do not enable Qt globally.
- [ ] Build a minimal window with Base/Logging and Qt Core/Gui/Widgets under
      Clang 18, C++23, warnings-as-errors and -fno-exceptions. Inspect compile
      commands, dependencies, platform plugins and shared-library linkage.
- [ ] Record the exact Qt/compiler/package versions and dependency notices.
      Verify native Wayland startup and an offscreen test. Report an unavailable
      environment rather than declaring the spike complete.
- [ ] Verify a separate clean Editor-OFF configuration and SDK installation have
      no Qt dependency. Existing browser configuration remains Editor-free.

Gate: E01/E02 proven without relaxing project standards. No custom GUI/RHI/input
framework is added. Failure here is evidence for a focused design correction,
not permission to introduce application exceptions or unreviewed dependencies.

## E0.1 Project model and persistence

- [ ] Implement WorkspaceState, typed actions/results, a small transition
      function, and ProjectStore; no widget-owned settings copies.
- [ ] Implement version-1 parsing, bounds, relative-path rules, saved/draft
      comparison, explicit errors and canonical serialization.
- [ ] Implement cooperative save lock, disk-digest conflict detection and
      QSaveFile replacement with direct-write fallback disabled. Add narrow
      fault-injection boundaries for write/commit failures.
- [ ] Add the engine sample descriptor, ignore generated .ludus directories, and
      add parser fixtures consumed by both
      C++ and Python validation, including valid/invalid Unicode and types.
- [ ] Test failed Open retaining the previous project, save/reopen round-trip,
      partial writes, commit failure, external change/deletion, failed save
      aborting Close/Reload, and the dirty action paths.

Gate: E03-E05. Tests verify destination bytes and dirty/saved state after failures,
not only that a serializer produces the expected string.

## E0.2 Shared tooling and adapter commands

- [ ] Extract only query/read target-resolution logic from rad_debugger.py into
      cmake_targets.py. Preserve RAD session behavior and argument limitations;
      rerun existing tests before adding Editor orchestration.
- [ ] Add editor_project.py semantic validation and editor_tool.py with the
      fixed configure/build/build_run operations and private stdio protocol.
- [ ] Reuse bootstrap, managed executable and environment helpers. Extract
      command planning for existing configure cache-repair logic only if needed;
      do not run a blocking engine.run or monkeypatch global tooling functions.
- [ ] Implement named codemodel-v2 query, bounded reply validation, executable
      selection, custom artifact paths, source/build identity checks, and
      post-build re-resolution. Add a cooperative nonblocking per-tree lock.
- [ ] Implement explicit ludus/cmake provider branches and the external
      installed-SDK sample's two native presets. Document prerequisite SDK
      environment and unsupported generator/tree conventions.
- [ ] Test missing/stale tools, unknown target, library target, ambiguous target,
      malformed/oversized/traversing replies, failed configure/build, CMake
      regeneration, custom output names and no stale-artifact launch.

Gate: E06/E07/E13/E14 tooling contracts. Fake commands test planning/failure
sequences; a real installed-SDK sample verifies the external-consumer boundary.

## E0.3 Streaming and real process lifecycle

- [ ] Implement production selectors supervisor and incremental protocol/UTF-8
      framing. Use nonblocking writes with bounded output/control queues and the fixed encoded-output credit window;
      bound QProcess internal backlog as well as the visible log.
- [ ] Implement child session ownership, Linux non-reaping exit observation,
      descendant cleanup, TERM/KILL deadlines and direct-child reaping. Keep
      platform details private to tooling; no new engine process module.
- [ ] Implement cancel, parent EOF and supervisor TERM/SIGINT handling; reject
      malformed/version-mismatched control, enforce one request/job, and emit
      one consistent terminal result only after cleanup.
- [ ] Add real subprocess fixtures: long-running child, ordinary grandchild,
      TERM-ignoring child, exiting leader with surviving writer, simultaneous
      output, no-newline output, invalid UTF-8, nonzero/signal exit and failed
      executable spawn. Include empty/quoted/Unicode/shell-looking argv/cwd.
- [ ] Test cancellation before spawn, during every phase, repeated Stop, build
      success racing Stop, stdout partial frames, slow/no protocol reader, missing/invalid/duplicate credit and
      stdin EOF. Confirm unrelated test processes are never signaled.
- [ ] Distinguish healthy-supervisor EOF cleanup from abrupt supervisor death;
      assert CleanupUnknown for the latter, with no blind retry.

Gate: E08/E10/E11/E14 real OS behavior. Tests synchronize with fixture readiness
messages/pipes and deadlines, not arbitrary sleeps. Mocked Popen success alone
cannot prove cleanup, argument integrity or freedom from pipe deadlocks.

## E0.4 Complete workspace UI

- [ ] Add MainWindow/EditorController/ToolProcess/LogBuffer, the form/runtime
      status/output layout, argument list editor and documented actions.
- [ ] Use one state source; centralize capabilities, block render-triggered edit
      signals, defer reentrant actions, bind callbacks to QObject lifetime and
      reject stale job/epoch events. Stop wins before launch.
- [ ] Implement asynchronous dirty/close dialogs, cancel-write fallback and
      CleanupUnknown operator recovery. Never destroy a running QProcess to
      simulate orderly cancellation or block GUI on waitForFinished.
- [ ] Implement bounded logs, omission markers, retained last results and Copy
      Job Details. Preserve plain-text output and exact command arguments.
- [ ] Add scripts/editor for launching an already-built application with an
      explicit trusted tooling-root/Python; do not silently prepare dependencies.
- [ ] Add offscreen tests for capabilities/keyboard actions, double starts,
      pending close, error/retry, failed Save, stale timers/events and malicious
      current-job protocol. Use the same production adapters where practical.

Gate: E09/E12 and all earlier behavior visible through the actual UI. UI text
should describe the user's project/actions, not leak protocol implementation.

## E0.5 Acceptance and handoff

- [ ] Complete all ten demonstration steps in requirements.md. Record the
      desktop/session, actual project/target, tool versions and observed result.
- [ ] Measure noisy-output tests against the named byte/block/backlog bounds;
      record RSS behavior over repeated fixed-size bursts and GUI event-loop
      latency/cancel response on the test host. Initial acceptance target:
      no steadily growing retained output, ordinary UI heartbeat gaps below
      100 ms during a sustained 10 MiB output burst, excluding OS scheduling
      pauses explicitly identified in the evidence. This is a target, not a
      pre-existing benchmark or hard real-time guarantee.
- [ ] Run required pinned Debug/Development warning-clean builds and tests,
      format/tidy, ASan/UBSan build/tests, header/include/dependency gates,
      build-time budget checks and SDK installation/external consumers.
- [ ] Test Editor ON and an independent clean Editor OFF tree; inspect SDK
      exported targets/headers for Qt and Editor leaks. Preserve browser
      validation using its pinned separate toolchain.
- [ ] Write docs/development/editor-workspace.md usage/troubleshooting and
      docs/development/editor-workspace-evidence.md with actual commands,
      pass/fail/skip results, native acceptance and known limits. Keep the
      requirement-to-test mapping and task status current.

Gate: E15. No checked completion box without attached evidence. Offscreen Qt
success does not replace interactive Wayland or installed-SDK acceptance.

## Requirement verification map

| Requirements | Primary evidence |
| --- | --- |
| E01/E02 | E0.0 compile commands, Editor OFF configure/install, dependency inspection |
| E03/E04/E05 | Shared parser fixtures and failure-preserving ProjectStore tests |
| E06/E07 | File API fixtures, target-build sequences and old-binary failure regression |
| E08 | Real argv/cwd-reporting fixture, exact JSON comparison |
| E09 | Transition/capability tests, stale/reentrant/double-start UI tests |
| E10 | Real process-tree/cancel/EOF fixtures plus native Close acceptance |
| E11 | Long-line/invalid-text/slow-reader stress, byte counts and UI latency |
| E12 | Job-details round-trip, command/provenance/omission checks |
| E13 | Actual external sample using an explicitly installed SDK |
| E14 | Stage-specific error and abnormal-supervisor/cleanup evidence |
| E15 | Recorded complete native workflow and repository gates |

## Validation commands

Use existing repository commands and pinned tools, after explicit preparation:

```bash
./scripts/build linux-clang-debug
./scripts/test linux-clang-debug
./scripts/build linux-clang-development
./scripts/test linux-clang-development
./scripts/check linux-clang-development --all
./scripts/build linux-clang-asan-ubsan
./scripts/test linux-clang-asan-ubsan
./scripts/check-build-budget linux-clang-development
./scripts/install-sdk linux-clang-development
out/host-tools/venv/bin/python -m unittest discover -s scripts/python -p 'test_*.py'
```

For Editor-enabled configurations, explicitly configure with
`-DLUDUS_BUILD_EDITOR=ON` using the managed CMake and prepared preset. Add Qt tests
to CTest and use QT_QPA_PLATFORM=offscreen only for display-free tests, not for
the native acceptance application. Preserve test-exception opt-in only on test
targets. Use a separate binary directory with Editor OFF for isolation verification;
record exact configure/build commands so cached ON settings cannot disguise it.
Do not recreate/clean unrelated developer build trees or weaken gates to pass.

For this documentation-only package, validate Markdown links, examples, requirement
coverage, diff whitespace and factual source references. Full implementation
checks above are Kiro's future gates; documentation does not claim they ran.
