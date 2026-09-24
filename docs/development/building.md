# Building Ludus

## Trees

The source tree contains only hand-written project files. Generated state belongs under `out/`:

```text
out/host-tools/venv/        pinned CMake, Ninja, and Conan Python packages
out/conan/home/             project-local Conan cache and installed profile
out/conan/<preset>/         Conan CMakeToolchain and CMakeDeps files
out/build/<preset>/         CMake build trees and compile_commands.json
out/install/<preset>/       installed SDK prefixes
out/test-results/           reserved for local test output
out/logs/                   reserved for local logs
```

The install tree is treated as the SDK boundary. The external consumer test uses `find_package()` against `out/install/<preset>/` rather than `add_subdirectory()` on the engine.

## Build flavor and assertion policy

Every preset now supplies an explicit `LUDUS_BUILD_FLAVOR`. Debug uses CMake
Debug; Development and Profile both use RelWithDebInfo; Release uses Release
(or MinSizeRel for a manual build). Direct CMake invocations must supply the
matching flavor. Profile is its own engine flavor even though its optimization
configuration matches Development. Assertion policy never follows `NDEBUG`.

Debug/Development enable `LUDUS_ASSERT` and Check inspection breaks. Profile and
Release compile Assert away and disable Check breaks. Require/Check/Fatal stay
active everywhere. `REQUIRE`/`FATAL` always terminate, including after a debugger
continuation; `CHECK` returns its boolean. Enabled `ASSERT`/`ASSERT_F` is now
**resumable through explicit developer action** in non-CI runs — a debugger
Continue (Debug or Development) or, with no debugger in a non-CI Debug build, the
external helper's Continue-once dialog — otherwise it terminates. This is gated
by the generated `LUDUS_ASSERT_DIALOGS_AVAILABLE` (1 only for non-CI Debug) plus
a runtime CI veto; the assertion-policy version is 2. See
[assertions.md §5.1](../architecture/assertions.md) and
[ADR 0006](../decisions/0006-resumable-development-assertions.md).

The installed `assert_config.hpp` and SDK manifest carry the built variant's
policy. A Release consumer of a Development SDK uses Development's assertion
policy. Do not override the generated macros or mix headers/libraries from
different variants. Use separate prefixes. Installation refuses an incompatible
or legacy unversioned Ludus prefix before overwriting files; move an old generated
`out/install/<preset>` aside, then rerun `scripts/install-sdk`. Multi-config SDK
generation is explicitly unsupported until per-configuration packages exist.

### Diagnostic helper and report delivery

The external diagnostic helper is a Python development tool,
`tools/diagnostics/ludus_diagnostic_helper.py` (installed to the SDK `bin`
directory as `ludus_diagnostic_helper`). It launches an engine binary with the
diagnostic channels wired up so assertion/`FATAL`/`CHECK` reports are captured
independently of normal Logging. Run an engine binary under it with:

```bash
python3 tools/diagnostics/ludus_diagnostic_helper.py -- <engine-binary> [args...]
# or, from an installed SDK:
ludus_diagnostic_helper -- <engine-binary> [args...]
```

The engine calls `ludus::diagnostics::InitializeDiagnosticSession()` (from
`ludus/diagnostics/session.hpp`, in the `Ludus::DiagnosticsIntegration` target,
which depends on FoundationBase but is not part of it) once at the top of `main`,
before workers or the logger. It reads these descriptors/policy from the
environment:

- `LUDUS_DIAGNOSTIC_REPORT_FD` — connected `AF_UNIX`/`SOCK_DGRAM` report socket.
- `LUDUS_DIAGNOSTIC_CONTROL_FD` — connected `AF_UNIX`/`SOCK_SEQPACKET` control
  socket (versioned Hello/HelloAck handshake).
- `LUDUS_DIAGNOSTIC_INTERACTIVE=0` — force report-only (local headless Debug).

The helper sets the first two for its child; a directly launched binary with no
helper simply runs report-only with no transport. Interactive presentation is
eligible only in a local, non-CI Debug build with a live terminal/display; CI is
auto-detected and forced report-only, and CI workflows additionally set
`LUDUS_DIAGNOSTIC_INTERACTIVE=0` explicitly. This layer changes no assertion's
fatal action.

To run Release policy tests without changing the production preset:

```bash
out/host-tools/venv/bin/cmake --preset linux-clang-release -B out/build/linux-clang-release-assert-tests -DLUDUS_BUILD_TESTS=ON -DLUDUS_WARNINGS_AS_ERRORS=ON
out/host-tools/venv/bin/cmake --build out/build/linux-clang-release-assert-tests
out/host-tools/venv/bin/ctest --test-dir out/build/linux-clang-release-assert-tests --output-on-failure
```

## One-Command Onboarding

Run:

```bash
./init.sh
```

`init.sh` is safe to rerun. On Ubuntu/Debian hosts it installs missing system prerequisites with `apt-get`, creates or updates the project-managed virtual environment, resolves Conan dependencies, and writes Conan generator files for every committed preset. It does not configure or build Ludus engine targets by default, so engine developers can initialize once and then configure/build from VS Code CMake Tools or the command line when they choose.

