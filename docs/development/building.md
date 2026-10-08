# Building Ludus

Build this checkout to develop the engine or produce an installed SDK. For a
game project, use [the independent-project workflow](project-sdk-workflow.md).
All commands below run from the Ludus repository root.

## Initialization and macOS SDK compatibility

Run `./init.sh` before configuring the engine with a Ludus preset. Native
preset toolchains require the current initialization record; an existing Conan
file alone does not prove setup completed. A plain configure without a toolchain
reports the init command. Advanced builds may provide a caller-owned toolchain;
they still receive the feature dependency checks. Browser setup retains its
separate Emscripten toolchain checks. Configuration never runs init for you.

On macOS, init compiles and links a small probe using the actual Clang compiler,
libc++ headers and SDK before installing managed tools or building Conan
packages. It checks `NAN` and `INFINITY`, whose header definitions HarfBuzz needs.
Clang 18 with the macOS 27 SDK fails this check; macOS 26.5 passes on the tested
host. Without `SDKROOT`, init tests Clang's default SDK first, then installed
sibling SDKs newest first, selecting the first that passes. It does not download
an older SDK or change the system's Xcode selection.

An explicit `SDKROOT` is validated and never silently replaced. If it fails,
install/select a compatible SDK, then rerun init, for example:

```bash
SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk ./init.sh
```

If `SDKROOT` was set accidentally, use `unset SDKROOT` and rerun `./init.sh`.
The path above must exist locally. Lowering the deployment target is insufficient:
it changes the minimum supported OS, not the SDK's headers. Existing valid local
SDK/header links are preserved when validation fails.

Init records the real compiler/SDK/header destinations and metadata. Moving or
retargeting them invalidates setup and refreshes native CMake caches during
explicit repair. Conan dependencies receive the selected SDK, compiler paths and
matching libc++ include flags too; cached dependency packages cannot substitute
for the compiler/SDK probe. CMake independently checks the math-header contract
so an overridden SDK fails during configure with recovery instructions.

## Missing or stale dependencies

CMake configuration checks the dependencies required by enabled features before
creating build rules. A **Ludus setup problem** reports the affected feature,
missing tool/file or package setting, and a **How to fix** action. Run that action
explicitly, then use **CMake: Configure** in VS Code (or rerun your configure
command) before building. Configure and build do not download or repair tools.

- Missing generated native/browser toolchain: run the preset-specific `init.sh`
  command printed by the error.
- Removed compiler or Ninja executable: restore it or update the named CMake
  setting; rerun `./init.sh` for project-managed tools. If changing compilers,
  configure a fresh build directory rather than reusing an incompatible cache.
- Missing Catch2, volk, FreeType or HarfBuzz package metadata: prepare the selected preset with
  `init.sh`; tests need `--with-tests`. Custom builds can set the package's `_DIR`
  setting or `CMAKE_PREFIX_PATH` to an existing compatible installation.
- Missing shader compiler/validator: run `./scripts/shader-probe bootstrap` from
  the Ludus source repository. Browser GLSL ES also needs
  `./scripts/bootstrap-spirv-cross`. If tools moved, update
  `LUDUS_SLANG_COMPILER`, `LUDUS_SPIRV_VALIDATOR`, or `LUDUS_SPIRV_CROSS`.
  macOS Metal requires only Slang. Installed SDK consumers use the same checks;
  bootstrap commands belong to the source repository, not the game directory.
- Unusable Python: install Python 3.10+ and set `Python3_EXECUTABLE` if needed.
- Missing editor Qt: use opted-in native editor setup or follow the
  [browser editor guide](browser-editor.md); an existing installation can be
  selected with `Qt6_DIR`.
- Missing Luau source profile: run `./scripts/luau-probe bootstrap` explicitly.
  Behavior cooking reports missing paired SDK host tools and inputs with their
  declaration names; restore the matching SDK or correct those arguments.
- Missing shader/reflection inputs: restore the named project file or fix the
  source/schema/baseline declaration. Missing installed generator files require
  restoring or reinstalling the SDK.

Optional features do not require their tools when disabled. Existing but
unrunnable Slang/validator executables report their probe failure, including
shared-library or permission problems. These checks catch common setup failures;
package-internal errors and dependencies removed after configuration can still
produce underlying CMake/build-tool diagnostics. They are not a full integrity
or tool-version audit; build-time shader checks retain that responsibility.

## One-command onboarding


