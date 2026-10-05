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
Resolve setup problems with [project setup](project-setup.md).

## From an engine checkout

```bash
./scripts/build linux-clang-development
./scripts/test linux-clang-development
```

Use `--with-tests` during initialization to prepare test targets first. Debug,
Development, ASan/UBSan and Release have selectable native presets. Keep each
profile in its own build and SDK directories.

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

References: [native debugging](https://github.com/Alegruz/Ludus/blob/main/docs/development/debugging.md),
[build profiles](https://github.com/Alegruz/Ludus/blob/main/docs/development/building.md),
and [project operations](https://github.com/Alegruz/Ludus/blob/main/docs/development/project-sdk-workflow.md).