The default preset for command-line build, test, check, and install scripts is `linux-clang-development`. If you want a lighter initialization that prepares only one preset, use:

```bash
./init.sh --preset-only linux-clang-debug
```

`--all-presets` is accepted for clarity, but it is already the default initialization scope.

Run the full validation path explicitly when you want it:

```bash
./init.sh --validate
```

Validation builds the development preset, runs tests, runs checks, builds/runs the ASan/UBSan preset, installs the SDK, and runs the external SDK consumer.

During initialization, Conan may build missing third-party packages such as Catch2 while populating `out/conan/home/`. That is dependency-cache preparation, not a Ludus engine target build. If an existing CMake cache was created with older tool paths or a broken generator path, init fresh-configures that generated build tree with the current preset to repair it. This still does not compile or link Ludus targets.

Automatic system installation may ask for your sudo password once. It does not run destructive Git commands and it keeps generated project state under `out/`.

On minimal Ubuntu installations, the standard `universe` package component may be disabled. Ludus enables it automatically when apt cannot find packages such as `clang-18`, `lld-18`, `clang-format-18`, `clang-tidy-18`, or Python venv support. If Ubuntu's helper reports success without updating `/etc/apt/sources.list.d/ubuntu.sources`, Ludus patches that deb822 source file directly and keeps a one-time `.ludus-backup` copy beside it.

The project-managed venv is deliberately scoped to build tools. The bootstrap process still starts from the system Python you already have, but CMake, Ninja, and Conan are Python-distributed tools with project-pinned versions. Keeping them in `out/host-tools/venv/` avoids `sudo pip`, avoids modifying the user's global Python environment, and makes every developer and CI job use the same tool versions. When `config/tool_versions.json` changes, rerunning `./init.sh` updates those pinned tools in place while the executable paths remain stable. The venv mostly stores those tool packages; it is generated state and can be removed safely.

LLVM 18 is the Milestone 0 floor because the project builds as C++23 and the supported Ubuntu 24.04 standard library needs a recent Clang frontend. Older Clang binaries can appear present while still failing on ordinary C++23 library headers.

`init.sh --no-system-install` keeps package-manager changes disabled and only uses already-installed tools. With `--validate`, `--skip-checks`, `--skip-sanitizers`, and `--skip-sdk` are available when a developer wants a faster partial validation pass.

## Bootstrap

Run:

```bash
./scripts/bootstrap
```

Bootstrap is the exhaustive dependency-preparation step. It uses the system Python to create `out/host-tools/venv/`, then installs the pinned CMake, Ninja, and Conan versions from `config/tool_versions.json`. This is intentional network access.

After tool validation, bootstrap installs the committed Conan profile into the project-local Conan home, creates `conan.lock`, populates the Conan cache, writes Conan generator files under `out/conan/<preset>/`, and runs a minimal CMake configure smoke test.

## Conan

Conan 2 is used only before CMake configure. CMake does not invoke Conan, `FetchContent`, or any network fetch. Catch2 v3 is the only third-party C++ dependency in Milestone 0 and is declared in `conanfile.py` with an exact version.

Normal configure, build, test, check, and SDK install commands use the Conan files already generated by bootstrap. They should not require network access unless generated state is removed.

## Presets

The default preset is:

```bash
./scripts/build linux-clang-development
```

Other presets:

```bash
./scripts/build linux-clang-debug
./scripts/build linux-clang-asan-ubsan
./scripts/build linux-clang-release
```

Development and sanitizer presets use `RelWithDebInfo` with project target options that keep assertions enabled. Release disables tests by default and keeps normal symbols.

## VS Code

The shared workspace settings make CMake Tools use presets and point it at Ludus' project-managed CMake:

```text
out/host-tools/venv/bin/cmake
```

Run `./init.sh` before pressing the CMake Tools Build button. If the extension still reports `/usr/bin/cmake` or an older CMake version, reload the VS Code window so it picks up `.vscode/settings.json` and the freshly installed managed tools.

## Checks

Run both formatting and targeted clang-tidy:

```bash
./scripts/check linux-clang-development --all
```

Run only formatting:

```bash
./scripts/check linux-clang-development --format
```

Apply formatting:

```bash
./scripts/check linux-clang-development --format --fix
```

Run only clang-tidy:

```bash
./scripts/check linux-clang-development --tidy
```

clang-tidy uses the selected preset's `compile_commands.json` and analyzes project sources under `modules/` and `apps/`, not generated, installed, or third-party code.

## Cleaning Generated State

It is safe to remove generated state under `out/`:

```bash
rm -rf out/build
rm -rf out/install
rm -rf out/conan/linux-clang-development
```

If you remove `out/conan/` or `out/host-tools/`, rerun:

```bash
./init.sh
```

Do not remove source files to clean builds.

## Diagnosing Toolchains

Run:

```bash
./scripts/doctor
```

Doctor reports required and optional tools, versions, paths, and whether each satisfies the project requirements. It exits nonzero only when a required item is missing or invalid.
