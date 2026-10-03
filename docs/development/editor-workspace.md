# Ludus Editor workspace (E0) — usage and troubleshooting

The editor is an **optional, OFF-by-default** native developer tool
(`apps/editor`, target `ludus_editor`). It opens a project descriptor, edits its
native build/launch settings, saves, reopens, configures/builds, runs the
selected executable in a separate process, stops it, and reports failures. It is
not a scene editor and embeds no game framebuffer; the runtime has its own
window and the editor's status area only describes it.

See the specification package for the authoritative contract:
`.kiro/specs/editor-workspace/{requirements,design,tasks}.md` and
`docs/architecture/editor-workspace-research.md`.

## Prerequisites (optional editor setup)

The editor needs Qt 6 (6.4-compatible) with the Core/Gui/Widgets modules and a
Wayland platform plugin, in addition to the normal reference toolchain
(`./init.sh` prepares pinned Clang 18 / LLD / managed CMake/Ninja/Conan and the
managed Python venv). Qt is not added to Conan or the SDK. Default initialization
skips editor setup; select **Build Ludus editor** in the setup window or use
`./init.sh --cli --with-editor` to install missing Ubuntu/Debian Qt packages and
build the selected native Debug or Development editor. `--no-editor` explicitly
disables the editor for the prepared presets. Initialization also excludes test
targets by default; use `--with-tests` to include editor tests or `--run-tests` to
build and execute the enabled tests after setup.
Editor tests also require Qt Test, supplied by the same Qt base development
package on Ubuntu/Debian.

To manage Qt yourself, install it once (names vary by distribution) and use
`./init.sh --cli --with-editor --no-system-install`:

- Debian/Ubuntu: `sudo apt-get install -y qt6-base-dev qt6-wayland`
- Fedora/Amazon Linux: `sudo dnf install -y qt6-qtbase-devel qt6-qtwayland`

Only the editor configure/build uses Qt. A default configure
(`LUDUS_BUILD_EDITOR=OFF`) and all browser builds never search for Qt.

## Build the editor (Editor ON)

The editor is gated behind `LUDUS_BUILD_EDITOR` (OFF by default). Configure a
native preset with the option ON using the managed CMake, then build the target:

```bash
./init.sh --cli --with-editor                # optional Qt prerequisites + editor build
# Or manage Qt and the editor build yourself:
./init.sh                                   # pinned tools + managed venv (once)
# Editor ON configure/build (managed cmake shown as `cmake` for brevity):
cmake --preset linux-clang-development -DLUDUS_USE_INIT_OPTIONS=OFF -DLUDUS_BUILD_EDITOR=ON
cmake --build --preset linux-clang-development --target ludus_editor
```

`LUDUS_BUILD_EDITOR=ON` on an unsupported host (non-Linux, non-x64, or the
browser toolchain) fails configuration with a specific message rather than
producing a broken build.

## Launch

`scripts/editor` locates the already-built `ludus_editor` through the shared
CMake File API helper and starts it with the trusted managed Python interpreter,
the `editor_tool.py` adapter, and this tooling root. It never builds or installs
anything; if the editor is not built it prints the preparation commands above.

```bash
./scripts/editor --preset linux-clang-development \
                 --project examples/editor-workspace/ludus.project.json
```

Opening a descriptor reads metadata and queues a read-only setup check for v2
SDK projects. The check uses the selected CMake to list presets and reports
missing/stale setup in the status area and Output. It never configures, builds,
downloads, runs project hooks, or changes files. Configure, Build, Build and Run,
and Stop remain explicit actions.

## Recent projects

The **Recent Projects** panel and **File → Recent Projects** menu reopen saved
descriptors directly. Double-click a panel entry or select it and choose
**Open Selected**. The editor remembers the ten most recently opened projects
across restarts, with their names and absolute descriptor paths. Opening a project
from the command line or completing New Project adds it too. Failed opens do not
change the list; opening an entry again moves it to the top.

Reopening uses the same unsaved-change prompts and read-only setup checks as
**Open Project**. Missing descriptors stay marked **(missing)**; trying to open
one reports the error and retains the current workspace. **File → Clear Recent
Projects** clears history without deleting or changing any project. **View →
Recent Projects** restores a closed panel.

History is local to the user, in `$XDG_CONFIG_HOME/Ludus/Editor/recent-projects.json`
(normally `~/.config/Ludus/Editor/recent-projects.json`). It is saved atomically,
separately from project files. If preferences cannot be written, project opening
still works and history remains available for the current session.

