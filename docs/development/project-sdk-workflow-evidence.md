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

## P2 / P3 — host tools, shared SDK store, portable project CLI

Implemented in the `ludus_tools` package (installable, stdlib-only, Qt-free):

- `templates.py`: versioned bundled `minimal` native template (v1); atomic
  no-replace creation — stage into a sibling dir, reject existing/symlink/
  nonempty destinations, re-check and never overwrite a destination that appears
  between staging and publish, discard staging on cancellation.
- `create.py`: `create_project` ties descriptor + unresolved lock + rendered
  template; `--sdk` records a local prefix ONLY in ignored `.ludus/local.json`
  and keeps the committed lock unresolved (no fabricated hashes). `migrate_v1_to_v2`
  is explicit, preserves relative paths/launch settings, requires an engine
  selection, refuses provider `ludus`, and does a recoverable paired
  descriptor+lock commit (`.migrating` temporaries + `recover_partial_migration`).
- `resolve.py`: precedence = per-op `--sdk` → saved override → locked release in
  the store; prints resolved identity/path/override; unresolved lock without an
  override is an actionable stop (never a fallback engine build); resolved-input
  **stamp** (manifest + exported cmake + lib size/mtime) gates reconfigure/relink
  for mutable local prefixes and aborts if the prefix changes mid-operation.
- `buildlock.py`: the per-build-tree cooperative lock uses the SAME lock path as
  `editor_tool.BuildTreeLock` so CLI and Editor interlock (second = `Busy`).
- `operations.py`: synchronous configure/build/run with exact argv/no-shell,
  CMake File API artifact resolution via the existing `cmake_targets.py`,
  reconfigure-on-stamp-change, and the rule that a failed build refuses to launch
  a stale binary.
- `cli.py` + `__main__.py` + `scripts/ludus`: the `ludus sdk …` / `ludus project
  …` commands with stable exit codes and `--json`. `pyproject.toml` packages a
  redistributable wheel exposing a `ludus` entry point with **no** third-party
  runtime deps.

Checks run here (Python 3.11.15):

```
$ python -m unittest test_ludus_tools test_ludus_project_ops test_ludus_cli   # 59 OK
$ python -m ludus_tools project create /tmp/MyGame --name "My Game" --engine 0.1.0   # creates v2 project
$ # two projects share one installed SDK, each with its own ignored override,
$ #   committed locks unchanged  (test_ludus_cli.test_two_projects_one_sdk)
$ python -m venv /tmp/ludus-venv && /tmp/ludus-venv/bin/pip install .   # install at a NEW location
$ cd /tmp && /tmp/ludus-venv/bin/ludus --store /tmp/s sdk list          # runs with no checkout on path
$ /tmp/ludus-venv/bin/python -c "import ludus_tools,sys; print([m for m in sys.modules if 'PyQt' in m or 'PySide' in m or m=='engine'])"
  []   # no Qt/engine modules loaded — Qt-free, checkout-free (P07/P11)
```

Behaviors proven against a FAKE SDK (a tar of a minimal prefix + valid
manifest), since the real SDK build is UNAVAILABLE: archive traversal/absolute/
symlink rejection, digest mismatch, cancellation, reuse of an identical install,
no-replace creation + destination race, v1→v2 migration + partial-migration
recovery, resolution precedence, flavor-mismatch rejection, stamp-change
detection, and `build` with an unresolved lock exiting UNAVAILABLE rather than
building the engine.

**UNAVAILABLE (needs the pinned native toolchain):** actually configuring/
building/running the generated C++ project (the generated CMake requires 3.29
and Clang 18 + a real SDK), and inspecting its compile database to prove no
engine source compiles. The `operations.py` path that drives this is
implemented and unit-covered up to the `cmake` invocation; the compile itself is
deferred to the reference toolchain.

## P4 — Editor v2 schema sharing (implementation + GUI gap)

Implemented:

- `apps/editor/src/internal/project_descriptor.h`: added `Version`,
  `EngineRequirement`, `TemplateRef`, the `HasEngine`/`HasTemplate` flags and the
  version-2 limit constants (kept equal to `ludus_tools.descriptor`).