```bash
git clone https://github.com/Alegruz/Ludus.git
cd Ludus
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
and the PowerShell wrappers. Those wrappers do not establish a native Windows
port; supported native hosts and limits are described below.

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
See [the editor guide](editor-workspace.md).

Conan may still build missing third-party packages while preparing the dependency cache; Catch2 is included only when tests are enabled. That is dependency setup, not a Ludus engine target build. If an existing CMake cache points at stale tool paths, init may fresh-configure that generated build tree to repair it, but it still does not compile or link Ludus targets.

VS Code is configured to use the project-managed CMake at `out/host-tools/venv/bin/cmake`. If VS Code was already open while `./init.sh` ran, reload the window before pressing the CMake Tools Build button.

Run `./init.sh --validate` when you want the full build/test/check/sanitizer/SDK-consumer validation pass.

RAD Debugger is available as optional Linux x64 tooling. Use
`./init.sh --with-rad-debugger` or `./scripts/setup-rad-debugger` to build the
pinned debugger locally, then `./scripts/debug linux-clang-debug ludus_smoke`.
VS Code provides **Ludus: Debug target with RAD** through Tasks: Run Task;
onboarding preserves F5 and existing debugger preferences. See
[the debugging guide](debugging.md) for usage and Linux alpha
limitations, and [the tooling design](../architecture/developer-tools.md) for
future editor and scripting milestones.

## macOS support


The first macOS slice uses upstream Clang 18 and libc++ with Apple silicon
and Intel profiles, targeting macOS 14 or later. Install Xcode Command Line Tools and
Homebrew's `llvm@18`, then run:

```bash
./init.sh --cli macos-clang-development --preset-only --locked --no-system-install
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
./init.sh --cli macos-clang-debug --preset-only --locked --with-tests --no-system-install
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

