# Project live editing implementation tasks

All boxes start unchecked. Read [requirements](requirements.md), [design](design.md)
and the [handoff](../../../docs/architecture/project-live-reload-kiro-handoff.md).
One phase may contain several focused commits; one implementation PR can deliver
all phases. Update evidence before checking a task. Do not count mocks, skipped
checks or optionalized failures as real acceptance.

## L0 ABI and two-generation feasibility

- [ ] Inspect actual main, E0/RAD/tooling/SDK/resource code and concurrent SDK PR.
      Record dependencies and current supported capabilities; do not duplicate
      pending project-sdk-workflow implementation or silently depend on it.
- [ ] Implement small GameApi tables/handwritten fixture and CMake helpers.
      Prove Clang 18/no-exceptions warnings, public header budgets/self-sufficiency,
      explicit allocation/error paths and module/host dependency closure.
- [ ] Load A and B concurrently from distinct immutable paths; verify hidden
      symbols, instances/destructors use their own code, matching symbols and
      wrong/missing entry/table/SDK/sanitizer rejection. Audit dlopen constructors,
      static/TLS/third-party assumptions. Failure stops the dependent phase.

## L1 Host lifecycle, play and restart

- [ ] Add Qt-free static GameHost library, project-owned host and static shipping
      dispatch using the same game implementation. Build an external installed-SDK
      sample with a visible frame/tuning effect, not a log-only rendering claim.
- [ ] Implement bounded host protocol and persistent play supervisor with owned
      pipes/socketpair, exact argv/cwd, Hello/ready, Pause/Step/Resume/Stop/Status.
- [ ] Add editor session state and optional play sidecar. Exercise open-without-
      code, failed open, dirty documents, stop-before-spawn, duplicate start,
      crash/hang, EOF, supervisor loss, descendant cleanup, close/project switch
      and stale epoch events. Preserve legacy executable Run and debugger paths.

## L2 Build during play and immutable generations

- [ ] Extend canonical File API resolver for MODULE_LIBRARY and host artifacts;
      split new-path build locks/session leases without removing E0 protection.
- [ ] Implement complete manifest/symbol/hash publication and lease-aware cleanup.
      Build while A runs; failed/cancelled builds cannot activate old outputs.
- [ ] Implement manual build/reload then opt-in debounce/coalescing. Test source
      edits during build, CMake regeneration, two editors/CLI lock contention,
      half-written output, target/SDK changes, newest-result ordering and Stop
      racing publication. Record the limits of source-input checking.

## L3 State-preserving code replacement

- [ ] Implement Quiesce/Snapshot/Stage/Commit/Retire with explicit context modes,
      resource leases/change sets, callback counts and cooperative limitations.
- [ ] Implement bounded checkpoint/schema migration fixture: function-body edit
      preserves position/score/RNG/time; add/remove/rename/type changes have
      explicit migrations or RestartRequired. Preserve paused state.
- [ ] Inject errors at every pre-commit stage and assert old instance/checkpoint/
      resource equality and continued play. Inject native candidate crash/hang
      separately and prove editor/document survival plus explicit recovery.
- [ ] Inject late callbacks, outstanding job/resource user, destruction failure,
      failed loader release/resident images and Stop during reload. Never unmap
      referenced code; disable reload rather than grow a generation chain.

## L4 Editing during play

- [ ] Add copied schema/property reads, small standard Qt inspector controls and
      atomic conditional multi-property edits with expected revisions.
- [ ] Add small versioned tuning documents, atomic save/conflicts, document undo/
      redo, session-only edit default and explicit Apply to Document.
- [ ] Exercise stale objects, simulation edits, old schema, out-of-range/NaN/
      over-limit strings, rejected batch without partial mutation, duplicate/lost
      reply reconciliation, undo conflict, drag coalescing and code reload while
      drafts/selection/history exist. Stop never persists simulation by surprise.

## L5 Supported assets and failure diagnostics

- [ ] Implement one real supported asset/configuration replacement with current
      public resource APIs, immutable input/validation, safe swap and deferred
      destruction. Test invalid import and in-flight users retain the old asset.
      Full importer suites wait for their own APIs; show unsupported kinds.
- [ ] Add Copy Session Details, phase/error/status display, bounded records and
      backpressure. Test slow/non-reading clients, malformed/chunked protocol,
      duplicate payload mismatch, full result ledger and Stop priority.

## L6 Acceptance, integration and PR delivery

- [ ] Run native sample journey: open A, play, live edit/undo, edit/build/reload
      code, migrate/reject layout changes, replace supported asset, stop, reopen
      A, switch to B, close with work in flight; observe real window behavior.
- [ ] Run 100 successful/rejected reloads under ASan/UBSan; record live instances,
      callbacks, leases, handles, checkpoint size, loader residency and RSS trend.
      Distinguish sanitizer cache behavior from real leaks and retain raw results.
- [ ] Record debugger-launched/attach sessions, startup and new-generation source
      breakpoints, locals/stacks, pause/step/continue, safe reload, destructor crash
      recovery. Use an available supported debugger; do not call untested RAD
      compatibility validated. Preserve manual Restart if symbol reload is limited.
- [ ] Run pinned Debug and Development warning-clean builds/tests, ASan/UBSan,
      `scripts/check --format` and Development `--tidy`, header self-sufficiency,
      foundational includes, build budget, installed SDK/external consumers and
      Editor ON/OFF. Enable test targets explicitly under current optional-test
      presets. Run relevant E0/RAD/Python/browser packaging regressions.
- [ ] Package/relocate host/API SDK artifacts with the existing SDK workflow;
      reuse its shared backend if merged. Prove module rebuild compiles no engine
      source, shipping static mode runs and runtime exports have no Qt.
- [ ] Measure cold/warm build, publish, property latency and reload pause p50/p95
      on a recorded host. Report target misses and pending hardware acceptance.
- [ ] Write `docs/development/project-live-reload.md` and
      `docs/development/project-live-reload-evidence.md` with actual usage,
      recovery, statuses, limitations and evidence for all L01-L16.
- [ ] Commit/push `codex/project-live-reload`, publish one implementation PR to
      main, attach evidence and keep it draft until required acceptance passes.
      Fetch current main, resolve conflicts, repair real CI failures and repeat
      affected tests. Verify exact final-head checks, hard-gate conclusions and
      mergeability; do not merge/tag/release or modify other PRs without approval.

## Evidence ledger template

| Phase/requirement | Revision and command | Result | Evidence/blocker |
| --- | --- | --- | --- |
| L0 / L04 | Pending | Not run | Implementation required |

Use PASS/FAIL/SKIP/UNAVAILABLE explicitly. A successful workflow containing a
failed continue-on-error step is not PASS for that requirement.
