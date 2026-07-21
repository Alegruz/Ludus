# Ludus

Ludus is currently a minimal C++23 engine skeleton for rendering research and engine architecture experiments. Milestone 0 intentionally contains infrastructure only: one small foundation library, unit tests, a smoke executable, SDK installation, and an external `find_package()` consumer.

## Supported Hosts

Milestone 0 is validated on Ubuntu 24.04 with Clang/LLVM 18 and on Windows with Visual Studio 2022 Build Tools (MSVC). Both hosts use project-pinned Ninja, CMake, and Conan 2 plus Python 3.

The onboarding script installs Ubuntu system prerequisites with `apt-get` when they are missing. On minimal Ubuntu installs it may enable the standard `universe` component because LLVM and Python venv support are often published there. If Ubuntu's helper reports success without changing the deb822 source file, Ludus patches the Ubuntu source component list after keeping a one-time backup. It then installs pinned project-managed CMake, Ninja, and Conan into `out/host-tools/venv/`.

The venv is only for project-managed build tools. Ludus still uses the system Python executable to create it, but avoids installing Python packages globally or depending on whatever CMake, Ninja, or Conan version happens to be on the machine.

On Windows, install Python 3.10+ and Visual Studio 2022 Build Tools with the **Desktop development with C++** workload. Then double-click `init.cmd`, or run one command from Command Prompt, PowerShell, or Windows Terminal:

```powershell
git clone <repository>
cd <repository>
.\init.cmd
.\scripts\build.ps1
.\scripts\test.ps1
```

`init.cmd` locates Visual Studio 2022 and activates its x64 compiler environment automatically, so Developer PowerShell and PowerShell execution-policy changes are not required. It gives actionable installation guidance when Python or the C++ workload is missing, and installs the pinned Python build tools into `out/`; Visual Studio and Python remain system prerequisites.

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

## Presets

Available committed CMake presets:

```text
linux-clang-debug
linux-clang-development
linux-clang-asan-ubsan
linux-clang-release
windows-msvc-debug
windows-msvc-development
windows-msvc-release
```

`linux-clang-development` is the Linux default. On Windows, `windows-msvc-development` is selected by default.

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

Formatting checks are read-only by default. Use `./scripts/check --format --fix` to apply clang-format.

## SDK Install

`./scripts/install-sdk` builds the selected preset, installs the SDK to `out/install/<preset>/`, configures `tests/sdk_consumer/` as a separate CMake project, builds it against the install prefix with `find_package(Ludus CONFIG REQUIRED)`, and runs it.

## Current Limitations

Milestone 0 does not include rendering, Vulkan, windowing, input, assets, jobs, ECS, reflection, serialization, plugins, an editor, or game code.

## More Detail

See [docs/development/building.md](docs/development/building.md) and [docs/architecture/milestone-0.md](docs/architecture/milestone-0.md).
