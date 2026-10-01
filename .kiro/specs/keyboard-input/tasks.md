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

- [x] Implement owned bounded binding records and stable dense action IDs.
  Validate replacement before modifying active configuration.
- [x] Evaluate button alternatives and Axis1D after each accepted transition;
  cache values and step flags. Exclude baseline-held suppressed keys.
- [x] Implement active-map replacement/reset, old-ID cancellation visibility,
  and explicit map version in published snapshots.
- [x] Demonstrate configurable Jump, MoveX/MoveY, Quit bindings in application
  code; no hard-coded gameplay actions inside the core. (Delivered in M5's
  input demo; the core carries no gameplay defaults.)

Tests (`modules/input/tests/action_tests.cpp`): multi-key OR release behavior;
rapid button tap; two taps ordered; opposite directions/neutral axis;
+1/-1 through-zero edges and steady-hold no-repeat; left/right modifiers bound
separately; shared keys across actions; invalid map transaction preserves old
map (InvalidAction/InvalidKey/ConflictingType/DuplicateBinding/CapacityExceeded);
full capacities; removed-ID cancellation; switch while held; reset plus fresh
press in the same step (Cancelled coexists with Pressed). Focus/overflow
suppression lives in `reducer_tests.cpp`.

Gate: PASS. Action values/flags are cached (O(1) queries; no binding scan or
native call in GetAction); binding evaluation lives entirely in the reducer, so
changing bindings needs no reducer change; old-map events cannot activate the
new map (MapReplaced reset discards old-map pending) and newly bound held keys
are suppressed until release/repress. 32 input test cases / 746 assertions pass
(debug); ASan/UBSan clean; hot-path alloc test clean.

## M4 - Trace, replay fixtures, and bounded performance evidence

Requirements: K02, K03, K07, K12, K13. Prerequisites: M1, M3.

- [x] Implement opt-in bounded trace/counters, source/sequence/step metadata,
  reset reasons, map versions, and separate trace versus gameplay loss counts.
- [x] Reproduce a complete normalized fixture through the production path and
  compare masks/actions/ordering at each step. Fixture includes known initial
  focus/bindings and at least one reset. Incomplete traces reject exact replay.
- [x] Add a debug demo dump through existing logging without logging each key
  by default. No disk writes or formatting inside native callbacks. (The
  diagnostic dump is in M5's input demo via LUDUS_LOG_*.)
- [x] Measure the workloads in design section 8; commit concise metadata,
  results, and reproduction commands. Verify allocation counts over repeated
  hot operations and trace-on/off; exclude OS callback setup from core claims.

Implementation: `InputDebugTrace` (`input_debug.h`/`.cpp`), trace recording in
the reducer (transitions traced at step time with assigned StepId; ignored/
overflow traced at ingest), separate `GetTruncatedCount()` (ring wrap) vs
gameplay-overflow `Overflow` entries. Tests in `trace_tests.cpp`; evidence
harness `benchmarks/input_bench.cpp`; results in
`docs/development/keyboard-input-performance.md`.

Gate: PASS. Complete fixture replays identically across repeated runs (trace
on/off and two trace-on runs match in length and ordering); trace wrap and
gameplay overflow each flip `IsCompleteForReplay()` to false independently;
footprint 15.8 KiB reducer + 16.0 KiB trace = 31.8 KiB (< 128 KiB target);
construction = 1 allocation, hot loop = 0 allocations (bench + alloc-test gate);
no locks. Times reported as planning-grade wall-clock, no latency/determinism
claim.

## M5 - Installed SDK, demonstration, final verification

Requirements: K10-K15. Prerequisites: M1-M4.

- [x] Update tests/sdk_consumer to include public Input headers, link
  Ludus::Input, define a map, ingest a short tap, consume it, and check results.
  No private/native headers or source-tree include paths.
- [x] Extend smoke or add a small input demo according to checked-out conventions.
  Show held movement/action values, tap edges, focus cancellation, and one
  in-memory rebinding example. Respect existing graphics behavior.
- [x] Document manual steps: tap, hold, simultaneous opposites, alternative keys,
  alt-tab while held, regain focus while held, release/repress, rebinding while
  held, close window, and idle rendering. Record compositor/backend versions.
- [x] Complete pinned validation below and re-review diff against AGENTS.md.
  Report unrelated baseline failures separately; never mark them passed.
- [x] Publish actual implementation API/limitations and checked evidence paths;
  update statuses only when gates really pass.

Implementation: `tests/sdk_consumer/{main.cpp,CMakeLists.txt}` (links
`Ludus::Input`, exercises a tap + rebind through installed public headers only);
`apps/input_demo/` (new demo: configurable Jump/MoveX/MoveY/Quit, native pump
loop + synthetic fallback, trace/counter dump); root `CMakeLists.txt`. Manual
matrix recorded in the decision log.

Gate: PASS for every runnable gate (see commands below + ledger). The only
OUTSTANDING item is the real-compositor focused key-delivery manual matrix:
the sandbox has no uinput/virtual input device and weston's headless shell does
not focus a buffer-less surface, so tap/hold/alt-tab-while-held with ACTUAL key
events on a focused native window could not be exercised here. Seat/keyboard
binding, routing wiring, and the pump were validated live against weston 13.0.3;
all keyboard semantics are validated through the production reducer via headless
fixtures and the installed-SDK consumer.

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
| M3 | `modules/input/src/internal/reducer.hpp` (action aggregate eval already present from M1), `modules/input/tests/action_tests.cpp`, `modules/input/CMakeLists.txt` | `./scripts/test linux-clang-debug` input (746 assertions); ASan/UBSan input clean; alloc test clean; `--format` clean; action_tests clang-tidy clean | DONE. Demo in M5 |
| M4 | `modules/input/src/{input_debug.cpp,internal/reducer.hpp}`, `modules/input/tests/trace_tests.cpp`, `modules/input/benchmarks/input_bench.cpp`, `docs/development/keyboard-input-performance.md`, `modules/input/CMakeLists.txt` | `./scripts/test linux-clang-debug` (37 input cases / 833 assertions); ASan/UBSan input clean; `ludus_input_bench` run (1 ctor alloc, 0 hot); `--format` + per-file clang-tidy clean | DONE. Debug dump in M5 |
| M5 | `tests/sdk_consumer/{main.cpp,CMakeLists.txt}`, `apps/input_demo/{main.cpp,CMakeLists.txt}`, root `CMakeLists.txt`, decision log, this ledger | `build/test linux-clang-debug` (26/26), `linux-clang-development` (25/25), `linux-clang-asan-ubsan` (22/22), `--format` clean, `install-sdk linux-clang-development` (consumer prints "Input: tap + rebind OK"), `check-build-budget --profile` (window.h 129ms/2000ms budget), Wayland ON+OFF reconfigure/build/test, warnings-as-errors rebuild of input/platform, live weston 13.0.3 pump (10165 assertions) + live keyboard tests | DONE except the real-compositor focused-key manual matrix (no uinput / no focus-granting shell in sandbox) |
