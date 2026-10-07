# Runtime and editor

The native editor owns documents and workflow controls. A separate GameHost
process owns the play session and engine resources. The installed SDK supplies
public engine libraries plus compatibility metadata; the game project owns its
authored code/content and portable build intent.

## Process and ABI boundaries

| Owner | Responsibility | What crosses the boundary |
| --- | --- | --- |
| Optional Qt editor | Workspace, draft documents, build/run controls | Copied bounded protocol data to the host supervisor |
| Shared CLI/project tools | Setup validation, build resolution, generation publication | Explicit project paths, descriptors and operation results |
| Native GameHost | Platform/Input/RHI session and gameplay lifetime | Versioned GameApi function table and explicit host services |
| Gameplay implementation | Game-specific state and behavior | Opaque instance and bounded values/checkpoints |
| Shipping executable | Static game/runtime composition | The same gameplay implementation linked statically |

The gameplay ABI does not pass C++ classes, STL objects, virtual tables,
exceptions or cross-module deletion. Creator-owned destruction and compatible
SDK identity matter as much as symbol lookup. Engine libraries stay static;
live editing replaces the native gameplay module, not arbitrary engine libraries.

The implemented native loader is Linux x64 with a compatible Debug or Development
SDK. Windows loading and dynamic browser Wasm replacement are separate work.
See the [live-editing guide](../../development/project-live-reload.md)
and [ABI decision](../../decisions/0012-game-host-and-live-reload.md).

## Gameplay replacement

Build a new immutable generation while the current generation remains active.
At a frame boundary the host validates compatibility, quiesces the old instance,
captures a bounded checkpoint and prepares a candidate with restricted services.
A recoverable rejection discards the candidate and resumes the old instance.
A successful prepared swap retires old instances/callbacks before unloading code.

Changing state layout needs declared migration or restart. Raw object memory is
not preserved across code changes. Crashes and arbitrary external side effects
cannot be rolled back by the transaction; uncertain retirement requires recovery
or process restart. See [reload design](../../architecture/project-live-reload.md)
alongside the implemented guide rather than treating every design phase as shipped.

## Script debugging and replacement

The private [S2 scripting slice](../../architecture/luau-s2.md) adds source stops,
copied locals and whole-VM replacement with declared-state migration. GameApi
1.1 appends `ProcessScriptDebug`, gated by the optional `ScriptDebug` capability
and negotiated table size. Providers receive bounded JSON over the same GameHost
connection; no VM pointer crosses the ABI. Update may return `ScriptPaused`:
control/platform pumping continues while host simulation ticks remain frozen.
Checkpointing and reload wait for a completed tick. Instance destruction retires
all stopped/running VM roots before native module release.

The fixture verifies two separately loaded native module images and browser
Wasm event-loop responsiveness. It remains default-off and outside the installed
scripting SDK; DAP/editor UI, waits, other providers and device/performance
qualification are later work. See [commands and limits](../guides/build-and-debug.md#s2-debug-and-program-replacement).

## Optional installed behavior provider

[S4](../../architecture/behavior-s4.md) supplies `Ludus::Behavior`, generated
project contracts, native/Luau staged transactions, declared-state codecs and
paired offline cooking. An independent relocated SDK consumer builds and executes
a static shipping program. The component is default-off; Editor/GameHost wiring,
six-platform acceptance and representative release qualification remain S5–S7.
The private S2/S3 fixtures do not become editor features simply by enabling it.

## Configuration is a control operation

`FoundationConfig` owns typed schema/layer evaluation and prepared candidates.
`FoundationConfigJson` adds bounded JSON adaptation. `RuntimeConfiguration` owns
the first GameHost schema and plain host options. Qt, filesystem discovery, Python
and TOML authoring stay outside these portable runtime cores.

One control owner validates changes and publishes options at a safe point.
Subsystem hot loops read plain module-owned options rather than looking up
strings in a global registry. Applying a session value, applying it to an authored
document, and saving preferences are separate operations.

Use [configuration workflow](../guides/configuration.md) for layer precedence,
inspection and persistence, and the
[configuration contract](../../architecture/engine-configuration.md)
for descriptor/context lifetime and prepared transactions.

## The editor is a client of shared policy

Opening a project checks setup without downloading, configuring or building it.
Explicit creation/repair operations validate selectable CMake presets, SDK/tool
paths and actual configure/build/test behavior. Machine-specific paths remain
in ignored local settings. Editor and CLI use the same project policy.

Qt remains optional and outside the installed engine SDK. The workspace shell
and current controllers exist; later scene/document editing stages in the editor
design remain acceptance contracts. Use [current capabilities](../getting-started/status.md)
and [editor workflow](../../development/editor-workspace.md) to determine available operations.

Read the [editor ownership design](../../architecture/editor-architecture.md)
and [project/SDK contract](../../architecture/project-sdk-workflow.md).


## Multiplatform GUI direction

The proposed editor separates a Qt-free document/command core, Qt native
presentation and Ludus-rendered authoring viewports. Gameplay remains isolated
in GameHost. Linux workspace and a bounded Qt/Wasm document preview exist;
native macOS/Windows tools, the core extraction and scene integration are
future work. Browser scene authoring must qualify text, accessibility, large
models and canvas composition before its frontend is fixed.

Read the [GUI systems design](../../architecture/editor-gui-systems.md),
[decision](../../decisions/0023-editor-presentation-and-document-core.md)
and [actual reference review](../../architecture/editor-gui-reference-review.md)
for ownership, transactions, platform gates and the book-informed refinements.
