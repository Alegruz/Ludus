# Project live reload evidence

The implementation remains **draft and incomplete**. This ledger supersedes the
initial Kiro ledger: its headless runs and two reused module paths did not prove
native rendering, complete checkpoint equality, bounded distinct-image residency,
or actual debugger single stepping. The committed requirements remain unchanged.

## Local takeover and current-main integration, 2026-10-03

The implementation checkout incorporates main
`ab4133481ea1bf47c19177b961c93b541d822df0` (project creation/setup repair), on top
of local implementation commit `9e31ea16d3ee` and the preceding release integration.
Private work remains preserved in stashes; the primary workspace is untouched.
Results concern this merged implementation tree, rather than the old remote PR
head `cdd2e48e65509898cdab235e060c9a88ab3e588a`. Final published-head checks and
mergeability must be verified after publication.

The Qt editor now owns the persistent play actor, an independent generation
build lane, copied inspector values, session undo and tuning-document controls.
Source reload is opt-in, debounced and coalesced during builds. Metadata-only
open uses the shared read-only setup checker. Explicit creation/repair and play
builds select the same owned CMake/presets as the canonical SDK workflow. The
single build/setup adapter is independent of the persistent play actor; setup
repair cannot mutate SDK settings during play.

The current asset is a real host-owned **frame-clear configuration** using
public RHI SetFrameTarget. Its immutable cooked payload is copied between frames;
submitted GPU work retains no pointer to it. Texture/model/audio/shader importers
remain unsupported. Reserved Uniform fields are not a rendered shader effect.

| Validation | Actual result and scope |
| --- | --- |
| Debug/Development/ASan-UBSan warning-clean builds | PASS, with pinned tools; Development Editor ON, other two Editor OFF |
| Full Debug/Development/ASan-UBSan CTest | PASS, 40/39/33 registered tests respectively; two explicit live platform SKIPs in each, not acceptance passes |
| Final affected runtime cases after retirement repairs | PASS, Debug 7 tests including real GDB; Development 6 runtime tests; ASan-UBSan 6 tests |
| Actual worker-thread reload/Stop safety | PASS: live worker rejects reload until join; undrainable Stop leaves instance/services/image resident in a child process |
| Persistent native supervisor | PASS: actual host pause/Step/reload, copied properties, tuning apply/undo/redo/save, source supersession, invalid configuration retains old asset, native Update/destructor crashes, hang and parent EOF recovery |
| Python regression suite | PASS: 290 tests, four explicitly skipped environment-dependent cases; live tooling tests use pinned CMake/Ninja |
| Full Development format/tidy/foundational boundary | PASS on the final merged tree with pinned Clang 18 and four bounded tidy workers |
| Installed SDK consumer | PASS; no Qt export dependency introduced |
| Native Wayland editor journey, legacy provider | PASS: 39 assertions; actual installed SDK external build, property undo/redo/save, automatic body-edit reload while paused, RHI configuration replacement, Stop/reopen and saved authored value |
| Native Wayland editor journey, canonical v2 resolver | PASS: 53 assertions, saved local override/unresolved lock, body-edit reload, unsupported checkpoint-schema rejection preserves A, native configuration replacement, reopen and close during build without a protocol error |
| Current-main integration, Debug/ASan-UBSan full CTest | PASS: 41/33 registered tests, respectively; two explicitly skipped live platform cases in each |
| Actual Editor creation/setup and play regressions | PASS: 46 cases/277 assertions, with the installed SDK New Project configure/build/test enabled; no setup skip |
| GDB launch and attach | PASS: separate tests, A already loaded before attach, actual source stops, arguments/locals/stacks/next in A/B and zero inferior exit; no ptrace policy changes |
| Native current-main setup/play/switch journey | PASS: 127 assertions, real repair, ten warm reloads and ten edits, schema rejection, configuration replacement, A reopen, switch to independent B, close during build, and copied host PID/argv/cwd/SDK diagnostics |
| Final Qt Play regressions under ASan/UBSan | PASS: 20 assertions in three cases, including unexpected owner teardown and explicit recovery; callbacks retire before owned buffers are destroyed |
| Post-commit retirement uncertainty | PASS with the actual supervisor/host in Debug and ASan/UBSan: B commits before retirement failure, B identity and both generation leases remain, Resume rejects, explicit Stop confirms cleanup, and saved tuning stays unchanged |
| Canonical repaired-preset integration | PASS: 9 affected Python tests, including actual CMake preset discovery and selecting the owned CMake/presets for play |

