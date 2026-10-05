# Repair project setup

A game needs both its committed build intent and local tool/SDK paths. Checking
metadata or finding a hidden base preset is not enough to prove a usable CMake
setup.

## Check before changing anything

In the editor, choose **Project → Check Setup**. Eligible project opening also
performs a read-only setup check. It reports missing/stale presets, tools and
SDK inputs without downloading, configuring, building or rewriting the game.

## Repair explicitly

Save project settings, then choose **Project → Repair Project Setup**. Keep
**Use this project's selected engine** unless you intend to choose a different
compatible installed engine. Review the setup choices and start **Repair Project**.

Repair regenerates owned local settings, preserves custom presets and unrelated
IDE settings, refreshes stale CMake inputs, and verifies selectable configure/
build/test presets plus real configure/build/native tests before reporting ready.
Browser setup additionally needs an installed matching browser SDK.

## Shared SDKs and local overrides

An SDK is the engine's installed headers, libraries and build metadata. Games
can share an immutable release SDK in the user store. An engine developer can
instead use a local install prefix.

```bash
# From a prepared engine checkout:
./scripts/install-sdk linux-clang-development

# With the installed CLI, select the resulting prefix for this game:
ludus project engine /path/to/MyGame \
    --sdk /path/to/Ludus/out/install/linux-clang-development --profile development
```

Machine paths belong in ignored `CMakeUserPresets.json` and `.ludus/local.json`,
not committed game metadata. The engine requirement and release lock express
portable project intent. A local override does not manufacture a published
release; a release-oriented lock can remain unresolved during local development.

## When an SDK or tool moves

Run Check Setup, select the intended engine, then repair or update explicitly.
Do not copy an old CMake cache from another machine. Inspect compiler, Ninja,
shader tools where required, SDK components and actual selectable presets.
The IDE should use the selected project-managed CMake in preset mode.

The CLI exposes the same setup operations:

```bash
ludus project check /path/to/MyGame --tools /path/to/Ludus
ludus project repair /path/to/MyGame --tools /path/to/Ludus --sdk /path/to/sdk
```

For store installation, engine migration and direct CMake, see the
[complete project/SDK guide](https://github.com/Alegruz/Ludus/blob/main/docs/development/project-sdk-workflow.md)
and [editor repair workflow](https://github.com/Alegruz/Ludus/blob/main/docs/development/editor-workspace.md).
