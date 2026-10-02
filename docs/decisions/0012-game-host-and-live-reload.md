# ADR 0012: Separate GameHost and explicit gameplay replacement

## Status

Accepted design; implementation and acceptance pending in
[project-live-reload](../../.kiro/specs/project-live-reload/tasks.md).

## Context

The optional editor and installed SDK currently support separate executable
play. Editing C++ while playing requires a code lifetime and state contract;
loading a DLL alone cannot preserve arbitrary C++ objects or recover from native
crashes. The editor must preserve authored documents and remain debuggable.

## Decision

Keep gameplay in a separately supervised GameHost. Keep engine libraries static
inside that host. Replace one native gameplay module through a narrow versioned
function table and host service API. Publish immutable module/symbol generations,
stage explicit checkpoint migration while retaining the old instance, and commit
at a quiescent frame boundary before retiring old code references.

Use copied stable-ID property schemas and conditional commands for live tuning,
with explicit Apply to Document and undo/conflict behavior. Asset replacement is
a distinct resource operation. SDK/ABI changes restart the host. Shipping targets
statically dispatch the same game implementation. First support Linux x64 under
the reference toolchain; keep browser build/page reload.

The normative [design](../../.kiro/specs/project-live-reload/design.md) defines
lifecycle, allocation, staging, process, compatibility and boundedness rules.
The [research](../architecture/project-live-reload-research.md) records actual
Gems reading and current primary references. This extends ADR 0002 with a
specific optional gameplay-module exception; it does not make internal engine
modules shared or broaden the project-sdk-workflow milestone.

## Consequences

The editor survives gameplay failures and can build while playing without
overwriting active images. A small explicit API supports maintainable tests and
debugger-visible generations. Game authors must supply intentional checkpoints,
property declarations and migrations, and obey cooperative reload-safe services.
Unsupported side effects/resources require restart. Native crashes cannot be
rolled back. Arbitrary binary patching, automatic C++ reflection, embedded
viewport rendering and dynamic Wasm remain separate decisions.
