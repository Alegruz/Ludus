# Ludus game projects and shared engine SDKs

Game projects should own their application code and depend on a selected Ludus
SDK. The SDK is installed once and shared across projects. Editor and CLI are
two clients of the same project operations. This removes repeated nested engine
checkouts/builds while giving new projects a portable engine dependency.

This is an implementation design, not a report of shipped functionality. The
authoritative implementation contracts are in
[`requirements.md`](../../.kiro/specs/project-sdk-workflow/requirements.md),
[`design.md`](../../.kiro/specs/project-sdk-workflow/design.md) and
[`tasks.md`](../../.kiro/specs/project-sdk-workflow/tasks.md). Use the
[Kiro prompts](project-sdk-workflow-kiro-handoff.md) to implement them.

## What exists today

Ludus exports installed `Ludus::` CMake targets and generated assertion policy
headers. `scripts/install-sdk` builds/installs an SDK and exercises an external
consumer. `examples/editor-sdk-project` already demonstrates an application
consuming `find_package(Ludus CONFIG REQUIRED)` without compiling engine sources.

Editor E0 opens/saves version-1 project descriptors and configures/builds/runs an
external CMake application in a separate process. It does not create projects
or select/install SDKs. Its launcher and managed tooling currently depend on
the engine checkout. See [Editor usage](../development/editor-workspace.md).

The existing SDK test supplies generated Conan metadata alongside the install
prefix. Native package config finds volk and optional Wayland; Text additionally
links static FreeType/HarfBuzz dependencies. Therefore, the existing SDK boundary
is a useful starting point but not proof a downloaded archive works alone.

## The application relationship

| Owner | Responsibility |
| --- | --- |
| Runtime SDK | Public engine API, libraries, policy headers, link dependencies |
| Host tools | SDK store, project templates, build/run orchestration |
| Optional Editor | Project management UI and later content authoring |
| Game | Source, assets, CMake targets, launch settings, engine requirement |

A game never depends on Qt or Editor internals. Build definitions remain ordinary
CMake. Portable project intent lives in a versioned descriptor; an exact lock
identifies the engine packages. Local machine paths live in ignored settings.
SDK installation and engine version changes are explicit operations.

## Engine development and game development

For a game developer, the CLI/Editor resolves the project's lock to a shared,
immutable installed SDK. Build compiles game code and links existing libraries.
Multiple games can use one engine version, while older games retain older SDKs.

For an engine developer, a local override selects the SDK installed from the
developer's existing Ludus checkout. The engine is rebuilt/installed explicitly
after engine edits; the game then reconfigures and rebuilds against the changed
SDK inputs. The override is visible and does not rewrite the release lock.
There is no automatic fallback to fetching/building engine sources.

Keep game execution separate from the Editor initially. Static linking meets
the current application boundary without requiring a stable game-plugin ABI.
The optional [project-live-reload architecture](project-live-reload.md) now defines
that follow-on host/module and live editing boundary; it does not expand this SDK
milestone. Engine smoke apps remain internal validation tools.

## New Project and headless use

The Editor's New Project form collects destination/name/template/engine. Missing
SDK installation is a separate explicit stage. Both UI and CLI invoke the same
validation, SDK resolution and atomic template generation operations. A failed
creation leaves the existing workspace intact and no partially published game.

Generated files become user-owned; later template versions do not overwrite
game source. Opening a project reads metadata and local setup. Configure/build/run use
shared process supervision, build locks and CMake artifact resolution.

Creation, project Open, SDK update and explicit repair must also check local
setup: selectable configure/build/test presets, SDK and dependency paths,
compiler/Ninja/shader tools, and IDE CMake selection. Open reports problems
without modifying files or starting builds/downloads. Repair preserves custom
presets/settings and verifies configure/build/test. These shared CLI/Editor
checks are required by P16; their implementation remains part of this milestone.

The CLI must work without Qt or a display. A windowed game's runtime still needs
its documented display/GPU environment. Headless tooling and headless runtime
are separate capabilities.

## Delivery order and limits

First prove the runtime package is relocatable with its full dependency closure.
Next install the tools independently, introduce project locking/templates/CLI,
connect Editor creation, and validate versioned release candidates. Linux x64
native Debug/Development/Release is the initial acceptance matrix.

Host tools and target SDKs remain distinct, allowing future native-editor/web-game
workflows. Preserve current browser builds; new web project templates and general
game asset packaging are deferred. A game release contains its executable/assets
and necessary runtime files, not the SDK and authoring tools.

Ludus-Sandbox should eventually become the reference independent application
using this interface. It is not present in this checkout. The Ludus implementation
must prove a standalone reference consumer and document the separate Sandbox
conversion, without inventing changes or acceptance in that repository.
