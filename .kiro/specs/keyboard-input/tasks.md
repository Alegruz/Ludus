# Keyboard input implementation milestones

All boxes begin unchecked: this handoff has not implemented or validated input.
Use [requirements.md](requirements.md) and [design.md](design.md) as the contract.
Update this file after each batch with changed paths, commands, results, and
remaining gates. Preserve working code and unrelated specs.

## M0 - Revalidate checkout and freeze the small API

Requirements: K01-K15. Prerequisite: none.

- [x] Read AGENTS.md, applicable steering, ADRs 0003-0005/0007/0008, containers
  and memory docs, and current Platform/pump/SDK/test tooling.
- [x] Compare checked-out Platform with the research baseline. Record actual
  revision and any existing input/browser work; reuse compatible code. Identify
  source collisions before editing; do not overwrite another implementation.
- [x] Finalize key table/explicit values, status enums, public signatures,
  sink lifetime, reset mailbox, map/snapshot versioning, and fixed capacities.
  Illustrate zero-step and three-step loop behavior with a small sequence.
- [x] Define neutral/invalid query results, cancellation for removed action IDs,
  sequence exhaustion, seat selection, and supported evdev mapping assumptions.
- [x] Record material deviations in a decision log with evidence. Keep source
  files compilable; no unrelated systems or dependency migrations.

M0 record: `docs/architecture/keyboard-input-decision-log.md`. Actual `main`
tip is `a66f551` (design baseline `6ebdf7c` treated as historical). The existing
browser canvas input path (`browser::Key`/`InputEvent`/`WindowState`,
`PollBrowserInput`) is preserved untouched; the new backend-independent
`ludus_input` reducer does not compete with it. Frozen API and capacities are in
the decision log.

Gate: reviewed API can express every required truth-table case with no circular
dependency or native public types. Ordinary implementation choices are delegated
to Kiro; M0 is not a mandatory pause for user approval.

## M1 - Backend-independent keyboard reducer

Requirements: K01-K07, K11, K12, K14. Prerequisite: M0.

Affected areas: new modules/input target/public/private files, root CMake,
input tests, header/export checks.

- [x] Implement normalized records, live Down, pending ring, output step view,
  and published keyboard snapshot. All mutation is instance-owned/main-thread.
- [x] Implement increasing-step checks, per-transition edges, repeat/duplicate
  filtering, validated enum access, separate live/simulation masks.
- [x] Implement reset mailbox/epochs, focused baselines, suppression, immediate
  queue invalidation, and safe overflow recovery including the rejected event's
  effect on live Down. Never need queue room to reset.
- [x] Add the production injection path and deterministic Catch2 fixtures.

Tests must include:

| Test | Expected result |
| --- | --- |
| Down, repeated Down, held idle step, Up | One press; held persists; one release |
| Down/Up in one step | Both edge flags, final up |
| Two Down/Up pairs in one step | Four ordered records; masks show both edges |
| Several pumps with zero steps | Pending events retained |
| Three ConsumeStep calls after one batch | Edges only in first, Down retained |
| Query same snapshot repeatedly | Same result; no consumption |
| Duplicate/decreasing step IDs | InvalidStep; pending/snapshot unchanged |
| Unknown and fabricated Key values | Neutral/status; no out-of-bounds access |
| Focus enter with held W | Raw Down; no press; action eligibility suppressed |
| Focus leave with queued tap/held key | Pending activations discarded; reset visible |
| Reset then genuine fresh records before consumption | Baseline first, new records retained in order |
| Tiny capacity overflow where last record is Up | No stuck held/action state; loss status visible |
| Overflow then release/repress while focused | New input recovers without focus cycle |
| Multiple resets before consumption | Latest baseline, retained reason accounting |
| Small test counter near exhaustion | Explicit failure/reset contract, no silent wrap |

All rows implemented in `modules/input/tests/reducer_tests.cpp` (plus
`key_tests.cpp` for enum validation and `allocation_tests.cpp` for the
zero-allocation gate). Tiny-capacity overflow and sequence exhaustion use the
internal capacity-parameterized `Reducer<4,8,16>` / `Reducer<256,8,16,3>`.

Gate: PASS. `ludus_input_tests` + `ludus_input_alloc_tests` pass on
`linux-clang-debug` with no display or Platform linked; `linux-clang-asan-ubsan`
is clean; all five new public headers pass `ludus_header_self_sufficiency`; the
alloc test proves exactly one allocation at construction and zero across 2000
ingest/consume/query iterations.

