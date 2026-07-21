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

## One-Command Onboarding

Run:

```bash
./init.sh
```

On Windows, install Python 3.10+ and Visual Studio 2022 Build Tools with the **Desktop development with C++** workload. Double-click `init.cmd`, or run it from any Command Prompt, PowerShell, or Windows Terminal session:

```powershell
.\init.cmd
```

The Windows default is `windows-msvc-development`; debug and release variants are `windows-msvc-debug` and `windows-msvc-release`. The same PowerShell wrappers drive local development and the Windows GitHub Actions job:

```powershell
.\scripts\build.ps1
.\scripts\test.ps1
.\scripts\install-sdk.ps1
```

The project manages CMake, Ninja, and Conan under `out/host-tools/venv`. `init.cmd` finds Visual Studio 2022 through `vswhere` and activates `cl.exe`, the Windows SDK, and the linker environment itself. Its child onboarding process receives that environment; the caller's terminal does not need to be a Visual Studio developer shell.

`init.sh` is safe to rerun. On Ubuntu/Debian hosts it installs missing system prerequisites with `apt-get`, creates or updates the project-managed virtual environment, resolves Conan dependencies, and writes Conan generator files for every Linux preset. The Windows entry point prepares every Windows preset. It does not configure or build Ludus engine targets by default, so engine developers can initialize once and then configure/build from VS Code CMake Tools or the command line when they choose.

The default preset is `linux-clang-development` on Linux and `windows-msvc-development` on Windows. If you want a lighter initialization that prepares only one preset, use:

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

The Linux default preset is:

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
