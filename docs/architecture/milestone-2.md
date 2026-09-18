# Milestone 2 Architecture

Milestone 2 is the first graphics milestone, not a renderer. It proves that an external Ludus application can use the installed SDK to initialize Vulkan, render into a swapchain image, and present a visible frame through the Milestone 1 runtime shell.

## Vulkan Presentation Path

Milestone 2 introduces the smallest Vulkan path needed to produce and present a frame. The runtime should create the Vulkan instance, create a surface for the Ludus window, select a suitable physical device, create a logical device and required queues, create a swapchain, acquire an image, record commands, submit work, and present.

The first visible result should be intentionally simple, such as clearing the swapchain image to a solid color. A programmable graphics pipeline, shaders, meshes, materials, and scene rendering are deferred until a later milestone so this milestone can focus on device, presentation, synchronization, and lifetime correctness.

Window resize should recreate swapchain-dependent resources when required. Minimize and restore should not corrupt state, crash, or continuously perform invalid presentation work.

## Graphics Module Boundary

Vulkan belongs inside Ludus, not inside `Ludus Sandbox`. The sandbox continues to consume only installed Ludus targets and public headers.

Milestone 2 does not need a generalized rendering hardware interface. If Vulkan-specific types or modules are the most direct representation of the implemented system, they should remain Vulkan-specific until another real backend or higher-level rendering requirement creates pressure for an abstraction.

The graphics dependency structure should remain narrow and explicit. Platform functionality supplies the native window and surface requirements, while the graphics implementation owns Vulkan objects, frame submission, synchronization, swapchain state, and teardown.

## Installed Graphics SDK Boundary

The graphics capability must remain usable through the installed SDK workflow established by the earlier milestones. `Ludus Sandbox` should acquire and configure the Ludus SDK through its onboarding path, then build independently without source-tree coupling.

Running the sandbox should create the Milestone 1 window and continuously present valid Vulkan frames. Development configurations should enable Vulkan validation where practical, and normal startup, resize, presentation, minimize/restore, and shutdown should complete without validation errors or sanitizer-detected host errors.

Milestone 0 and Milestone 1 validation remain intact. Adding graphics must not weaken bootstrap, tests, checks, sanitizer runs, SDK installation, or the external-consumer boundary.

## What Is Deliberately Missing

There is no rendering hardware interface, render graph, shader system, shader compiler pipeline, graphics pipeline abstraction, mesh system, texture system, material system, descriptor framework, bindless resource model, GPU-driven rendering, ray tracing, ImGui integration, asset system, scene graph, ECS, editor, or game rendering code.

Those systems need requirements from programmable rendering and real scene content. Milestone 2 keeps the graphics layer focused on one result: a correctly synchronized Vulkan frame reaches the screen through the installed Ludus SDK.
