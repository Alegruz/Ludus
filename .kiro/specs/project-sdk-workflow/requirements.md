# Ludus independent game project requirements

This milestone makes game projects independent consumers of a shared Ludus SDK.
Users can create, configure, build and run a project through either the Editor or
an installed CLI, without an engine source checkout. Engine developers can use
an SDK from their existing checkout through an explicit local override.

Read [design.md](design.md), [tasks.md](tasks.md), the
[architecture rationale](../../../docs/architecture/project-sdk-workflow.md)
and [Kiro handoff](../../../docs/architecture/project-sdk-workflow-kiro-handoff.md)
together. These documents specify future work; they do not certify existing
packages or commands. Existing Editor E0 behavior remains the baseline.

## Scope

Initial acceptance covers Linux x64 on the reference Ubuntu 24.04 and Clang 18
toolchain, single-config Ninja, native Debug/Development/Release SDKs, a minimal
native application template, and optional native Qt Editor integration. Preserve
existing browser builds and distinguish native host tools from Emscripten target
SDKs. New browser project creation, Windows/macOS support, scene authoring,
embedded play, hot reload and automatic compiler/system-package installation
are subsequent milestones. Do not advertise unsupported combinations.

## Required behavior

| ID | Requirement |
| --- | --- |
| P01 | A game owns its source, assets, build definitions and launch settings. It consumes public installed `Ludus::` targets and compiles no engine source during ordinary configure/build/run. |
| P02 | Installed SDKs are relocatable and include the complete redistributable link dependency closure, dependency CMake metadata and notices. Only documented system/toolchain prerequisites may remain external. No checkout, Conan cache or producer build path is required. |
| P03 | SDK identity includes engine version and source revision, target OS/architecture, compiler and C++ runtime ABI, flavor, feature set, assertion policy, sanitizer settings and package format. Compatibility is checked before configure. |
| P04 | Versioned project intent and an exact engine lock are committed. Machine paths and local overrides are ignored. Project movement and fresh clones preserve engine requirements. |
| P05 | A user-level store supports several immutable SDK versions/variants shared by projects. Explicit installation validates metadata, archive containment and checksums before atomic publication. Concurrent installation never exposes a partial SDK. |
| P06 | An explicit local installed-SDK override uses the same package interface, is displayed in diagnostics/UI, does not modify the committed lock and never silently builds the engine. Switching SDK identity invalidates or separates affected build trees. |
| P07 | Installed CLI and Editor call one project operation backend with the same resolution, creation, validation, build planning, locking and lifecycle semantics. The CLI does not require Qt or a Ludus checkout. |
| P08 | Project creation uses versioned bundled templates, validates the destination, stages complete files and publishes only to an absent destination. Failure/cancellation leaves no partially published project and never overwrites unrelated files. |
| P09 | Open reads and validates metadata and local setup only, reporting missing/stale presets, tools, SDK paths and IDE integration with actionable repair steps. Downloads, repair, migration, configure, build and execution are explicit operations. Existing version-1 projects remain readable and usable; migration is explicit and failure-preserving. |
| P10 | Builds use exact argument arrays, the shared CMake File API resolver and post-build artifact validation. Failed/cancelled builds cannot launch an older binary. Editor and CLI share cooperative build locks and cancellation/cleanup rules. |
| P11 | Runtime SDK, host tooling and Editor are separately installable. Runtime exports and game builds have no Qt dependency; installed tools do not import adapters from the engine checkout. |
| P12 | Application source compiles against a supported SDK/toolchain contract using C++23 and no exceptions for the supplied template. CMake describes target structure; project metadata does not become a duplicate build language. |
| P13 | CI assembles and tests release candidates in clean relocated environments, then publishes immutable checksummed artifacts for explicit version tags. Native Development and Release packages are available; Debug is included in the initial acceptance matrix. |
| P14 | An external reference project proves the same workflow used by generated projects. Ludus-Sandbox conversion is a separate repository change and requires inspecting that repository; absent access, record the pending follow-up rather than claiming completion. |
| P15 | Evidence distinguishes tests, real native GUI/runtime acceptance, unavailable gates and pending repository work. Existing runtime, Editor E0, RAD, browser and repository standards remain intact. |
| P16 | Creation, SDK selection/update and explicit repair share setup checks with Open through the CLI/Editor backend. Supported configure/build/test presets must be selectable through the selected CMake, not only hidden bases. Validate inheritance/conditions, SDK/dependency prefixes, compiler/Ninja/shader tool paths and IDE CMake selection. Preserve custom presets/settings, keep machine paths ignored, refresh stale caches, and verify configure/build/test after repair. |

## Acceptance journey

On a clean supported machine with documented system/toolchain prerequisites,
install the host tools and a released SDK, create a native project using the CLI,
build/run it without Qt, move the project and SDK, then build/run again. Repeat
creation and build/run through an installed Editor. No engine checkout or Conan
cache is present. Two projects reuse one SDK without modifying it.

Select a local installed SDK built from the developer's one engine checkout.
Build the game without rebuilding Ludus; refresh that SDK explicitly, then
reconfigure/rebuild the game. Show the selected revision and override in output.
Return to the locked SDK without editing committed metadata. Verify mismatched
flavors/toolchains, missing packages, corrupted installs and cancellation produce
actionable failures, not fallback engine builds or stale launches.

Exercise a fresh clone with only hidden base presets, missing user presets,
broken inheritance, disabled presets, absent build/test presets, moved SDK/tools
and stale IDE CMake selection. Open reports each problem without mutating files
or starting configure/download/build. Explicit repair restores selectable presets
and a working configure/build/test. Repeat repair and prove custom presets,
editor settings and application source survive. Run through both CLI and Editor.
