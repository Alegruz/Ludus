---
description: Navigate Ludus engine modules, ownership and implementation boundaries.
---

# Engine architecture map

Ludus composes explicit module owners into an application. Understanding which
owner holds a resource, which provider it borrows, and when it can publish a
change explains most startup, rendering and editing behavior.

This section describes the source baseline on October 5, 2026. It complements
[the workflow overview](../learn/architecture.md); the engineering documents
retain detailed contracts, reference attribution and acceptance evidence.

## Choose a subsystem

| Guide | Questions it answers | Current boundary |
| --- | --- | --- |
| [Foundation and jobs](foundation.md) | What can every module use? Who owns allocation and background work? | Foundation modules and bounded job graph kernel exist; work stealing and broad consumer migration remain later work |
| [Startup and shutdown](lifecycle.md) | When is a provider usable? How does partial startup unwind? | Private runner in the smoke app; engine-wide adoption is a migration plan |
| [Platform and rendering](rendering.md) | Who owns a window, frame and GPU resource? | Native Vulkan and browser WebGPU/WebGL 2 with a bounded public rendering slice |
| [Worlds and frame updates](world.md) | What makes an entity valid? When do edits and simulation take effect? | SDK identity/storage baseline and a game-owned reference application |
| [Content and audio](content.md) | Who owns source documents, resource revisions and playback? | Portable module targets; implemented authoring/acquisition workflow is native |
| [Runtime and editor](runtime.md) | How do SDKs, game code, configuration and desktop tools connect? | Native GameHost/live editing and optional Qt workspace; later editor stages remain planned |

## Module and dependency direction

The following is a responsibility map, not an exhaustive linker graph. Actual
public/private dependencies are specified by each module's CMake target.

| Layer or owner | Examples | Dependency rule |
| --- | --- | --- |
| Foundational vocabulary | `FoundationBase`: types, configuration macros, assertions | Never depends on a higher module |
| Foundation services | Memory, strings, containers, math, time, parsing, logging, profiling, jobs | Use explicit target dependencies; optional facilities do not enter `core.h` |
| Engine systems | Input, Platform, GraphicsRhi, Audio, Content, Ui, Text, GameplayWorld, PhysicsFluid, NetworkCore | Keep backend and OS details behind public engine vocabulary |
| Runtime composition | GameApi, GameHost, RuntimeConfiguration, application/session owners | Select services and control publication and resource lifetime |
| Authoring tools | Editor, CLI, cookers, packaging | Share portable services; keep Qt and tool dependencies outside the engine SDK |

Static libraries are the engine default. The native development gameplay module
is a deliberate separate ABI boundary; shipping game code is linked statically.
See [runtime composition](runtime.md) and the
[root target definitions](https://github.com/Alegruz/Ludus/blob/main/CMakeLists.txt).

## Public headers are a contract

Public headers live in a module's `include/` directory; private implementation
headers stay in `src/internal/` or another private implementation directory.
Installed consumers must not include private backend headers.

`core.h` guarantees only the foundational vocabulary. Containers, strings,
logging, math and other services require their own includes. Heavy facilities
such as formatting and filesystem implementation belong behind `.cpp`
boundaries. These rules keep dependency direction and header parse cost testable.
Read the [foundational header contract](https://github.com/Alegruz/Ludus/blob/main/docs/architecture/foundational-headers.md).

## Read implementation status carefully

A configured target proves a module exists, not that every host supports every
operation. The [platform guide](../guides/platform-targets.md) distinguishes
compile targets from validated hosts. For example, Text is currently native-only;
NetworkCore supplies bounded protocol primitives while Internet transports,
sessions, replication and prediction remain planned.

When a design document describes future phases, check the corresponding module
headers, tests and evidence before relying on the API. The
[decision records](https://github.com/Alegruz/Ludus/tree/main/docs/decisions)
explain why boundaries were chosen; they are not a feature-completeness list.
