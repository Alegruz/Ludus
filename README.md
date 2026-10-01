# Ludus

Ludus is currently a minimal C++23 engine skeleton for rendering research and engine architecture experiments. Milestone 0 intentionally contains infrastructure only: one small foundation library, unit tests, a smoke executable, SDK installation, and an external `find_package()` consumer.

## Supported Host

Milestone 0 is Linux-first and validated for Ubuntu 24.04 with Clang/LLVM 18, LLD, Ninja, CMake, Conan 2, Python 3, clang-format, and clang-tidy.

The onboarding script installs Ubuntu system prerequisites with `apt-get` when they are missing. On minimal Ubuntu installs it may enable the standard `universe` component because LLVM and Python venv support are often published there. If Ubuntu's helper reports success without changing the deb822 source file, Ludus patches the Ubuntu source component list after keeping a one-time backup. It then installs pinned project-managed CMake, Ninja, and Conan into `out/host-tools/venv/`.

The venv is only for project-managed build tools. Ludus still uses the system Python executable to create it, but avoids installing Python packages globally or depending on whatever CMake, Ninja, or Conan version happens to be on the machine.

## Quick Start

```bash
git clone <repository>
cd <repository>
./init.sh
```

That one command prepares the development environment for all committed presets: system prerequisites, project-managed CMake/Ninja/Conan, and Conan dependency files. It does not configure or build Ludus engine targets by default. Use the VS Code CMake extension or the command-line scripts when you want to configure/build.

Conan may still build missing third-party packages such as Catch2 while preparing the dependency cache. That is dependency setup, not a Ludus engine target build. If an existing CMake cache points at stale tool paths, init may fresh-configure that generated build tree to repair it, but it still does not compile or link Ludus targets.

VS Code is configured to use the project-managed CMake at `out/host-tools/venv/bin/cmake`. If VS Code was already open while `./init.sh` ran, reload the window before pressing the CMake Tools Build button.

Run `./init.sh --validate` when you want the full build/test/check/sanitizer/SDK-consumer validation pass.

RAD Debugger is available as optional Linux x64 tooling. Use
`./init.sh --with-rad-debugger` or `./scripts/setup-rad-debugger` to build the
pinned debugger locally, then `./scripts/debug linux-clang-debug ludus_smoke`.
VS Code provides **Ludus: Debug target with RAD** through Tasks: Run Task;
onboarding preserves F5 and existing debugger preferences. See
[the debugging guide](docs/development/debugging.md) for usage and Linux alpha
limitations, and [the tooling design](docs/architecture/developer-tools.md) for
future editor and scripting milestones.

## Presets

Available committed CMake presets:

```text
linux-clang-debug
linux-clang-development
linux-clang-asan-ubsan
linux-clang-release
```

`linux-clang-development` is the default local preset.

## Common Commands

```bash
./init.sh
./init.sh --validate
./scripts/bootstrap
./scripts/doctor
./scripts/build linux-clang-debug
./scripts/test linux-clang-debug
./scripts/check linux-clang-development --all
./scripts/build linux-clang-asan-ubsan
./scripts/test linux-clang-asan-ubsan
./scripts/install-sdk linux-clang-development
```

Formatting checks are read-only by default. Use `./scripts/check --format --fix` to apply
clang-format 18 and the Ludus designated-initializer convention: compact lists
may stay on one line (`value = { .A = 0, .B = 1 };`); multiline lists put their
opening brace on its own line, with one member per line and a trailing comma.

Local `./init.sh` enables automatic formatting of staged C/C++ before each commit.
Existing checkouts can enable it with `./scripts/install-hooks`. The hook preserves
partial staging: it formats the staged snapshot without including unstaged edits.
Existing custom hooks are preserved; chain `.githooks/pre-commit` from your hook
when using your own hook setup. CI verifies formatting using the same formatter.

## SDK Install

`./scripts/install-sdk` builds the selected preset, installs the SDK to `out/install/<preset>/`, configures `tests/sdk_consumer/` as a separate CMake project, builds it against the install prefix with `find_package(Ludus CONFIG REQUIRED)`, and runs it.

## Current Limitations

Milestone 0 does not include rendering, Vulkan, windowing, input, assets, jobs, ECS, reflection, serialization, plugins, an editor, or game code. Windows launchers exist only as thin future-facing wrappers around the shared Python entry point; the supported automatic onboarding host is Ubuntu Linux.

## More Detail

See [docs/development/building.md](docs/development/building.md) and [docs/architecture/milestone-0.md](docs/architecture/milestone-0.md).
