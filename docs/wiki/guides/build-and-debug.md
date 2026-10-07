# Build, run and debug

The engine checkout and a game project are different build roots. Build the
engine to develop Ludus or produce an SDK. Build a game against a compatible
installed SDK to work on that game's code.

## In the editor

Open the game's `ludus.project.json` and review **Project settings**. Select the
saved source directory, CMake preset and executable target. Save settings before
launching an operation that requires a clean project.

- **Configure** prepares the selected game build tree.
- **Build** compiles the selected target.
- **Build and Run** builds, then launches that result.
- **Build and Debug in RAD** uses the optional prepared RAD debugger.
- **Build and Play** launches the supported GameHost integration.
- **Stop** cancels/stops owned work and waits for cleanup.

A failed build does not launch an older binary. Read **Output** and the operation
status; an accepted request alone is not completion.

## From the game CLI

With `ludus` installed and the game's SDK selected:

```bash
ludus project configure /path/to/MyGame --profile development
ludus project build /path/to/MyGame --profile development
ludus project run /path/to/MyGame --profile development
```

Configure/build/run do not acquire SDKs or change the project's release lock.
Resolve setup problems with [project setup](../../development/project-sdk-workflow.md).

## From an engine checkout

```bash
./scripts/build linux-clang-development
./scripts/test linux-clang-development
```

Use `--with-tests` during initialization to prepare test targets first. Debug,
Development, ASan/UBSan and Release have selectable native presets. Keep each
profile in its own build and SDK directories.

## Try the private Luau feasibility probe

Scripting is in its first feasibility milestone. The engine checkout has an
opt-in interpreter probe; the installed SDK and game/editor workflows have no
scripting provider yet. From a checkout prepared with the Development test
preset:

```bash
./scripts/luau-probe bootstrap
./scripts/luau-probe cook
./scripts/luau-probe run --preset linux-clang-development
./scripts/luau-probe check --preset linux-clang-development
```

Bootstrap explicitly downloads the pinned source and builds a separate host
compiler. Cook creates trusted bytecode fixtures. Run enables
`LUDUS_BUILD_LUAU_PROBE`, builds the interpreter and checks errors, allocation
failures, interruption and debugger primitives. Ordinary engine/SDK builds keep
the option OFF and need no Luau dependency. Evidence is written to
`out/build/<preset>/tools/luau-probe/evidence.json`.

The [S0 guide and lifetime audit](../../architecture/luau-s0.md)
explain sanitizer/Web commands, the private loader fix, diagnostic Release
configuration and remaining adoption gates. This slice has native Linux and
Web probe coverage; Windows, macOS, Android and iOS require their own acceptance.

## S1 native and Luau interaction

After the pinned native setup:

```bash
./scripts/script-interaction bootstrap
./scripts/script-interaction cook
./scripts/script-interaction run --preset linux-clang-development
./scripts/script-interaction check --preset linux-clang-development
./scripts/script-interaction run --preset linux-clang-asan-ubsan
```

The opt-in headless fixture runs the same door interaction through native C++
and generated Luau bindings. It checks declared state, ordered events, entity
identity, phase/capability rules, command outcomes, and preservation of
unpublished effects on fault. The native executable links no VM. Bootstrap
builds the pinned compiler/analyzer as separate host tools; cook strictly checks
the door behavior and fingerprints generated contracts and bytecode.

The [S1 contract and acceptance guide](../../architecture/luau-s1.md)
covers Web/Chromium commands, evidence locations and limitations. This remains
an experimental integration with the reviewed S0 profile; it adds no installed
scripting SDK, production asset/reload workflow, visual editor or C# runtime.

## Prepare native debugging

```bash
./scripts/setup-rad-debugger
./scripts/debug linux-clang-debug ludus_smoke
```

These commands operate on the trusted engine checkout. RAD is optional, pinned
tooling with Linux alpha limitations; it is not required to run the editor.
Its setup may build/download prerequisites. Prefer Debug for source-level
inspection and use sanitizer builds to investigate memory/undefined behavior.

## Background work and ownership

The editor and installed CLI share operation/setup policy rather than two
independent build implementations. Concurrent managed builds of one game build
tree report **Busy**. Direct CMake bypasses that cooperative lock; avoid using
both on the same tree simultaneously.

References: [native debugging](../../development/debugging.md),
[build profiles](../../development/building.md),
and [project operations](../../development/project-sdk-workflow.md).