The first published takeover head `e72f1b78fcf4a032640dd80e7b153d51cd5e8036`
has the exact validated tree `8195ca5ac5f6d3cf8562c546b89d1347d64dc239`.
Its native CI passed Debug/GDB/supervisor, Development/SDK, sanitizer, optional
editor, PCH, analysis and build-budget jobs; browser compile/package and host
tooling jobs passed. The Release assertion-policy job failed because its actual
publication acceptance requires embedded symbols and optimized fixtures omitted
them. The repair retains debug information in test-only module fixtures and the
thin reference host when tests are enabled, leaving optimization and the full
acceptance enabled. The previously failing real Release supervisor acceptance
passes locally after that repair. Release remains unavailable for editor reload.

Debugger guards now inspect traced stopped threads, including workers while the
leader runs. The supervisor rejects a reload both before Query and if the host
stops during Query; it releases the rejected candidate lease without queuing a
later unload. Actual GDB launch/attach source stops in both A/B exercise the
supervisor rejection with the real inferior PID. Editor notifications disable
live edits/reload/native simulation controls until debugger Continue. These are
guards for an attached debugger, not an editor debugger launcher. Final affected
analysis passes, the final editor play cases pass 35 assertions in both
Development and ASan/UBSan with strict Catch2 timeout failures, and the pinned
Python suite passes 292 tests with four explicit environment skips. Final-head
CI remains required after publishing these repairs.

Raw local logs are retained under `/tmp/ludus-live-reload-*.log`, with final
runtime logs named `debug-runtime-final`, `asan-runtime-final` and
`focused-final`. Native Qt hardware acceptance is an explicit hidden test,
separate from default offscreen CTest. Its invocation fails if native Qt/SDK
prerequisites are absent; default unit test success does not claim hardware
acceptance:

```bash
QT_QPA_PLATFORM=wayland LUDUS_SDK_PREFIX=/absolute/path/to/sdk \
  out/build/linux-clang-development/apps/editor/ludus_editor_tests '[.live-journey]'
```

The final sanitizer stress log contains 150 unique-path attempts: 100 accepted,
50 rejected, one peak mapped generation and one live fixture allocation. Reload
p50/p95 is 422.5/637 microseconds on this host; first/peak RSS is
26,984,448/37,982,208 bytes. Raw records are in
`/tmp/ludus-live-reload-asan-metrics-final.jsonl`. These timings cover the host
transaction, not cold build/publish/property or GPU synchronization latency.

The longer native journey exposed a copied-property readiness race after reload:
editing could be enabled before the replacement snapshot arrived. The controller
now starts refresh before publishing edit readiness and requires a copied value.
The failure also exposed teardown callbacks accessing destroyed Qt owner buffers;
both process owners now retire those callbacks before member destruction.

The supervisor also distinguishes commit outcome from retirement status. A
RestartRequired result after B commits retains B as active and keeps A/B symbol
leases until confirmed process exit. The editor blocks edits/reload and permits
explicit Stop only through its still-trusted actor connection. Unknown actor
ownership continues to block automatic recovery.

[Native metrics](project-live-reload-evidence/native-editor-metrics-2026-10-03.json)
record a cold project-B build phase of 1.643 s, configure 2.853 s, publication
0.541 s and total request-to-publication 12.106 s. Ten warm no-change requests
measure total p50/p95 10.5285/14.596 s, including SDK validation and Qt dispatch;
publication alone measures 0.3525/0.524 s. The **under-1-second warm iteration
target is missed**. Ten copied-property round trips measure p50/p95
126.5/147 ms with a 20 ms observation interval. These paused-session round trips
include the acknowledged edit and refreshed values; they do not prove the
separate two-running-frame ack target or pixel-visible latency. Static analysis
and other validation ran concurrently, so this is a contended run rather than
an idle benchmark. Validation/identity checks have not been weakened.

