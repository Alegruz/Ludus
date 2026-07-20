# ADR 0001: CMake, Ninja, and Conan 2

## Status

Accepted.

## Context

The engine needs a Linux-first C++23 build that works with Clang, clangd, CTest, sanitizers, local developer scripts, CI, and eventual SDK installation. Dependencies must be pinned and resolved before CMake configure so that configuration and compilation do not access the network.

## Decision

Use CMake with Ninja and CMake Presets for the build. Use Conan 2 to resolve pinned third-party dependencies and generate `CMakeToolchain` and `CMakeDeps` files during bootstrap. Use Python 3 scripts as the cross-platform orchestration layer.

## Consequences

CMake remains target-oriented and short enough to inspect. Conan is kept out of configure and build steps, which makes offline builds predictable after bootstrap. Ninja gives fast local and CI builds. The cost is one required bootstrap step before normal development commands.