- `apps/editor/src/project_store.cpp`: `ParseDescriptor` now accepts version 1
  **and** 2. Version-dependent root keys preserve the old-reader rule (a v1 file
  still rejects v2's extra fields and the Release preset); v2 adds the
  `linux-clang-release` preset, the required-for-cmake / forbidden-for-ludus
  `engine` object (exact non-floating version, bounded unique components/
  features) and the optional `template` object. `SerializeDescriptor` is now
  version-aware and round-trips the v2 fields.
- New shared v2 fixtures `apps/editor/tests/fixtures/cases_v2.json` (+ the six
  `valid_v2_*`/`invalid_v2_*` files) consumed by BOTH readers.
- `apps/editor/tests/project_store_v2_tests.cpp` runs the C++ reader against
  `cases_v2.json` and a v2 serialize round-trip; wired into `ludus_editor_tests`.
- `scripts/python/test_ludus_tools.py` runs the Python v2 reader against the same
  `cases_v2.json`. The legacy v1-only `editor_project.py` and its
  `test_editor_tool.py` fixture checks are untouched, so v1 behavior and the
  "old reader rejects v2" rule are both preserved.

Checks run here:

```
$ python -m unittest test_ludus_tools test_ludus_project_ops test_ludus_cli test_editor_tool   # 95 OK
    # includes DescriptorV1Compat.test_shared_v2_fixture_cases (Python v2 reader vs cases_v2.json)
    # and test_legacy_reader_still_rejects_v2.
```

**UNAVAILABLE (needs Qt6 + Clang 18, neither present):** compiling
`ludus_editor_tests` to execute `project_store_v2_tests.cpp`; and all native Qt
GUI acceptance — the asynchronous New Project / SDK install+select stages, the
engine identity/profile/override controls, Editor↔CLI build-lock contention,
form-retention on failure, and the Editor ON vs independent OFF checks. Per the
handoff, native GUI acceptance **cannot** be replaced by offscreen tests, so it
is explicitly deferred to a reference-toolchain machine. The C++↔Python schema
*sharing* is implemented and cross-checked through the shared fixtures (the C++
half is verified only when the editor is compiled on that machine).

## P2 (catalog) / P5 — release catalog, CI, docs, Sandbox follow-up

Implemented:

- `ludus_tools/catalog.py`: publisher release-catalog model (HTTPS-only URLs,
  per-entry sha256) + bounded streamed download (declared size + hard cap,
  truncation + digest verification) that hands the archive to the strict
  `SdkStore.install_archive`. `ludus sdk install --version … --catalog …` wires
  it in. Remote metadata is fetched only on explicit install. A project
  descriptor can never supply a URL/script.
- `.github/workflows/project-sdk.yml`: candidate-package CI (every push/PR):
  host-tooling unit tests on pinned Python + install-into-fresh-venv + Qt/engine-
  free assertion; then a Development SDK candidate build, **relocation to a fresh
  dir with an empty environment** (`env -i`, no `out/conan`, no checkout on the
  path), external-consumer link against the relocated prefix, and a
  compile-database check that no engine source is compiled. Never publishes.
- `.github/workflows/release.yml`: version-tag assembly/validation for all three
  flavors with tag/version/revision agreement and resolved-identity +
  dependency-inventory assertions; a `publish` job gated to
  `workflow_dispatch` only, `environment: release`, least-privilege
  `contents: write`, and an explicit guard that refuses to publish from a PR. The
  publish step is a documented placeholder — this task must not publish a public
  release.
- `docs/development/project-sdk-workflow.md`: user guide (install, SDK install/
  override/refresh, prerequisites, create, migrate, direct CMake, recovery,
  compatibility limits, Sandbox follow-up).

Checks run here (Python 3.11.15):

```
$ python -m unittest test_ludus_tools test_ludus_project_ops test_ludus_cli   # 65 OK
    # CatalogDownload: HTTPS-only parse, digest mismatch, truncation, oversize,
    # and download->install happy path (injected local opener; no network).
$ python -c "import yaml; [yaml.safe_load(open(f)) for f in (...workflows...)]"  # YAML valid
```

**UNAVAILABLE / not performed (by design or environment):**

- The `project-sdk.yml` / `release.yml` jobs that need the pinned native
  toolchain + Conan + Wayland were authored but not *executed* here (no runner,
  no toolchain). They are standard GitHub-hosted-runner steps intended to run on
  `ubuntu-24.04` with the reference toolchain.
- No release is published, no tag created, no merge performed (task constraint).

### Ludus-Sandbox (P14) — honest status

Ludus-Sandbox is **not** present in this checkout and was **not** inspected or
modified. Its conversion (remove the mandatory nested engine build; pin an engine
release; consume `find_package(Ludus CONFIG REQUIRED)` + the `ludus` CLI) is a
separate change in that repository requiring its own access, implementation and
evidence. This PR validates the standalone reference consumer in this repository
(`tests/sdk_consumer` + the generated minimal template) and records the Sandbox
conversion as an explicit external follow-up. A separate Sandbox PR is NOT a gate
for this Ludus PR, but the pending conversion is clearly recorded here and in
`docs/development/project-sdk-workflow.md`.