The [final native run](project-live-reload-evidence/native-editor-final-metrics-2026-10-03.json)
with all 127 assertions also overlapped static analysis and sanitizer compilation.
Its warm total p50/p95 is 13.4675/22.025 s, publication 0.3945/0.777 s,
and copied-property round trips 103/116 ms. The cold project-B total is 4.198 s.
Both raw runs are retained; neither is an idle or pixel-visible latency claim.

[Raw sanitizer records](project-live-reload-evidence/asan-reload-metrics-2026-10-03.jsonl)
retain the 100 accepted/50 rejected reload observations described above.

Still incomplete: editor-integrated debugger launch/attach, native pixel/visible
latency evidence, idle iteration measurements, and final published-head
CI/mergeability. Windows DLL and macOS
loader backends and unsupported resource importers are outside the first host
matrix. The PR stays draft; unchecked acceptance work is not counted as passed.

## Recorded local repair validation, 2026-10-02

The repair checkout starts at PR #55 head
`d70127cd589fa855b1c1f47e33fee4ab04f9295c` and integrates main
`1ff05dcc00a0494cd45b19bff92c262b6c414d66`. Results below include the local repairs;
they are not checks for the old remote head or a claim that every feature is ready.
Revalidate the published head and inspect its actual constituent CI steps.

Tools: Ubuntu 24.04, x86-64, Clang/format/tidy 18.1.3, CMake 3.29.6, Ninja
1.11.1.3, Conan 2.8.1, GDB 15.1. Native Wayland/Vulkan uses the Intel UHD 620.
The primary checkout is preserved; validation uses an isolated implementation
checkout and copied Conan cache. The local execution sandbox blocks private
socket `send`/`setsockopt` and display connections, so native socket/process tests
run outside that sandbox. No test expectations are weakened for this restriction.

| Check | Actual result and scope |
| --- | --- |
| Debug build, warnings as errors | PASS |
| Full Debug CTest | PASS, 38 registered tests; two live Wayland/keyboard tests explicitly SKIPPED, not acceptance passes |
| Development with Editor ON, warnings as errors | PASS |
| Full Development CTest | PASS, 38 registered tests; the same two live platform tests SKIPPED |
| Full ASan/UBSan CTest | PASS, 32 registered tests; the same two live platform tests SKIPPED |
| Latest runtime Debug tests | PASS, six tests including real GDB journey |
| Latest runtime ASan/UBSan and public header checks | PASS, seven tests |
| Python regressions | PASS, 227 tests, four explicitly skipped environment-dependent tests; 35 real process tests also pass outside the sandbox |
| Installed Development SDK closure/consumer | Dependency bundling and path audit PASS; consumer PASS outside the local private-socket restriction |
| External `examples/live-edit-game` | Module, host and shipping build against the installed SDK; compilation database contains game and thin entry sources, no engine implementation sources; static headless run PASS |
| Native window | Actual dynamic host completed 60 Wayland/Vulkan frames and a separate pause/Step/reload/Resume/Stop journey; frame presentation and tick preservation asserted. No pixel readback or editor integration acceptance claimed |
| GDB | Actual A and B source breakpoints in one inferior, host stacks, arguments, state locals, `next`, distinct image mappings, zero inferior exit PASS |
| Formatting/foundational boundary | PASS at repair checkpoints; recheck final diff |
| Development analysis with Editor ON | Full `scripts/check linux-clang-development --all` PASS after repairs, using pinned tools and four bounded analysis workers |

The copied-property protocol now passes actual socket tests in Debug and
ASan/UBSan. Tests assert rejected multi-property batches leave every value and
revision unchanged, a successful batch advances one revision, lost-reply retries
do not apply twice, stale edits return current snapshots, and old schema or
module-generation requests reject after reload. Property records have explicit
typed fields; float values use IEEE binary32 bits on the wire, with ordinary
numeric controls intended for the editor. The codec accepts bounded arrays of
flat records and valid UTF-8, including JSON surrogate-pair escapes, while
rejecting deeper nesting, nonfinite property values and malformed UTF-8.

Pure Python play-sidecar and tuning-document owners are implemented and tested
but not yet wired into the editor. Opening is metadata-only; explicit document
edits are atomic batches, dirty until Save, with independent undo and exact
draft/disk revision conflicts. Failed replacement preserves the saved file.
Shared process supervision now retains uncertain leaders rather than entering
a blocking reap or equating an uninspectable process group with no descendants.
Three regressions enforce those uncertainty paths.

