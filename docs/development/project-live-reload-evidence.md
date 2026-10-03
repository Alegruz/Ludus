# Project live reload evidence

The implementation remains **draft and incomplete**. This ledger supersedes the
initial Kiro ledger: its headless runs and two reused module paths did not prove
native rendering, complete checkpoint equality, bounded distinct-image residency,
or actual debugger single stepping. The committed requirements remain unchanged.

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
