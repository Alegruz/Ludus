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

| Reading path | Questions it answers |
| --- | --- |
| [Foundation and jobs](foundation.md) | What can every module use? Who owns allocation and background work? |
| [Startup and shutdown](lifecycle.md) | When is a provider usable? How does partial startup unwind? |
| [Platform and rendering](rendering.md) | Who owns a window, frame and GPU resource? |
| [Terrain and generation](terrain.md) | How should ground, generation, editing and streaming connect? |
| [Procedural content generation](../../architecture/procedural-content-generation.md) | How do recipes, domain generators, author controls and runtime publication compose? |
| [Physics systems](../../architecture/physics-systems.md) | Who owns shapes, bodies, sensors, constraints and queries? |
| [Worlds and frame updates](world.md) | What makes an entity valid? When do edits and simulation take effect? |
| [Content and audio](content.md) | Who owns source documents, resource revisions and playback? |
| [Runtime and editor](runtime.md) | How do SDKs, game code, configuration and desktop tools connect? |

## Shared contracts and development policy

- [Module ownership and dependencies](../../architecture/foundational-headers.md)
- [Engine quality architecture](../../architecture/engineering-quality.md) and
  [consulted-source review](../../architecture/engineering-quality-gems-review.md)
- [Current CI checks](../../development/continuous-integration.md)
- [Platform targets and validated capabilities](../guides/platform-targets.md)
- [Architecture decisions](../../README.md#decisions)

Read each design's status and corresponding implementation evidence before
relying on a proposed API or platform integration.