## M2 - Native Wayland keyboard adapter

Requirements: K01, K03-K07, K10-K12, K14. Prerequisite: M1.

Affected areas: modules/platform registry/listeners/private routing, WindowBase
sink attachment, Wayland teardown and pump error paths, platform tests/CMake.

- [x] Add optional seat/keyboard lifecycle and deterministic single-seat choice;
  negotiate supported versions and register all relevant listeners.
- [x] Normalize keys through a tested private mapping table. Implement focus
  enter baseline, leave/reset, sink attach/detach, and surface routing.
- [x] Close keymap fds on all paths, ignore gameplay repeats safely, and handle
  modifiers/repeat_info without adding text/IME, XKB, or a timer.
- [x] Reset on close, keyboard capability removal, seat global removal, and
  terminal display failure. Keep windows usable when there is no keyboard.
- [x] Preserve prepare/read/cancel pairing, zero-timeout poll, EINTR/EAGAIN, and
  existing HandleEvent({}) and headless return semantics.
- [x] Exercise normalization/lifecycle using backend seams independent of a
  compositor; retain existing Wayland event tests. Use a real compositor for
  attachment, focus, routing, and idle-pump validation. (See gate note.)

Implementation: `modules/platform/src/window_wayland.cpp` (seat/keyboard
listeners, surface routing, enter/leave baselines, terminal-reset-on-close),
`src/internal/evdev_keymap.hpp` (tested evdev->Key table, no +8 offset),
`include/ludus/platform/keyboard_sink.h` (noexcept fn-ptr sink seam),
`WindowBase::AttachKeyboardSink`/`DetachKeyboardSink`. The pump body was
extracted to `pumpDisplayOnce()`; `HandleEvent({})` behavior and headless return
semantics are unchanged.

Gate: PARTIAL. No regressions (26/26, 2 compositor tests skipped without a
display). Normalization + sink seam covered by `ludus_platform_keyboard_tests`.
Live validation against a real **weston 13.0.3** headless compositor passed for
seat selection, keyboard-capability binding, listener registration, and a stable
idle pump (`ludus_platform_wayland_tests`: 10440 assertions;
`ludus_platform_live_keyboard_tests`: pass), clean under ASan/UBSan.
OUTSTANDING: full focused-window key delivery and held-state-on-leave against a
compositor could NOT be exercised here — the sandbox has no uinput/virtual input
device and weston's headless shell does not grant keyboard focus to the
buffer-less test surface. This remains a manual gate (recorded in M5).

## M3 - Button/axis actions and rebinding

Requirements: K06-K09, K12, K14. Prerequisite: M1; M2 is needed for demo proof.

- [ ] Implement owned bounded binding records and stable dense action IDs.
  Validate replacement before modifying active configuration.
- [ ] Evaluate button alternatives and Axis1D after each accepted transition;
  cache values and step flags. Exclude baseline-held suppressed keys.
- [ ] Implement active-map replacement/reset, old-ID cancellation visibility,
  and explicit map version in published snapshots.
- [ ] Demonstrate configurable Jump, MoveX/MoveY, Quit bindings in application
  code; no hard-coded gameplay actions inside the core.

Tests: multi-key OR release behavior; rapid button tap; two taps ordered;
opposite directions/neutral axis; +1/-1 changes; left/right modifiers bound
separately; shared keys across actions; no modifiers/chords inferred; invalid
map transaction preserves old map; full capacities; removed-ID cancellation;
switch while held; switch with queued presses; reset plus fresh press in the
same step; focus/overflow suppression. Axis edge expectations must match design.

Gate: gameplay queries never scan bindings or call native APIs; changing
bindings does not require reducer changes; old-map events cannot activate the
new map and newly bound held keys cannot activate until release/repress.

## M4 - Trace, replay fixtures, and bounded performance evidence

Requirements: K02, K03, K07, K12, K13. Prerequisites: M1, M3.

- [ ] Implement opt-in bounded trace/counters, source/sequence/step metadata,
  reset reasons, map versions, and separate trace versus gameplay loss counts.
- [ ] Reproduce a complete normalized fixture through the production path and
  compare masks/actions/ordering at each step. Fixture includes known initial
  focus/bindings and at least one reset. Incomplete traces reject exact replay.
- [ ] Add a debug demo dump through existing logging without logging each key
  by default. No disk writes or formatting inside native callbacks.
