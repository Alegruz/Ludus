# ADR 0016: Target detection and runtime capability ownership

## Status

Accepted.

## Context

FoundationBase already owns OS/architecture and compiler macros (ADR 0007).
The old dispatch classified every Apple target as macOS, silently omitted unknown
targets, and allowed contradictory build/backend inputs. Modules repeated raw
OS checks. Debugging target assumptions required manually inspecting those macros.

## Decision

Keep one compiler-authoritative, macro-only detection boundary in `config.h` and
`compiler.h`. Add numeric selectors while preserving active-only compatibility
flags. Classify mobile Apple/Android separately, record pointer width and byte
order, and diagnose unrecognized facts or forbidden overrides. Keep frontend
identity separate from Microsoft ABI compatibility and query compiler extensions.

Apply CMake's expected **target** OS through project options and export it through
FoundationBase; check it against compiler detection in the engine and installed
SDK consumers. Do not generate host-derived architecture headers. Keep backend
selection and consistency checks
in Platform; keep runtime capability ownership in the relevant subsystem.

Expose an explicitly included `target.hpp` with constexpr enums and an immutable
descriptor for ordinary code and startup diagnostics. Keep it out of `core.h`.
Enforce dispatch with cross-target positive/negative compile tests and prevent
first-party engine/app code from repeating raw OS detection.

The [design and literature review](../architecture/platform-detection.md) defines
recognition, validation limits, future runtime query contracts, and extension rules.

Complete and validate its [initial implementation completion gate](../architecture/platform-detection.md#initial-implementation-completion-gate)
before the broader [conference and journal research pass](../architecture/platform-detection.md#conference-and-journal-research-backlog).
That backlog records queued sources and proposed trials; it does not change the
accepted detection boundary or establish adoption of those ideas. Preserve the
validated revision and measurements as the comparison baseline for later work.

## Consequences

Existing `#ifdef` callers retain their meaning. Unknown targets now fail early and
require an explicit port instead of inheriting a misleading desktop classification.
Named target facts cost no runtime probing/allocation and add no heavy headers.
Recognition of Windows/macOS/mobile/wasm64 does not add or validate their engine
backends; existing CMake build restrictions remain in force. Actual device,
permission, and instruction-set availability still require subsystem-owned queries.
