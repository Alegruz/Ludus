# Ludus Editor workspace (E0) — implementation evidence ledger

This ledger records the **actual** state of the E0 implementation: what was
implemented, which gates ran and passed, and which gates are **blocked by the
build environment** and therefore remain incomplete. Per `tasks.md`, no
completion box is checked without attached evidence, and offscreen/unit success
never substitutes for interactive Wayland or installed-SDK acceptance.

## Starting revision

- Branch: `main`, baseline commit `3ac97af` ("docs(editor): specify first
  workspace milestone and Kiro handoff"). Working tree was clean at start
  (`git status --porcelain` empty).

## Build environment reality (material deviation from the design baseline)

`scripts/python/engine.py doctor` on this host reports:

| Tool | Required | Found | Satisfies |
| --- | --- | --- | --- |
| Python | ≥ 3.10.0 | 3.9.25 (system) | no (pyenv has 3.10–3.14 available) |
| Clang | ≥ 18.0.0 | 15.0.7 | **no** |
| LLD | ≥ 18.0.0 | 15.0.7 | **no** |
| clang-tidy | ≥ 18.0.0 | reports non-18 | **no** |
| Project-managed CMake/Ninja/Conan | pinned | missing | **no** |
| Project-managed venv | present | missing | **no** |

Additional facts:

- **Qt 6 is not installed** and the host is Amazon Linux 2023 (`dnf`, fedora-like),
  not the Ubuntu/apt host the design assumed (which listed Qt base 6.4.2 as an
  apt candidate). `find_package(Qt6 6.4 ...)` cannot succeed here.
- **No Wayland/X display** (`WAYLAND_DISPLAY` and `DISPLAY` are empty); this is a
  headless sandbox. Native GUI/runtime acceptance is impossible here.
- `init.sh` / bootstrap cannot complete: it needs network Conan plus pinned
  Clang 18 and the managed venv, none of which are available.

**Consequence.** The C++/CMake/Qt build gates (E0.0 build proof) and the native
Wayland GUI + runtime acceptance (E0.5) **cannot be executed in this
environment** and are recorded below as BLOCKED, not passed. The Python tooling
layer (adapter, protocol, process ownership, File API extraction) and all shared
descriptor fixtures **are** fully implemented and verified here, because they run
on the available Python interpreter. This matches the spec's instruction to
"report an unavailable environment rather than declaring the spike complete."

## What was implemented

| Area | Files |
| --- | --- |
| CMake option + gating | `CMakeLists.txt` (`LUDUS_BUILD_EDITOR` OFF, native-only `add_subdirectory`), `apps/editor/CMakeLists.txt` (local Qt discovery, host gating, AUTOMOC per target, `QT_NO_KEYWORDS`, test-exceptions only on tests, no install/export) |
| State + model (C++) | `apps/editor/src/internal/{project_descriptor,workspace,project_store,log_buffer,tool_process,controller,main_window}.h`, `apps/editor/src/{workspace,project_store,log_buffer,tool_process,controller,main_window}.cpp`, `apps/editor/main.cpp` |
| Offscreen C++ tests | `apps/editor/tests/{workspace,project_store,log_buffer,tool_process,controller}_tests.cpp` |
| Shared File API logic | `scripts/python/cmake_targets.py` (extracted from RAD; RAD delegates) |
| Descriptor validation | `scripts/python/editor_project.py` (shares fixtures with C++) |
| Tooling adapter | `scripts/python/editor_tool.py` (stdio protocol, selectors supervisor, owned process groups, non-reaping cleanup, bounded output/credit) |
| Adapter tests | `scripts/python/test_editor_tool.py` (descriptor + File API + real-process/protocol) |
| Launcher | `scripts/editor`, `scripts/python/editor_launch.py` |
| Examples | `examples/editor-workspace/ludus.project.json`, `examples/editor-sdk-project/{CMakeLists.txt,CMakePresets.json,ludus.project.json,main.cpp}` |
| Shared fixtures | `apps/editor/tests/fixtures/*.json` + `cases.json` |
| Docs | `docs/development/editor-workspace.md`, this ledger |
| Ignore rule | `.gitignore` adds `.ludus/` |

## Gates — actual results on this host

Commands were run from the repository root unless noted.

### PASS

- **Python tooling test suite** (system Python 3.9.25):
  `python3 -m unittest discover -s scripts/python -p 'test_*.py'`
  → `Ran 65 tests ... OK (skipped=13)`. The 13 skips are the pre-existing
  `test_web_package` tests that require Node; they are unrelated to the editor.
  The baseline before this change was 40 tests; the editor adds 25 and the RAD
  suite remains 22, with **no regressions**.
- **RAD behavior preserved after File API extraction**:
  `python3 -m unittest test_rad_debugger` → `Ran 22 tests ... OK`. RAD now
  delegates its codemodel query/resolution to `cmake_targets.py` while keeping
  its historical client name, error strings and laxer behavior.
- **Shared descriptor fixtures** validate identically to the recorded verdicts:
  `test_editor_tool.DescriptorFixtureTests` (5 tests) — valid Ludus/cmake
  descriptors (including empty/quoted/Unicode/shell-looking args), and rejected
  bad-version/unknown-field/absolute-path/bad-target/invalid-UTF-8/NUL cases.
- **Shared File API logic** (`FileApiTests`, 7 tests): executable-only listing,
  custom artifact paths, library/ambiguous/missing rejection, wrong codemodel
  version, multi-config rejection, source/build identity mismatch, reference
  traversal rejection, oversized-file bound.
- **Real-process / protocol behavior** (`RealProcessTests`, 13 tests, real OS
  pipes and real child processes — not mocked Popen):
  - configure discovers targets and succeeds; digest mismatch → `Conflict`;
  - build_run preserves the **exact** argument list and cwd (empty, `a b`,
    `日本語`, `$(echo hi)`, `"quoted"`, `--flag`) observed through a real child;
  - a failed build launches **no** runtime even with a stale successful binary
    present (`BuildFailed`, no `runtime_started`);
  - cancel cleans up an **ordinary grandchild** (verified the grandchild PID is
    dead after cancel), `cleanup_confirmed=true`, exit 130;
  - cancel accepted before spawn prevents the launch;
  - stdin EOF (parent loss) cancels;
  - nonzero runtime exit → `RuntimeFailed` with the exit code; a runtime signal
    → `RuntimeSignaled` with the signal number;
  - no-newline (300 KiB) output stays bounded per frame; invalid UTF-8 output is
    replaced; a protocol version mismatch and a future/non-boundary credit
    offset are `ProtocolError` (exit 2).
  The suite also runs clean under `-W error::ResourceWarning` (no fd/process
  leaks).
- **Python compile** of all `scripts/python/*.py` (`py_compile`) and import under
  the managed-equivalent Python 3.10.20 both succeed.

### BLOCKED (environment-limited; remain incomplete)

These require the reference toolchain / Qt / a Wayland desktop, none of which
exist on this host. They are **not** claimed as passing.

| Gate | Why blocked |
| --- | --- |
| E0.0 Qt dependency spike: minimal window under Clang 18, warnings-as-errors, `-fno-exceptions` | No Clang 18 and no Qt 6; `find_package(Qt6 6.4)` cannot resolve. |
| `./scripts/build linux-clang-debug` / `-development` (Editor ON and a separate Editor-OFF tree) | No managed CMake/Ninja/Conan, no Clang 18, bootstrap cannot complete. |
| `./scripts/test linux-clang-debug` / `-development` (incl. `ludus_editor_tests`, offscreen) | Depends on a successful Editor-ON build. |
| `./scripts/check --all` (clang-format/clang-tidy 18) on the new C++ | Pinned clang-format/clang-tidy 18 unavailable (host has non-18). |
| `./scripts/build/test linux-clang-asan-ubsan` | Same toolchain/bootstrap blockers. |
| `./scripts/check-build-budget` | Needs a configured build. |
| `./scripts/install-sdk` + external `editor-sdk-project` consumer | Needs a full build + install; also confirms no Qt/Editor leak in the SDK. |
| Native Wayland GUI + separate-process runtime acceptance (all 10 demo steps) | Headless sandbox, no compositor/GPU. Offscreen tests do not substitute. |
| Noisy-output RSS / UI heartbeat latency measurement (E0.5 targets) | Needs the built GUI running natively. |

## Design conformance notes and deliberate choices

- **No-exceptions**: all C++ app/engine code is exception-free and compiled with
  the inherited `-fno-exceptions` via `ludus_apply_project_defaults`;
  `ludus_enable_test_exceptions` is applied **only** to `ludus_editor_tests`.
  Errors are returned as status enums / result structs / `std::optional`.
- **Optional/isolated**: Qt is discovered only inside `apps/editor` and only when
  `LUDUS_BUILD_EDITOR=ON`; there is no `install()`/export of any editor target or
  header, no Qt in Conan/init/SDK, and the browser build path is untouched.
- **Shared, not duplicated, File API logic**: `cmake_targets.py` is the single
  implementation; RAD delegates to it with `require_version=False`/`bounded=
  False` and its own messages so RAD's 22 tests pass unchanged.
- **One asynchronous Python tooling operation**: `editor_tool.py` is a
  one-operation `--stdio` process using a selectors loop, `start_new_session`
  process groups, `waitid(WNOWAIT)` non-reaping observation, TERM→KILL deadlines,
  and `/proc`-based descendant inspection — no worker threads, no
  `communicate()` full buffers, no `startDetached`, no shell concatenation.
- **Bounded protocol**: fixed 256 KiB output-credit window, 1 MiB aggregate
  control cap with a 4 KiB terminal-result reserve, 256 KiB line cap, 2 MiB
  framing backlog cap, 1 MiB retained log with visible truncation markers.
- **Codemodel-driven artifacts**: the executable is always resolved from the
  CMake File API codemodel (named client `client-ludus-editor`), with
  source/build identity checks and a post-build re-resolution; the artifact path
  is never guessed and a failed/cancelled build never launches an old binary.

## Requirement → evidence map (current state)

| Requirement | Evidence | Status here |
| --- | --- | --- |
| E01 (optional/OFF, no Qt when OFF) | CMake gating authored; OFF path never enters `apps/editor` | Code complete; **configure/install proof BLOCKED** |
| E02 (Qt 6.4 Widgets, no exceptions, warnings-as-errors) | App authored to contract | Code complete; **compile proof BLOCKED** |
| E03/E04/E05 (descriptor parse/validate/save) | `project_store*.cpp` + `editor_project.py` + shared fixtures; Python `DescriptorFixtureTests` PASS; C++ `project_store_tests.cpp` authored | Python PASS; **C++ run BLOCKED** |
| E06/E07 (configure/File API/build/no-stale-launch) | `cmake_targets.py` + adapter; `FileApiTests` + `RealProcessTests` PASS | PASS (real processes) |
| E08 (exact argv/cwd, no shell) | `RealProcessTests.test_build_run_preserves_exact_arguments_and_cwd` PASS | PASS |
| E09 (one operation, stale/reentrant) | `workspace_tests.cpp` + `controller_tests.cpp` authored; transition logic | **C++ run BLOCKED**; logic verified by review and by adapter tests |
| E10 (async, Stop, deadlines, EOF, CleanupUnknown) | `RealProcessTests` cancel/EOF/grandchild PASS | PASS (adapter); **native Close BLOCKED** |
| E11 (bounded output) | `log_buffer_tests.cpp` authored; `RealProcessTests` no-newline/invalid-UTF-8 PASS | PASS (adapter); **C++ log test run BLOCKED** |
| E12 (Copy Job Details, no telemetry) | `controller_tests.cpp` authored | **C++ run BLOCKED**; implemented in `controller.cpp` |
| E13 (engine + external SDK samples) | `examples/*` authored | **install-sdk consumer run BLOCKED** |
| E14 (distinguishable errors) | result-code vocabulary + adapter error paths; `RealProcessTests` PASS | PASS (adapter) |
| E15 (all gates) | this ledger | Partial: Python gates PASS, native/C++ gates BLOCKED |

## Task status

Because the native/C++ gates cannot run here, no E0 task box is marked complete
in `tasks.md`. The Python-side acceptance (E0.2/E0.3 behavior, shared fixtures,
RAD preservation) is demonstrably passing; the C++/Qt/native portions are
implemented and awaiting a reference-toolchain host to execute their gates.

## How to finish acceptance on a reference host

1. `./init.sh` (pinned Clang 18 / LLD / managed CMake/Ninja/Conan / venv);
   install Qt 6 (`qt6-base-dev qt6-wayland` or distro equivalent).
2. `cmake --preset linux-clang-development -DLUDUS_BUILD_EDITOR=ON` then
   `cmake --build --preset linux-clang-development --target ludus_editor`.
3. `ctest` the `editor` label with `QT_QPA_PLATFORM=offscreen` for the C++ unit
   tests; run `./scripts/check linux-clang-development --all`,
   `./scripts/build/test linux-clang-asan-ubsan`, `./scripts/check-build-budget`.
4. Configure a **separate** Editor-OFF binary dir and `./scripts/install-sdk`;
   confirm no Qt/Editor target or header appears in the install.
5. On a real Wayland desktop with a usable GPU, run `./scripts/editor` and
   complete the ten demonstration steps in `requirements.md`; record the
   session, versions and observed results, and the noisy-output RSS / UI latency
   measurements.
