# Project live editing requirements

Status: proposed implementation, 2026-10-02. Implement with AGENTS.md/steering,
[design](design.md) and [tasks](tasks.md). L01-L16 are normative. File paths,
commands and APIs in this package are deliverables, not existing capabilities.

## L01 Project lifecycle

Open SHALL parse/validate project and optional play metadata without executing
game/project code or initiating setup. Failed open SHALL preserve the current
workspace. Switch/close SHALL resolve dirty documents, cancel owned work and
confirm process cleanup before releasing the workspace or advancing its epoch.
CleanupUnknown SHALL block switch/new play until explicit recovery.

## L02 Process and dependency ownership

Editor SHALL execute gameplay only in a supervised separate host process. Host
SHALL own engine lifecycles/window/services; module SHALL own its game instances.
Runtime/game exports SHALL remain Qt-free. Engine libraries SHALL remain static
by default. The module SHALL not create a duplicate engine singleton runtime.

## L03 Compatibility and opt-in support

v1 project executable workflows SHALL remain usable; new play metadata SHALL be
optional and closed/versioned. Native Linux x64 Clang 18 Debug/Development SHALL
be the first supported reload profiles. Unsupported platforms/features SHALL
report capability errors. Existing browser static build/package/test paths SHALL
remain intact. SDK/ABI/toolchain/policy changes SHALL require a new host session.

## L04 ABI and allocation

One unmangled entry SHALL negotiate caller-owned versioned POD tables with
explicit statuses and noexcept callbacks. Boundary objects SHALL follow the
lifetimes, aliases, memory and exact compatibility rules in design sections 2/6.
Wrong ABI/table/SDK/architecture/runtime/policy/sanitizer identities SHALL fail
before game Create. Query metadata and published metadata SHALL agree. Module
objects SHALL be destroyed by their owning code before releasing that image.

## L05 Immutable artifacts

Build SHALL use validated CMake File API artifacts, verify actual post-build
identity and atomically publish complete module/symbol/manifest generations.
Failed/cancelled/superseded builds SHALL never activate stale binaries. Active
and debugger-leased payloads SHALL not be overwritten or collected. Building
while playing SHALL use one build writer and independent session leases.

## L06 Watching and request ordering

Auto-build/reload SHALL be opt-in, bounded and debounced. Watcher notifications
SHALL be hints; publication SHALL require successful build/input validation.
Only newest eligible build results for the current epoch/session/SDK/target
SHALL activate. Stop SHALL prevent late spawn/commit. Source-input instability
SHALL be visible and SHALL not be mislabeled as a reproducible/latest build.

## L07 Safe reload

Reload SHALL occur at a safe host-frame boundary after generation dispatch is
gated and all tracked work is drained. Old module/instance SHALL remain available
until candidate acceptance. Candidate services SHALL isolate active mutation and
side effects. Commit SHALL perform an allocation-free, non-failing prepared host
swap. Ordinary pre-commit rejection SHALL preserve old state/resources and resume
the prior pause state. Unsupported resource semantics SHALL require Restart.

## L08 Unload and hard failures

All callbacks/jobs/destructors/views/instances SHALL retire before the loader
reference is released. Native crash/hang SHALL be reported as host failure,
never caught as a reload status. Uncertain retirement SHALL disable further
reload and require restart. Logical retirement SHALL not be described as proof
of OS unmapping. Repeated reload SHALL have bounded live resources/residency.

## L09 Checkpoints and migration

Reload state SHALL use bounded tagged/versioned records with stable IDs,
validated sizes/digests and logical resource references. Raw C++ memory dumps,
vtable/function/OS pointers and compiler field ordering SHALL be prohibited.
New code SHALL explicitly accept or migrate old schemas; incompatible migrations
SHALL preserve the active game and offer explicit Restart. Simulation/random
state and intentionally reset caches SHALL be documented and exercised.

## L10 Live property mutation

Inspector SHALL use copied schema/values and stable object/property IDs, never
runtime addresses. Typed conditional edits SHALL validate generation/schema/
revision/ranges and be all-or-none at a safe boundary. Results SHALL return actual
values/revision. Simulation writes to editable values SHALL invalidate stale
revisions. Schema reload SHALL invalidate cached runtime plans/selections only
where IDs cease to exist, with explicit diagnostics.

## L11 Authoring safety and undo

Saved documents, dirty drafts and simulated values SHALL remain distinct. Play
edits SHALL be session-only by default. Apply to Document SHALL be an explicit
selected persistable-field command with revision checks, then ordinary Save.
Stop/reload SHALL not auto-save runtime state. Document undo/redo SHALL survive
play/reload; runtime undo SHALL be conditional and report conflicts. Failed
atomic save/external file conflict SHALL retain the draft and old disk content.

## L12 Assets

Source/cooked/runtime assets SHALL have distinct ownership. A supported asset
replacement SHALL validate an immutable artifact before a safe handle swap and
defer old resource disposal until outstanding users complete. Failed import/
validation SHALL retain the old asset. Unsupported asset kinds SHALL be explicit;
this phase SHALL not invent texture/model/audio importers or a full scene/ECS.

## L13 Protocol and budgets

Versioned local asynchronous channels SHALL separate control from logs, bound
frames/queues/schema/properties/checkpoints and tag events with current identities.
Duplicate IDs SHALL have defined retained-result semantics; ambiguous results
SHALL reconcile via Status/reads, never blindly replay. Stop SHALL not starve.
EOF/supervisor loss SHALL use existing confirmed-cleanup/CleanupUnknown rules.
Debugger-paused/unresponsive hosts SHALL not be killed merely for heartbeat loss.

## L14 Debugging

Debug Game SHALL target the host, preserve generation-specific symbols/source
identity and support manual reload/restart. Reload SHALL not unload a module
while execution is stopped inside it. Actual stepping, symbol replacement,
breakpoints, pause/step/continue and crash cleanup SHALL have recorded native
acceptance with a supported debugger. RAD SHALL remain optional.

## L15 Standards and validation

All production/game targets SHALL use C++23/no exceptions/pinned warning,
format/tidy/include/header/build-budget policies. Installed SDK exports and
headless tooling SHALL exclude Qt/editor internals. Debug/Development,
ASan/UBSan, Editor ON/OFF and external SDK consumers SHALL pass applicable gates.
Tests SHALL inject real load/build/migration/IPC/process failures and exercise
100 reloads; mocks alone SHALL not prove loader, debugger or rendered behavior.

## L16 Evidence and PR completion

Evidence SHALL state exact revisions/tools/commands/results, skips/blockers,
iteration timings and resource behavior. Kiro SHALL implement in one dedicated
branch, publish/update one implementation PR to main, repair CI failures and
merge conflicts, and verify every applicable check on its final head. Required
checks SHALL not be weakened, skipped or hidden to make a green summary.
Incomplete acceptance SHALL leave the PR draft with concrete blockers.
