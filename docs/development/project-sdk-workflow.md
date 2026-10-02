# Independent Ludus game projects and shared SDKs — user guide

This guide covers installing the host tools, installing and overriding/refreshing
SDKs, project creation and migration, building with the CLI (and the Editor),
direct CMake use, recovery and the current compatibility limits. It documents the
workflow implemented by `.kiro/specs/project-sdk-workflow`.

> Scope. The initial supported matrix is **Linux x64**, the reference Ubuntu
> 24.04 + Clang 18 toolchain, single-config Ninja, native Debug/Development/
> Release SDKs and a minimal native template. Windows/macOS, new browser project
> templates, scene authoring, embedded play and hot reload are later milestones.
> Do not assume unsupported combinations work.

## Prerequisites

**Host tools** (the `ludus` CLI): Python ≥ 3.10. No Qt, no engine checkout.

**Building a generated game** additionally needs the documented toolchain for a
Ludus SDK: CMake ≥ 3.29, Ninja, Clang 18 + LLD 18, and — for shader compilation —
the pinned Slang compiler and `spirv-val` (`config/shader_toolchain.json`). A
windowed game also needs its runtime display/GPU environment (a Vulkan loader +
driver); headless *tooling* does not imply a windowed game runs without a
compositor/GPU.

## Install the host tools

```bash
python3 -m pip install ./scripts/python     # from a Ludus checkout, or the published wheel
ludus --help
```

The installed `ludus` launcher locates its own packaged modules; it does not need
a Ludus source checkout.

## Install an SDK

```bash
# From a local archive (checksum optional but recommended):
ludus sdk install --archive ludus-sdk-development.tar.gz --digest <sha256>

# From a publisher release catalog:
ludus sdk install --version 0.1.0 --target linux-x64 --flavor development \
    --catalog https-or-local-catalog.json

ludus sdk list
```

SDKs install into a shared user store (default `~/.local/share/ludus`,
overridable with `--store` or `LUDUS_SDK_STORE`). Each SDK directory is immutable
and content-addressed, so several projects share one copy. Installation validates
archive bounds/containment, the payload digest and the manifest identity, stages
to a sibling directory and atomically publishes — a concurrent or cancelled
install never exposes a partial SDK, and an existing good SDK is never replaced by
a partial one. Repair a corrupt store entry explicitly with `--repair`.

## Create a project

```bash
# Against a release (writes an exact engine requirement; the lock stays
# "unresolved" until a published release exists):
ludus project create ./MyGame --name "My Game" --template minimal --engine 0.1.0 \
    --components FoundationBase,GraphicsRhi

# Against a local SDK you built yourself (engine-developer flow). The local
# prefix is recorded ONLY in ignored .ludus/local.json; the committed lock stays
# release-oriented:
ludus project create ./MyGame --name "My Game" --template minimal --sdk /path/to/sdk-prefix
```

Creation is atomic and no-replace: it refuses an existing/symlink/nonempty
destination, stages the complete project, and publishes only to an absent
destination (a destination appearing mid-create is never overwritten). Generated
files are yours after creation; later template versions never overwrite your game
code. The generated `CMakeLists.txt`/`CMakePresets.json` are ordinary, editable
CMake — project metadata is not a second build language.

## Configure, build, run

```bash
ludus project configure ./MyGame --profile development
ludus project build     ./MyGame --profile development
ludus project run       ./MyGame --profile development   # ensures a fresh build first
```

Configure/build/run never download an SDK, refresh the lock or compile engine
sources. SDK resolution precedence is: `--sdk <prefix>` for this command → the
saved `.ludus/local.json` override → the exact locked release in the store. The
resolved identity, path and override status are printed. A failed or cancelled
build never launches an older binary. The CLI and Editor share a per-build-tree
cooperative lock, so a concurrent build of the same tree reports `Busy` rather
than corrupting it. Raw `cmake --preset …` works too (set `LUDUS_SDK_PREFIX`
yourself) but is outside the cooperative-lock guarantee.

## Engine override and refresh (engine developers)

```bash
# Point a project's build at a locally built SDK, without touching the lock:
ludus project engine ./MyGame --sdk /path/to/local/sdk --profile development

# After rebuilding/reinstalling that local SDK, just rebuild the game — the
# tooling detects the changed SDK inputs (not just the path) and reconfigures/
# relinks. Local engine install and game build must not run at the same time.
ludus project build ./MyGame --profile development

# Return to the locked release without editing committed files:
ludus project engine ./MyGame --clear-override --profile development
```

The override is visible in build output and never rewrites the committed
descriptor/lock. To test a *different* engine version, update project intent
explicitly (`ludus project engine --version <release>` / `ludus project migrate`)
rather than overriding past compatibility validation.

## Migrate a version-1 project

```bash
ludus project migrate ./LegacyGame --engine 0.1.0
```

Migration is explicit (Open never migrates silently), preserves relative paths and
launch settings, requires an engine selection, and commits the new descriptor and
lock together. If a migration is interrupted, leftover `*.migrating` files mark an
incomplete commit; rerun migration or discard them to recover.

## Direct CMake (no Ludus CLI)

```bash
export LUDUS_SDK_PREFIX=/path/to/installed/sdk-prefix
cmake --preset linux-clang-development
cmake --build out/build/linux-clang-development --target my_game
```

The generated presets read `$env{LUDUS_SDK_PREFIX}`. `find_package(Ludus CONFIG
REQUIRED)` resolves the bundled dependency metadata relative to the SDK prefix and
performs the usual CMake version/variant compatibility checks.

## Recovery and failure modes

- Unresolved lock + no override → build stops with an actionable message (install
  the release or set a local override); it never falls back to building the engine.
- Flavor/toolchain/policy mismatch → rejected before configure with expected-vs-
  actual detail.
- Corrupt SDK in the store → explicit `ludus sdk install --repair`.
- Interrupted migration → discard `*.migrating` and retry.

## Compatibility limits

SDK identity is derived from the real build inputs (triple, compiler id/version,
C++ runtime ABI, flavor, variant, policy, features) — never inferred from a
version string. An SDK is used only when that identity is compatible with the
request. See `docs/architecture/project-sdk-workflow.md` for the rationale and
`docs/development/project-sdk-workflow-evidence.md` for the implementation
evidence and the gates still pending on a reference-toolchain machine.

## Ludus-Sandbox conversion (follow-up)

Ludus-Sandbox should become the reference independent application using this
interface (remove its mandatory nested engine build; pin an engine release). That
repository is **not present in this checkout** and has not been inspected, so its
conversion is a **separate** change requiring access to that repository and its
own evidence. The reference consumer validated here is the standalone project in
`tests/sdk_consumer` plus the generated minimal template; converting Sandbox is
tracked as external follow-up and is not claimed complete by this work.
