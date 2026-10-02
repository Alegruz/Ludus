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

## P1 — relocatable runtime SDK (implementation + what remains)

Implemented (reviewable by inspection + the Python checks noted):

- `cmake/LudusSdkManifest.json.in` extended to **schema 2**: adds source
  revision, target triple/os/arch, compiler id/version, `cxx_runtime_abi`,
  `distro_baseline`, build type, sanitizer block, a nested `assert_policy`
  block, sorted `features`, `components`, a redistributable `dependencies`
  inventory (name/version/licenses/kind) and `system_prerequisites`. The legacy
  flat `enable_asserts`/`break_on_check`/`assert_policy_version`/
  `assert_dialogs_available`/`build_flavor_id`/`sdk_variant` fields are retained
  so `engine.verify_sdk_install` and `cmake/CheckSdkVariant.cmake.in` keep
  working unchanged.
- `cmake/EngineSdkIdentity.cmake` (new): derives triple / C++ runtime ABI tag /
  distro baseline from the actual build inputs and accumulates the component,
  feature, dependency and system-prerequisite inventory via GLOBAL properties,
  serialized to the manifest `@…_JSON@` placeholders by
  `ludus_sdk_finalize_identity()`.
- Root `CMakeLists.txt`: computes compiler/source identity at root scope,
  registers the audited link-closure dependencies (volk bundled; FreeType +
  HarfBuzz bundled, consumed PRIVATE by `Ludus::Text` but still in the final
  consumer's static link line; Threads system), the system prerequisites
  (Vulkan loader, pthreads, Wayland when enabled, host Slang/spirv-val) and the
  components, then installs `THIRD_PARTY_NOTICES.md` and a stable package-local
  dependency search directory `lib/cmake/Ludus/dependencies`.
- `cmake/LudusConfig.cmake.in`: prepends the bundled dependency directory to
  `CMAKE_PREFIX_PATH` *locally* (restored at the end so global consumer search
  state is never mutated), resolves volk/FreeType/HarfBuzz/Threads from the
  bundled metadata when present, exports the full identity as `Ludus_*` vars, and
  adds a public `ludus_apply_app_policy()` helper (C++23 + no-exceptions) for
  templates so they never reference the checkout-only
  `ludus_apply_project_defaults`.
- `cmake/sdk/THIRD_PARTY_NOTICES.md` (new): records bundled vs system
  dependencies and their obligations; the manifest `dependencies` array is the
  machine-readable twin.
- `scripts/python/engine.py` `verify_sdk_install`: now also requires the notices
  file, audits ALL installed `lib/cmake/Ludus/**.cmake` for producer-path
  leakage (not just the Ludus package files), and asserts the manifest identity
  fields are resolved (no `@…@`/`unknown`) with a non-empty dependency inventory.

Checks run here (Python 3.11.15):

```
$ cmake -P (EngineSdkIdentity triple/abi/distro + accumulators)   # triple/abi derive OK;
    # GLOBAL-property accumulation is NOT scriptable under `cmake -P` (CMake limitation),
    # so the accumulator path is exercised only in a real configure (UNAVAILABLE here).
$ python -m unittest test_ludus_tools  # 33 OK — includes ManifestTemplate rendering the
    # extended template to valid JSON, parsing it into SdkIdentity, and asserting the
    # legacy flat fields remain.
$ python -m unittest test_editor_tool  # 35 OK — engine.py edits did not regress E0.
```

**UNAVAILABLE (requires the pinned toolchain + Conan, deferred):** the actual
`cmake --install` of the extended manifest; building Debug/Development/Release
candidate archives with hashes; populating `lib/cmake/Ludus/dependencies` with
the real bundled volk/FreeType/HarfBuzz CMake metadata + static libs; extracting
at a fresh prefix with no producer checkout/Conan cache and configuring/building/
running an external consumer that links every public module. These are the P1
*relocation* gate and remain open; the manifest/export/config *plumbing* and the
Python-side store/identity validation that consume them are implemented and
tested.
