# ADR 0002: Static Libraries as the Internal Default

## Status

Accepted.

## Context

The engine is early. The first priority is reliable infrastructure and a clean module boundary, not runtime plugin loading or ABI stability.

## Decision

Build internal engine modules as static libraries by default. Export installed CMake targets for SDK consumers, starting with `Ludus::FoundationBase`.

## Consequences

Static libraries keep Milestone 0 simple to debug, test, sanitize, and install. They avoid premature decisions about binary compatibility, symbol visibility, plugin lifetimes, and dynamic loading. If future runtime requirements justify shared libraries or plugins, that decision can be made with concrete constraints.