Platform now provides [native Cocoa windows and keyboard input](../architecture/macos-platform.md).
It publishes borrowed native handles and backing-pixel dimensions for the Metal backend. RHI supports Cocoa presentation and headless rendering,
including frame clearing, fullscreen pipelines, MSL shaders and uniform uploads.
See the [Metal rendering guide](fullscreen-rendering.md#macos-metal). FoundationFilesystem provides [native regular-file reads](../architecture/filesystem.md),
including pinned roots, revision clones and independent offset reads. Content
read adapters use this backend. Vulkan/Volk is excluded on macOS. Content supports [atomic native saves](../architecture/content-resources.md#native-saves).
Audio stream/device output, interactive diagnostic helpers, the
[native Qt Editor](editor-workspace.md), and the
[world demo](../examples/world-demo.md) support macOS. Game
[release packaging](game-packaging-publishing.md#macos-app-packages) creates
ad-hoc signed app bundles. Live Play/RAD debugging and Developer ID signing/
notarization remain deferred. The smoke app can present through Metal. Linux and browser
backends retain their existing implementations.


## Headless and sandboxed builds

Prepare only the preset you need, with tests explicitly enabled:

```bash
./init.sh --cli linux-clang-development --preset-only --locked --with-tests
./scripts/build linux-clang-development
./scripts/test linux-clang-development
```

Use `--no-system-install` when system prerequisites are already available.
User-owned LLVM 18 installations work when their `bin` directory is on `PATH`
and their shared libraries are discoverable by the host loader. Setup resolves
versioned executables such as `clang++-18` and creates project-local tool shims.
It validates the host toolchain before creating the build-tool venv or downloading
Python packages. `--no-system-install` still downloads the pinned managed tools
and Conan dependencies; `--locked` preserves the committed dependency lock.
Conan resolution, downloads and builds stream their output to the terminal.

Outbound HTTPS access is needed during dependency setup. Builds and tests use
the populated local cache. A headless build needs no Wayland packages; CMake
selects the headless backend automatically when the client headers, protocols
and scanner are unavailable. Inspect the configure output for the selected
backend. Live compositor/GPU tests remain opt-in and skip when their prerequisites
are absent; passing the portable tests does not validate hardware rendering.

Some diagnostic and GameHost tests, and the installed SDK consumer, exercise
Unix domain socket pairs (`SOCK_DGRAM` and `SOCK_SEQPACKET`), socket options and
child processes. Sandboxes may restrict these even though no external service
is contacted. `PermissionError: Operation not permitted` from socket operations,
unavailable diagnostic transports or a host exiting before its protocol handshake
can indicate a sandbox restriction. Allow local IPC in the execution environment,
then rerun the affected tests:

```bash
out/host-tools/venv/bin/ctest --preset linux-clang-development --rerun-failed --output-on-failure
```

Verify the actual failure before attributing it to sandbox policy. Keep these
tests enabled: they validate engine diagnostics and process supervision. To
force diagnostic helpers into report-only mode during local headless work, set
`LUDUS_DIAGNOSTIC_INTERACTIVE=0`.

## Development container

The checked-in `.devcontainer/` configuration supplies the Ubuntu 24.04 reference
host, LLVM 18 (including formatting, analysis and sanitizer tools), Python venv
support and native build prerequisites. It omits Wayland and Qt development
packages so the default configuration uses the headless backend.

Install Docker and the VS Code Dev Containers extension on the host, clone Ludus,
and choose **Dev Containers: Reopen in Container**. Container creation prepares
`linux-clang-development` with tests and the committed Conan lock; it downloads
dependencies but does not build Ludus. Then run in the container terminal:

```bash
./scripts/doctor
./scripts/build linux-clang-development
./scripts/test linux-clang-development
./scripts/install-sdk linux-clang-development
```

Setup runs as the unprivileged `vscode` user with `--no-system-install`. System
packages are installed while building the image, so workspace initialization
does not need sudo. The host still needs access to its Docker daemon; this
configuration does not install Docker or grant daemon access. Container creation
requires network access for the base image, Ubuntu packages and pinned dependencies.
The Dockerfile accepts an optional BuildKit `proxy_ca` secret containing a trusted
CA bundle for HTTPS package mirrors behind a proxy.

Start with a fresh checkout or move existing generated `out/` state aside before
switching between host and container builds. Tool shims, venvs, Conan files and
CMake caches contain machine-specific paths. The container limits builds to two
workers by default; adjust `CMAKE_BUILD_PARALLEL_LEVEL` for the available resources.
Desktop/editor and GPU rendering validation require a separate capable host.

## Trees

The source tree contains only hand-written project files. Generated state belongs under `out/`:

```text
out/host-tools/venv/        pinned CMake, Ninja, and Conan Python packages
out/host-tools/raddebugger/  optional pinned RAD source builds
out/conan/home/             project-local Conan cache and installed profile
out/conan/<preset>/         Conan CMakeToolchain and CMakeDeps files
out/build/<preset>/         CMake build trees and compile_commands.json
out/install/<preset>/       installed SDK prefixes
out/test-results/           reserved for local test output
out/logs/                   reserved for local logs
out/debug/rad/              local RAD launch descriptions and debugger sessions
```

The install tree is treated as the SDK boundary. The external consumer test uses `find_package()` against `out/install/<preset>/` rather than `add_subdirectory()` on the engine.

## Build flavor and assertion policy

Presets set `LUDUS_BUILD_FLAVOR` explicitly. SDK variants keep their generated
assertion/profiling policy with their headers and libraries; use separate install
prefixes for each variant. See [diagnostic helpers and assertion policies](diagnostics.md)
for launch/report commands and [the assertion contract](../architecture/assertions.md)
for caller behavior.

## Bootstrap

Run:

```bash
./scripts/bootstrap
```

Bootstrap is the exhaustive dependency-preparation step. It uses the system Python to create `out/host-tools/venv/`, then installs the pinned CMake, Ninja, and Conan versions from `config/tool_versions.json`. This is intentional network access.

After tool validation, bootstrap installs the committed Conan profile into the project-local Conan home, creates `conan.lock`, populates the Conan cache, writes Conan generator files under `out/conan/<preset>/`, and runs a minimal CMake configure smoke test.

## Conan

Conan 2 is used only before CMake configure. CMake does not invoke Conan, `FetchContent`, or any network fetch. The current native dependency set is declared in `conanfile.py` and pinned by
`conan.lock`; Catch2 is included only when tests are enabled. Some engine backends
use separately pinned sources under `third_party/`. Browser builds use the
separate pins in `config/web_toolchain.json`. The early Milestone 0 dependency
inventory is historical.

Normal configure, build, test, check, and SDK install commands use the Conan files already generated by bootstrap. They should not require network access unless generated state is removed.

## Presets

Development is the default local flavor. Prepare each preset before selecting
it; `--preset-only` does not prepare another profile's dependencies.

| Flavor | Linux | macOS | Purpose |
| --- | --- | --- | --- |
| Debug | `linux-clang-debug` | `macos-clang-debug` | Debug assertions and source inspection |
| Development | `linux-clang-development` | `macos-clang-development` | Optimized local development |
| Profile | `linux-clang-profile` | `macos-clang-profile` | Profiling flavor with Profile assertion policy |
| Release | `linux-clang-release` | `macos-clang-release` | Release policy; tests off by default |
| ASan/UBSan | `linux-clang-asan-ubsan` | `macos-clang-asan-ubsan` | Sanitizer validation |

Browser presets are `web-emscripten-development` and `web-emscripten-release`.
See [browser packaging](web-packaging.md) for that separate toolchain.
To inspect selectable presets with the project's prepared CMake:

```bash
out/host-tools/venv/bin/cmake --list-presets=configure
out/host-tools/venv/bin/cmake --list-presets=build
out/host-tools/venv/bin/cmake --list-presets=test
```

Development/Profile and sanitizer presets use `RelWithDebInfo`; the explicit
engine flavor determines assertion policy. Saved init selections can disable
sample/test/editor targets within a preset. See [diagnostics](diagnostics.md).

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

The formatter uses clang-format 18 and preserves compact designated initializers
on one line (`value = { .A = 0, .B = 1 };`). Multiline initializers place the
opening brace on its own line, with one member per line and a trailing comma.
Clang-format 18 alone cannot express the opening-brace rule, so run the project
command for both applying and checking formatting.

Local `./init.sh` enables the pre-commit formatter. To enable it in an existing
checkout, run `./scripts/install-hooks`. It formats only staged C/C++ in
`modules/`, `apps/`, and `tests/`; partially staged files keep their unstaged
contents, and fully staged files also receive the formatting in the working
tree. Custom hook paths and existing hooks are preserved. CI checks the same
formatter in a separate job before starting builds.

Run only clang-tidy:

```bash
./scripts/check linux-clang-development --tidy
```

clang-tidy uses the selected preset's `compile_commands.json` and analyzes project sources under `modules/` and `apps/`, not generated, installed, or third-party code.

`LUDUS_TIDY_JOBS` bounds concurrent analyzer processes (default 1). CI uses
two workers per job and two native shards. To reproduce one shard:

```bash
LUDUS_TIDY_JOBS=2 LUDUS_TIDY_SHARD_COUNT=2 LUDUS_TIDY_SHARD_INDEX=0 \
  ./scripts/check linux-clang-development --tidy
```

Shard indices start at zero; every shard must pass. The default count of one
analyzes the full database. Invalid indices and empty shards fail explicitly.
Browser analysis uses the same worker limit and includes owned engine,
application, probe and consumer-fixture sources while excluding pinned vendor implementations.
Both browser build configurations remain checked.

The native Development CI job also installs and relocates the SDK and links an
external consumer with Clang 18, without producer dependency paths. This reuses
the existing SDK build and makes relocation a required gate. Debug runtime
validation covers Debug assertion policies; the separate assertion matrix
continues to exercise Profile and Release.

`--all` (and `--format`) also runs the foundational include-boundary check
(`tools/check_foundational_includes.py`): a fast, text-only gate that fails if a
foundational header (`core.h`/`config.h`/`compiler.h`/`types.h`) pulls a
string/container/heavy STL header, or if any public header pulls a heavy STL or
private `internal/` header. Header self-sufficiency (every public header
compiles standalone) is checked by the `ludus_header_self_sufficiency` CTest.
See `docs/architecture/foundational-headers.md` and ADR 0007.

## Foundational header

Engine files get the universal Ludus vocabulary — fixed-width types, build/OS/
arch/compiler macros, codegen attributes, `Move`/`Forward`, and the assertion
macros — from a single header:

```cpp
#include <ludus/foundation/base/core.h>
```

Everything heavier (containers, `UniquePtr`, strings, logging, profiling,
platform, graphics) is included explicitly by the files that use it. `core.h` is
what a file *may* rely on, not mandatory boilerplate; a file that needs only the
numeric types may include `<ludus/foundation/base/types.h>` directly. See
`AGENTS.md` ("Foundational headers and the include boundary") for the
Allowed / Required-explicitly / Forbidden rules.

## Precompiled header

`core.h` can be precompiled as a pure build accelerator. It is opt-in and off by
default:

```bash
cmake --preset linux-clang-development -DLUDUS_ENABLE_PCH=ON
```

The PCH only ever mirrors `core.h` (never containers/strings/logging), and the
default build (PCH off) is the guarantee that no file depends on a PCH-only
symbol. See `cmake/EnginePch.cmake` and ADR 0007.

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