Earlier full Python runs under concurrent analysis/build load missed an existing
real-process test's terminal-event deadline. An idle full run passes in nine
seconds; exact-head CI still needs observation before treating the runner timing
as resolved.

The GDB journey stops in A before reload and proves A's mapping remains present
while its frame is stopped. Reload executes after that frame resumes; the next
source stop resolves in B with preserved simulation state. This does not yet
prove a debugger-aware editor supervisor, debugger attach, destructor crash
recovery, or RAD/LLDB acceptance. Those remain incomplete.

## Distinct generation stress measurements

The stress test copies each attempt to a never-reused path/inode, performs 100
successful replacements and 50 injected validation rejections, and measures
`/proc/self/maps`, resident pages and elapsed reload time. It checks one live
host allocation (the fixture instance), at most three mapped generation images,
and at most 64 MiB RSS growth. It separately proves uncertain retirement prevents
further gameplay and reload while code and service contexts remain pinned.

| Native test mode | Peak generation mappings | First/peak RSS bytes | Successful reload p50/p95 |
| --- | --- | --- | --- |
| Debug | 1 | 5,713,920 / 5,713,920 | 84 / 147 microseconds |
| ASan/UBSan | 1 | 17,629,184 / 28,622,848 | 463.5 / 2,851 microseconds |

These are headless runtime transaction measurements, not end-to-end editor or
GPU synchronization timings. Sanitizer RSS includes instrumentation/cache costs.
Raw per-attempt JSON lines are produced by setting `LUDUS_RELOAD_METRICS`; CI's
sanitizer job retains those measurements as an artifact. Missing measurements
are not reported as successful acceptance.

```bash
LUDUS_RELOAD_METRICS=/tmp/live-reload.jsonl \
  out/host-tools/venv/bin/ctest --preset linux-clang-asan-ubsan \
  -R game_host_reload --output-on-failure
```

## Concrete repairs

- Generation service tables live at stable heap addresses across ownership moves.
  Host allocations are tracked by owner and intrusive ledger. Uncertain code,
  service context and allocation ownership stays pinned until process exit;
  `CleanupUnknown` blocks Update and mutation rather than being resumed as play.
- Checkpoints use explicit bounded little-endian tagged fields, not native struct
  padding. Full supported state compares across A/B migration. Malformed tags,
  duplicate/missing fields, truncation, non-finite values and mismatched envelope
  identities are rejected before promotion.
- Control output is nonblocking and bounded without dropping partial frames.
  Step outcomes are retained until acknowledgment and identical retries do not
  advance another simulation tick. Stop remains responsive under backpressure.
  Protocol/session/epoch and numeric-overflow validation are enforced.
- Publication includes the matching host, checks sources after copying, uses a
  cross-process persistent sequence, validates a closed manifest and rejects
  escaped/symlink/missing payloads. OS-backed shared leases prevent collection
  by another supervisor. Collection shares the publication lock and never calls
  a failed deletion successful.
- Host and module compatibility include SDK source revision, variant/sanitizers,
  runtime ABI, full compiler version and target. Dynamic and static executables
  use one typed runtime entry and presentation loop; no incompatible function
  pointer casts or duplicate installed host parser remain.
- Windowed sessions now call actual Platform/RHI APIs and expose presentation
  evidence separately from module startup. Unsupported window/graphics startup
  returns a capability error. Gameplay input uses the existing Input reducer.

## Incomplete acceptance and next implementation

The persistent editor/Python play supervisor, separate Build/Play lanes,
metadata-to-manifest agreement, debugger-aware cleanup, source watcher ordering,
Qt inspector, wiring authored tuning documents/undo/conflicts, and actual supported
resource-backed asset replacement still need implementation and acceptance.
Native rendered property/code/asset changes, native crash/hang recovery,
full callback/resource retirement fixtures, debugger attach/stop integration,
SDK relocation for the gameplay sample, build budgets and affected browser
regressions remain required. Do not mark the phase checklist or PR ready from
this repair ledger. No merge, tag or release is authorized.
