# Ludus

Ludus is a C++23 engine and toolset in active development, with modular runtime
systems, installed SDK/CLI workflows and an optional native editor.

Start with [Ludus Wiki](https://alegruz.github.io/Ludus/) for task guides,
explanations and reference reading paths. Its
[Markdown sources and complete documentation index](docs/README.md) are also
available in this checkout for offline reading in any text/Markdown editor.
The wiki renders these same files; see [offline browser reading and packaging](docs/development/wiki.md#offline-reading).

## macOS support

The first macOS slice uses upstream Clang 18 and libc++ with Apple silicon
and Intel profiles, targeting macOS 14 or later. Install Xcode Command Line Tools and
Homebrew's `llvm@18`, then run:

```bash
./init.sh --cli --preset macos-clang-development --preset-only --locked --no-system-install
./scripts/build macos-clang-development
```

The init launcher selects a Python meeting the recorded minimum before opening
the setup selector; it reuses the prepared interpreter on macOS when available.
For graphical setup with Homebrew Python 3.12, install `python-tk@3.12`.
Apple's system Tk 8.5 is unsupported. Missing or old Tk falls back to terminal
setup; use `--cli` to select terminal setup explicitly. `--gui` instead reports
the missing GUI prerequisite without starting installation.

Debug, Development, Profile, Release, and ASan/UBSan configure/build/test presets
are available under `macos-clang-*`. Setup selects the matching architecture's
Conan profile and keeps managed tools/dependencies under ignored `out/` paths.
Use `--with-tests` during setup to prepare native test dependencies.
Refreshing the Conan lock preserves pins needed by other supported hosts.

`--preset-only` prepares dependencies for just the selected preset. Before
selecting another profile in VS Code, prepare it too. For Debug with tests:

```bash
./init.sh --cli --preset macos-clang-debug --preset-only --locked --with-tests --no-system-install
```

Omit `--preset-only` to prepare all native profiles in one setup run.
If the selected preset's generated toolchain is missing, CMake stops before
compiler detection and prints the init command needed to prepare that profile.

Debug, Development, and ASan/UBSan builds are validated on Apple silicon.
Native Cocoa integration tests require a WindowServer session and opt in with
`LUDUS_TEST_COCOA=1`; deferred feature cases explicitly skip. On the validation
host running macOS 26.6, Clang 18's ASan runtime deadlocks during initialization,
before `main`, including in an independent probe. Sanitizer execution remains
unverified on that host. The macOS 14 CI job runs the Cocoa lifecycle and keyboard
tests, Metal lifecycle/rendering tests, native filesystem tests and Content read adapters with ASan/UBSan
as well as the regular Development build.

Platform now provides [native Cocoa windows and keyboard input](docs/architecture/macos-platform.md).
It publishes borrowed native handles and backing-pixel dimensions for the Metal backend. RHI supports Cocoa presentation and headless rendering,
including frame clearing, fullscreen pipelines, MSL shaders and uniform uploads.
See the [Metal rendering guide](docs/development/fullscreen-rendering.md#macos-metal). FoundationFilesystem provides [native regular-file reads](docs/architecture/filesystem.md),
including pinned roots, revision clones and independent offset reads. Content
read adapters use this backend. Vulkan/Volk is excluded on macOS. Content persistence, audio stream workers, audio device output, interactive diagnostic helpers, the Qt editor,
Linux debugger journeys, the world demo (libc++ 18 lacks floating-point
`from_chars`), and release packaging are deferred. Portable modules
still compile; the smoke app can present through Metal. Linux and browser
backends retain their existing implementations.

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

On an interactive desktop, that command opens a graphical setup window. Choose
Engine contributor (all native presets), Application developer (one native
preset), Browser developer (one Emscripten preset), or Full validation, then
review the preset and optional tools before pressing **Initialize**. Closing or
cancelling the window runs no setup actions. Setup progress and any sudo password
prompt appear in the launching terminal after the window closes.

The default Engine contributor workflow prepares system prerequisites,
project-managed CMake/Ninja/Conan, and Conan dependency files for all native
presets. It does not configure or build Ludus engine targets by default. Use the
VS Code CMake extension or the command-line scripts when you want to
configure/build. Browser setup installs its separate pinned toolchain and
configures the selected browser preset.

Use `--cli` for terminal setup. CI, redirected input/output, and headless Linux
sessions automatically use the CLI. The GUI uses Python's optional Tkinter
module (`sudo apt-get install python3-tk` on Ubuntu); if Tk or a desktop display
is unavailable, default setup falls back to the CLI. `--gui` requires a window
and reports an error before setup if it cannot open one. Both interfaces share
the same setup code and flags; these options also work through `scripts/init`
and the PowerShell wrappers (the supported native host remains Ubuntu).

```bash
./init.sh --cli                                      # existing terminal workflow
./init.sh --cli --persona application                # one native development preset
./init.sh --cli --persona browser                    # browser development tools
./init.sh --cli --persona validation                 # complete native validation
./init.sh --gui --with-rad-debugger                   # review optional debugger setup
./init.sh --cli --with-editor                        # Qt prerequisites + native editor build
./init.sh --cli --no-editor                          # omit optional editor setup (default)
```

`--persona` supplies defaults; an explicit preset and scope flags override those
defaults. Full validation prepares all native presets even when `--preset-only`
is supplied. Tests are excluded by default: `--with-tests` enables native test
targets and Catch2 dependencies for later builds, while `--run-tests` also builds
and executes them during setup. `--validate`, the Full validation workflow, and
`--ci` explicitly opt into tests as part of validation. They reject `--no-tests`.

| Workflow | Tests | Sample applications | Browser probes | Editor / RAD |
| --- | --- | --- | --- | --- |
| Engine contributor | Off | On | Off | Off |
| Application developer | Off | Off | Off | Off |
| Browser developer | Off | On | Off | Off |
| Full validation | On, executed | On | Off | Off |

Use `--with-smoke-app` / `--no-smoke-app` to select the smoke app and native input
demo, and `--with-web-probes` / `--no-web-probes` for browser feasibility probes.
`--with-shader-probe` / `--no-shader-probe` controls the isolated native shader
probe (off by default); it requires its separate pinned shader tools. Required engine
modules and the native diagnostics helper remain available for applications and
the editor. Browser presets reject native tests, RAD, editor setup,
`--all-presets`, `--validate`, and `--ci`.

```bash
./init.sh --cli --persona application                # engine libraries, no samples/tests/editor
./init.sh --cli --with-tests --preset-only            # include tests in future builds; do not execute
./init.sh --cli --run-tests --preset-only             # build and run tests now
./init.sh --cli --persona browser --with-web-probes   # opt into browser probes
```

Selections are saved locally under `out/init/options.json` for the prepared
presets and apply to subsequent CLI, direct CMake preset, and VS Code builds.
Reinitialize to change them. `--no-editor` also disables the editor for those
presets. Custom CMake users can bypass saved choices with
`-DLUDUS_USE_INIT_OPTIONS=OFF` and set their own target options. Existing checkouts
without saved choices retain the committed developer preset settings.

The optional **Build Ludus editor** checkbox is off by default. Selecting it
installs `qt6-base-dev` and `qt6-wayland` on Ubuntu/Debian when missing, enables
`LUDUS_BUILD_EDITOR`, and builds `ludus_editor` for the selected Debug or
Development preset. `--no-system-install` uses your existing Qt 6.4+ installation
instead. Launch it afterwards with `./scripts/editor --preset <preset>`.
Qt remains an optional editor dependency outside Conan and the installed SDK.
See [the editor guide](docs/development/editor-workspace.md).

Conan may still build missing third-party packages while preparing the dependency cache; Catch2 is included only when tests are enabled. That is dependency setup, not a Ludus engine target build. If an existing CMake cache points at stale tool paths, init may fresh-configure that generated build tree to repair it, but it still does not compile or link Ludus targets.

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

The native reference environment is Ubuntu 24.04 on Linux x64. The editor is
optional; focused document editing/undo, general scene authoring and embedded
play remain later milestones. Browser runtime work has its own pinned toolchain
and device acceptance. Windows launchers remain future-facing wrappers; they do
not establish Windows native onboarding support. See the wiki's
[capability guide](docs/wiki/getting-started/status.md) and subsystem evidence
for implemented paths and their limits.

## More Detail

See [docs/development/building.md](docs/development/building.md) and [docs/architecture/milestone-0.md](docs/architecture/milestone-0.md).

The [game world architecture](docs/architecture/game-world.md) covers level data,
entity storage and updates. `Ludus::GameplayWorld` implements its identity/storage
baseline; the optional [reference game](apps/world_demo/README.md) demonstrates
editable levels, fixed ticks, transactional loading and native/browser rendering.

The [CPU fluid field](docs/architecture/fluid-field.md) is exported as
`Ludus::PhysicsFluid` for native and browser SDK consumers. It supplies bounded
shallow-water stepping, solid masks, momentum strokes, pressure forcing and
surface/velocity samples; Sandbox owns the sea sources and gameplay adapter.

The [engine configuration system](docs/architecture/engine-configuration.md)
provides portable typed layers, prepared transactions, source explanations and
sparse preferences. GameHost loads explicit cooked bundles, and the optional
Editor shares its schema in the Configuration workspace.
