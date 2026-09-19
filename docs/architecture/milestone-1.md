# Milestone 1 Architecture

Milestone 1 is a minimal native runtime shell, not a renderer. It proves that a separately maintained application can bootstrap a pinned Ludus revision, consume the installed SDK, create a native window, run an engine-controlled frame loop, observe time and basic input, process platform events, and shut down cleanly.

## External Sandbox Repository

`Ludus Sandbox` lives in a separate repository and is the first human-facing consumer of the engine SDK. Its onboarding script acquires a pinned Ludus revision into generated output, initializes Ludus, installs the required SDK, and configures the sandbox without building the sandbox itself.

The sandbox consumes Ludus only through the installed package boundary. It uses `find_package(Ludus CONFIG REQUIRED)`, links exported Ludus targets, and includes installed public headers. It does not use `add_subdirectory()` on the engine or reach into Ludus private source paths.

Machine-specific SDK and tool paths are generated locally rather than committed. CMake presets remain the project interface, while generated user presets bind the sandbox to the Ludus-managed CMake, Ninja, Clang, and installed SDK for the current checkout.

## Runtime Libraries

Milestone 1 introduces only the runtime structure required by the sandbox. The intended dependency direction is:

```text
FoundationBase
    ↓
Platform
    ↓
Application
```

The platform layer owns native window creation, event polling, monotonic timing, keyboard state, mouse state, and basic window state changes. SDL3 is an implementation dependency of Ludus and does not appear in the sandbox API.

The application layer owns the primary window, frame progression, event processing, frame timing, and orderly shutdown. Public APIs should keep ownership and control flow explicit rather than hiding runtime behavior behind global state or framework-heavy entry-point machinery.

## Installed Runtime SDK Boundary

The runtime must remain usable through the same installed SDK discipline established in Milestone 0. After onboarding, `Ludus Sandbox` must configure independently against the installed Ludus package and must not require source-tree coupling to build.

Running the sandbox should create a 1280×720 native window titled `Ludus Sandbox`, enter a continuous frame loop, expose frame delta time and basic keyboard/mouse state, handle resize/minimize/restore/close events, and terminate cleanly. Normal startup, interaction, resize, and shutdown should remain clean under the supported sanitizer configuration.

Milestone 0 validation remains intact: bootstrap, build, tests, checks, sanitizers, SDK installation, and the in-tree external-style SDK consumer must continue to pass.

## What Is Deliberately Missing

There is no Vulkan, OpenGL, Direct3D, Metal, rendering hardware interface, swapchain, shader system, render graph, mesh system, texture system, material system, ImGui integration, asset system, ECS, scene graph, serialization, reflection, job system, custom allocator, logging framework, plugin system, hot reload, scripting, audio, physics, editor, or game code.

Those systems need requirements from an actual runtime consumer. Milestone 1 keeps the application shell small and explicit so the next milestone can introduce graphics from a working external application rather than from speculative engine abstractions.
