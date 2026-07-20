# Milestone 0 Architecture

Milestone 0 is a reproducible engine skeleton, not a renderer. It proves that a clean clone can bootstrap pinned tools, resolve dependencies, build, test, install, and be consumed as an SDK.

## Static Internal Libraries

Internal modules are static libraries by default. That keeps linking, symbol visibility, sanitizer runs, and debugging straightforward while the architecture is still small. Shared libraries and plugins add ABI, lifecycle, distribution, and tooling concerns that are better introduced when there is a real runtime need.

## Public and Private Headers

Public headers live under each module's `include/` directory. Private implementation headers live under `src/internal/`. Downstream targets receive only public include paths, and install rules export only public and generated public headers.

This split keeps the SDK boundary honest from the first milestone. It also makes refactoring private implementation details cheap because consumers cannot accidentally depend on them.

## Installed SDK Boundary

Games are expected to live in separate repositories later, so Milestone 0 already tests an installed package with:

```cmake
find_package(Ludus CONFIG REQUIRED)
target_link_libraries(consumer PRIVATE Ludus::FoundationBase)
```

The consumer under `tests/sdk_consumer/` is configured as its own CMake project against the install prefix. It does not use `add_subdirectory()` on the engine.

## What Is Deliberately Missing

There is no Vulkan, rendering hardware interface, windowing, input, shader compilation, asset system, job system, custom allocator, logging framework, reflection, serialization, ECS, editor, plugin system, hot reload, or game code.

Those systems need real requirements. Milestone 0 keeps the foundation boring and testable so later milestones have a reliable place to stand.