## Create, initialize, repair and update projects

The **Project** menu provides **New Project**, **Check Setup**, and
**Initialize / Repair / Update Setup**. Setup actions require a saved, clean v2
CMake project; save pending edits first.

For Ludus-Sandbox, open its `ludus.project.json`, choose the setup action and
select an installed native SDK matching the descriptor profile and engine
version. A compatible Web SDK additionally enables the existing web Development
and Release bases. Blank SDK fields retain previously selected prefixes. To
update the local SDK selection, choose a new compatible prefix and repair again.
The operation does not change the project engine requirement or committed lock.

Repair verifies the managed CMake, Ninja, Clang 18, required SDK components,
dependency prefixes and shader tool paths. It generates marked
`ludus-local-<profile>` presets in ignored `CMakeUserPresets.json`, keeping custom
presets and their includes. Machine paths stay in ignored presets and
`.ludus/local.json`; the IDE uses preset mode and each preset selects its CMake
executable. Related stale IDE CMake-path overrides are removed; unrelated
settings are preserved. Real CMake configure/build/test preset discovery must
succeed, followed by a fresh configure, full build and native CTest. No-tests
is an error. Web profiles configure and build; browser runtime acceptance is
separate. The setup signature detects changed SDK contents and moved tools.

The optional **Prepare tools and build/install the native engine SDK** checkbox
explicitly initializes and builds the trusted tooling checkout, then installs
the SDK before repairing the game project. This can download dependencies; it
does not install system packages or execute the game project’s bootstrap hooks.
Web SDK acquisition remains an explicit engine-tooling step; select an installed
Web SDK in this dialog. All child commands use the existing streaming output,
Stop, process-group cleanup and single-operation ownership.

New Project creates the bundled minimal v3 native template in a sibling staging
directory, verifies setup/configure/build/tests, then publishes to an absent
destination and opens it. Failure/cancellation before publication discards the
stage and leaves the current workspace intact. Staging build artifacts are
discarded because their CMake paths belong to the staging directory. The next
Configure builds at the final location.

The CLI calls the same backend:

```bash
ludus project check /path/to/Ludus-Sandbox --tools /path/to/Ludus
ludus project repair /path/to/Ludus-Sandbox --tools /path/to/Ludus --sdk /path/to/sdk
ludus project update /path/to/Ludus-Sandbox --tools /path/to/Ludus --sdk /path/to/new-sdk
ludus project create /path/to/new-game --name MyGame --sdk /path/to/sdk --tools /path/to/Ludus
```

`create --engine` without `--tools` can still generate metadata without an SDK.
Use `--tools` with an installed SDK for the verified creation journey.

## The project descriptor (version 1)

One UTF-8 JSON file, conventionally `ludus.project.json`
(`examples/editor-workspace/ludus.project.json`):

```json
{
  "version": 1,
  "name": "Ludus smoke workspace",
  "provider": "ludus",
  "source_dir": "../..",
  "preset": "linux-clang-debug",
  "target": "ludus_smoke",
  "run": { "cwd": ".", "args": [] }
}
```

- `version` must be the number 1. `provider` is exactly `ludus` or `cmake`.
- `source_dir` and `run.cwd` are **relative** paths (intentional `..` allowed);
  they resolve against the descriptor directory / resolved source, never the
  editor launch directory. `source_dir` must contain `CMakeLists.txt` and
  `CMakePresets.json`.
- `preset` is `linux-clang-debug` or `linux-clang-development` in E0.
- `target` matches `[A-Za-z0-9_][A-Za-z0-9_.+-]*` and must resolve to a unique
  executable after Configure.
- `run.args` is an exact argument list (one string per entry). Empty, quoted,
  Unicode, whitespace and shell-looking entries are preserved verbatim; there is
  **no** shell, globbing, variable expansion, or RAD-style argument restriction.

Size limits: file ≤ 64 KiB, name ≤ 128 bytes, paths ≤ 4096 bytes, target ≤ 256
bytes, ≤ 64 args each ≤ 4096 bytes and ≤ 32 KiB total.

## Save, conflict and recovery

