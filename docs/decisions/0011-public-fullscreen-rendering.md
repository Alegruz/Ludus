# ADR 0011: bounded public fullscreen rendering

- Status: Accepted for this vertical slice
- Depends on: ADR 0009 (browser toolchain), ADR 0010 (verified Slang compiler)

## Decision

Export `GraphicsRhi` shader/uniform/pipeline handles and descriptions alongside
its existing lifecycle/frame API. Supply generated SPIR-V and WGSL with separate
explicit entry points. Keep backend objects private and keep all application
shader content outside engine APIs. Export an offline `ludus_compile_shader`
helper with the SDK, using the separately pinned host compiler and validator.

Bound pools to eight resources per kind, one uniform binding, one surface
pipeline and one triangle draw per frame. Poll Ready/Pending/Failed creation.
Never reuse resource IDs; invalidate them before release on shutdown/loss.
Upload into fenced Vulkan slots or ordered WebGPU writes. Destruction and resize
retire graphics work and Vulkan maintenance1 presentation fences; missing WSI
maintenance support fails explicitly. Headless Vulkan exercises the public path
without a compositor. Replace the old native frame implementation with a Vulkan
1.1 render-pass path using the existing Volk/lifecycle/logging infrastructure;
its speculative optional feature chain and unsafe command/fence reuse did not
provide the synchronization required by this slice.

## Consequences

Consumers can use the installed SDK without engine source/private headers or a
runtime compiler. Application layouts remain separately verified contracts,
with reflected minimum binding sizes checked at pipeline creation. Main-thread
singleton ownership and fixed capacities are deliberate limits. This is not a
material, texture, compute or render-graph interface. Live native WSI, physical
GPU/browser acceptance and additional host compiler packages remain separate
validation gates, recorded in the handoff. No ocean/game settings enter Ludus.