- [ ] Measure the workloads in design section 8; commit concise metadata,
  results, and reproduction commands. Verify allocation counts over repeated
  hot operations and trace-on/off; exclude OS callback setup from core claims.

Gate: complete fixture produces identical semantic results on repeated runs;
trace wrap/loss is visible; runtime storage/work is bounded; zero hot-path
allocations and locks are demonstrated. Report measured times without claiming
keyboard hardware latency or whole-engine determinism.

## M5 - Installed SDK, demonstration, final verification

Requirements: K10-K15. Prerequisites: M1-M4.

- [ ] Update tests/sdk_consumer to include public Input headers, link
  Ludus::Input, define a map, ingest a short tap, consume it, and check results.
  No private/native headers or source-tree include paths.
- [ ] Extend smoke or add a small input demo according to checked-out conventions.
  Show held movement/action values, tap edges, focus cancellation, and one
  in-memory rebinding example. Respect existing graphics behavior.
- [ ] Document manual steps: tap, hold, simultaneous opposites, alternative keys,
  alt-tab while held, regain focus while held, release/repress, rebinding while
  held, close window, and idle rendering. Record compositor/backend versions.
- [ ] Complete pinned validation below and re-review diff against AGENTS.md.
  Report unrelated baseline failures separately; never mark them passed.
- [ ] Publish actual implementation API/limitations and checked evidence paths;
  update statuses only when gates really pass.

```bash
./scripts/build linux-clang-debug
./scripts/test linux-clang-debug
./scripts/check linux-clang-development --all
./scripts/build linux-clang-asan-ubsan
./scripts/test linux-clang-asan-ubsan
./scripts/install-sdk linux-clang-development
CMAKE_BUILD_PARALLEL_LEVEL=2 ./scripts/check-build-budget --profile
```

Also configure/build/test with Wayland explicitly OFF and with Wayland ON using
the repository's supported configure interface. Confirm its exact flags first;
do not assume changing a CMake option rebuilds the selected preset automatically.
Run warning-as-error configuration, header-self-sufficiency/foundational gates,
and existing Wayland pump regressions. A skipped compositor test is not native
backend verification. Run init.sh only if the pinned tools/dependencies need it.

## Evidence ledger (fill during implementation)

| Milestone | Revision / paths | Checks actually run | Result / remaining gate |
| --- | --- | --- | --- |
| M0 | branch `feat/keyboard-input` from `a66f551`; `docs/architecture/keyboard-input-decision-log.md` | Baseline `./scripts/build` + `./scripts/test linux-clang-debug` (22/22) before edits | DONE. Toolchain provisioned (clang-18, cmake 3.29.6, conan 2.8.1, Wayland dev); API/capacities frozen |
| M1 | `modules/input/**` (5 public headers, `src/{key,keyboard,input_debug}.cpp`, `src/internal/{reducer.hpp,log_categories.h}`, 3 tests); `modules/input/CMakeLists.txt`; root `CMakeLists.txt` | `./scripts/build/test linux-clang-debug` (26/26 incl. input + alloc); `linux-clang-asan-ubsan` input tests clean; `./scripts/check --format` clean; `ludus_header_self_sufficiency`, `ludus_foundational_includes` pass; clang-tidy run manually per-file (clean) | DONE. Remaining gate: project `./scripts/check --tidy` cannot run in this sandbox (pre-existing clang-tidy vs Ninja C++-modules `@modmap` interaction, fails first on unmodified `apps/smoke/main.cpp`); verified my files tidy-clean out-of-band |
| M2 | `modules/platform/src/window_wayland.cpp`, `src/internal/evdev_keymap.hpp`, `include/ludus/platform/keyboard_sink.h`, `include/ludus/platform/base/window.h`, `modules/platform/CMakeLists.txt`, 2 new tests | `./scripts/build/test linux-clang-debug` (26/26); live `weston 13.0.3` headless run of `ludus_platform_wayland_tests` (10440 assertions) + `ludus_platform_live_keyboard_tests` (debug & ASan/UBSan, clean); `--format` clean; header self-sufficiency pass; per-file clang-tidy clean | DONE w/ outstanding manual gate: focused key delivery + held-loss-on-leave need a compositor granting focus + a virtual input device (absent here) |
| M3 | Pending | None | Not implemented |
| M4 | Pending | None | Not implemented |
| M5 | Pending | None | Not implemented |