Save validates the whole draft, takes a short cooperative lock in an ignored
`.ludus/` directory beside the descriptor, checks the on-disk bytes still match
the digest recorded at Open/last Save, then writes atomically with `QSaveFile`
(direct-write fallback disabled). A short/failed write, commit failure, or disk
conflict **retains the old saved file and your dirty draft** and reports the
error; it never silently overwrites. If the file changed on disk you get a
Conflict and an explicit Reload (with discard confirmation).

Open/Reload/Close on a dirty document offer Save / Discard / Cancel; a failed
Save aborts that action.

## Build / run semantics

Build and Build-and-Run require a clean saved document, snapshot it, verify its
digest, Configure, resolve the executable through the CMake File API codemodel
(never a guessed path), build only that target, re-resolve the artifact after
build (CMake may regenerate), and validate it. **A failed or cancelled build
never launches any artifact, including a previously successful one.** Run uses
the resolved absolute artifact, the explicit `run.cwd`, the inherited session
environment, and the exact argument list — no shell, no detached launch.

### External installed-SDK projects (provider `cmake`)

An external project (`examples/editor-sdk-project/`) uses `provider: "cmake"`,
`source_dir: "."`, and its own `CMakePresets.json`. Its two supported native
presets must produce single-config Ninja trees at `<source>/out/build/<preset>`.
The sample's preset supplies `CMAKE_PREFIX_PATH` from `$env{LUDUS_SDK_PREFIX}`;
the matching SDK must already be installed:

```bash
./scripts/install-sdk linux-clang-development   # produces out/install/<preset>
export LUDUS_SDK_PREFIX="$PWD/out/install/linux-clang-development"
./scripts/editor --project examples/editor-sdk-project/ludus.project.json
```

The adapter does not choose, build, or install an SDK, and never injects engine
Conan profiles into the external project. The external project compiles no engine
sources. Unsupported generators/tree layouts fail explicitly.

## Operations, cancellation and close

Exactly one operation is owned at a time. Configure/Build/Run never block the
GUI. Stop is idempotent, uses deadlines, terminates ordinary descendants (owned
process group), drains output, and confirms cleanup. A Stop accepted before the
runtime spawns prevents the launch. Normal Close while busy offers "Stop and
Close" (asynchronous; closes only after cleanup is confirmed) or "Keep Open"; a
close request is cancellable. If the supervisor is lost or cleanup cannot be
confirmed, the editor enters **CleanupUnknown**: no new operation and no
automatic close until explicit operator recovery (restart), with the known
job/PID details available through Copy Job Details.

An operation holds a cooperative, nonblocking lock on its build tree for the
whole configure/build/run (including runtime ownership). A second editor
instance targeting the same build tree gets a `Busy` result rather than racing
it. This lock is cooperative: an independent CLI build against the same tree
does not honor it, which is why concurrently modifying one build tree from two
tools is unsupported (see Known limitations).

## Copy Job Details

Produces a bounded, telemetry-free text record: descriptor digest,
provider/preset/target, absolute argv/cwd per executed stage, job id, stage
transitions, last exit/signal/result, cleanup status and dropped-output counts.
It contains **no** environment dump, credentials, or automatic upload, and makes
no deterministic-replay claim. Its argv/cwd can reproduce a tool invocation
manually.

## Known limitations (first acceptance baseline)

This first milestone does **not** promise:

- containment of a project that deliberately daemonizes or escapes its process
  group (no cgroup/service-manager/containment framework);
- recovery from a `SIGKILL` of the supervisor itself or machine loss;
- power-loss durability on every filesystem (atomic replacement is not a
  durability guarantee);
- coordination with independent CLI builds against the same build tree (the
  per-tree lock is cooperative; external CLI invocations do not honor it);
- that "Running" proves the game rendered a frame; it means the process started.

Windows/macOS process backends and a browser editor are out of scope for E0.


## Debug a native game

The Game toolbar and Build menu offer **Build & Debug in RAD** (F5 in the Editor)
for a clean, saved Debug or Development project. It builds the selected target
and verifies native symbols before opening RAD. RAD stays optional for all other
project operations. If it is missing, choose **Set Up RAD**, **Choose Existing
Installation**, or **Cancel**. Setup downloads/builds the pinned local revision;
missing system prerequisites are reported in Output.

The status says **RAD session open**. Run, breakpoints and stepping happen in RAD;
Stop closes the owned session and game. Local debugger preferences and existing
breakpoints survive repeated launches. The pinned Linux argument limitations apply
only to this debugging action. See [Native debugging](debugging.md#from-the-ludus-editor)
for setup, session files and compatibility acceptance.
