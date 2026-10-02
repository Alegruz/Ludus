# Project SDK Workflow — Implementation Evidence Ledger

This ledger records the *actual* commands, results, environment facts and
limitations observed while implementing `.kiro/specs/project-sdk-workflow`. It is
the honest record required by requirement **P15** and the per-phase evidence rule
in `tasks.md`. Where a required gate could not be executed in this environment it
is recorded as **UNAVAILABLE** with the reason, never as passed.

## Starting point

- Starting revision: `6065bf505cfdb9ed619db30586da1b71031135e8`
  (`docs: design independent game projects and shared SDK workflow`).
- Implementation branch: `codex/project-sdk-workflow` (created from `main`).
- Working tree at branch creation: clean (`git status --short` empty); no unrelated
  local changes to preserve beyond the committed design package.

## Environment / toolchain audit (P0)

The repository's **pinned reference toolchain** is Clang/LLVM 18 + LLD 18 on
Ubuntu 24.04, with managed CMake 3.29.6, Ninja 1.11.1.3 and Conan 2.8.1
(`config/tool_versions.json`), plus the pinned Slang 2026.1.2 / spirv-val shader
tools and (for the optional Editor) Qt6.

Actual tools present in this sandbox (`<tool> --version`):

| Tool | Required (pinned) | Present here | Usable for gates? |
| --- | --- | --- | --- |
| clang / clang++ | 18.x + LLD 18 | 15.0.7 (`clang version 15.0.7`) | No — wrong major, no LLD 18 |
| cmake | 3.29.6 managed | 3.22.2 system | No — below `cmake_minimum_required(3.29)` |
| ninja | 1.11.1.3 | 1.10.2 | Partial |
| conan | 2.8.1 | **not installed** | No |
| clang-format | 18.0.0 | 23.1.1 | No — pinned 18 required; newer reports non-18 checks |
| clang-tidy | 18.0.0 | (LLVM, non-18) | No |
| python3 | >= 3.10 | 3.9 default; **3.11.15 available via pyenv** | Yes (use 3.11.15) |
| Qt6 | required for Editor | not installed | No |
| Slang / spirv-val | pinned 2026.1.2 | not installed | No |
| git / gh | — | 2.50.1 / 2.95.0 | Yes |

Consequences for acceptance (recorded honestly, per P15):

- **UNAVAILABLE in this sandbox:** native engine build; full SDK build + install +
  relocation; external native consumer link; ASan/UBSan build+tests; pinned
  clang-format-18 / clang-tidy-18 gates; build-time budget (needs the pinned
  compiler); native Qt Editor GUI acceptance (New Project / Open / build / run /
  stop); browser/Emscripten and RAD regressions. These require the pinned
  toolchain, Conan network access to a working package set, a GPU/display, and Qt.
  They are deferred to a machine with the reference toolchain and are listed in
  the "Remaining gaps" section; the PR is therefore a **draft**.
- **AVAILABLE here:** the Python host-tooling layer (SDK store, archive
  validation, descriptor/lock v2 schema, templates, resolution, CLI) runs on
  Python 3.11.15 and is covered by `unittest` suites executed below; CMake export
  metadata review by inspection; schema/fixture consistency between C++ and
  Python by shared fixtures.

Per AGENTS.md Rule 0, these are reported as unavailable rather than satisfied
from a version string. "Never claim binary compatibility from a version string
alone" (handoff) is honored: SDK identity is derived from actual build inputs and
these gates stay open until run on the reference toolchain.

### Baseline test state (before implementation)

Pinned-toolchain gates could not be run (see table). The existing Python
`unittest` suites were run with the available 3.11.15 interpreter:

```
$ PY=/root/.pyenv/versions/3.11.15/bin/python3
$ cd scripts/python && $PY -m unittest test_editor_tool    # Ran 35 tests OK
$ $PY -m unittest test_formatting                          # Ran 13 tests OK (skipped=13, clang-format-18 absent)
$ $PY -m unittest test_rad_debugger                        # Ran 22 tests OK
$ $PY -m unittest test_web_package                         # Ran 5 tests OK
```

So the Python baseline is green; `test_formatting` skips because pinned
clang-format-18 is absent (expected, pre-existing).

## Compatibility / identity model (P03/P11)

SDK identity is derived from the actual CMake build inputs, not a version string:

- engine `version` + `source_revision` (git revision of the producer tree),
- target triple (OS/arch), compiler id + version, C++ runtime/ABI tag and distro
  baseline captured from the build environment,
- `flavor` (Debug/Development/Profile/Release) and the existing
  `LUDUS_SDK_VARIANT` compatibility key (assert policy version, dialogs, asan,
  ubsan, tsan),
- sorted `features`, assertion `policy` block, package `format`, and the
  per-module/target + dependency inventory with their redistributable notices,
- payload `digest` (SHA256) stored in the release catalog / lock / sidecar, never
  recursively inside the bytes being hashed.

Compatibility is checked **before** configure; mismatched flavor/toolchain/policy
is rejected with expected-vs-actual detail and never falls back to building the
engine.
