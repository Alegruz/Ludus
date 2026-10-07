# Game world validation

Local implementation checks, 2026-10-03. See the
[reference guide](../examples/world-demo.md) for setup, controls and limits.
The checks use the pinned native and browser toolchains in repository config.

| Check | Result |
| --- | --- |
| Development native build, warnings as errors | Passed with the graphical reference enabled. |
| Native CTest suite | 37 checks: 35 passed; two opt-in live Wayland/keyboard fixtures skipped. |
| ASan/UBSan CTest suite | 30 checks: 28 passed; the same two live fixtures skipped. |
| Profile/Release world builds and tests | Passed with assertions disabled and warnings as errors; four world checks in each policy. |
| Browser build and CTest contracts | Passed; 15 checks passed. |
| Chromium WebGPU scene and controls | Passed with SwiftShader; actual selected backend and player/guard/exit pixels checked. |
| Chromium WebGL 2 Auto fallback | Passed with SwiftShader after hiding WebGPU; selected backend and scene pixels checked. |
| Pinned format/tidy and include boundaries | Passed; the final fault-trace edits also passed focused sanitizer-profile tidy. |
| Installed SDK consumer | Passed; exported GameplayWorld headers/target and storage exercise verified. |
| Native graphical/headless reference | Passed: 120 headless ticks and ten Vulkan frames on Intel UHD Graphics 620. |

Both browser runs use Playwright 1.55.1 and bundled Chromium
140.0.7339.186. The harness verifies pause, exactly one neutral step, held
movement, restart, focus cancellation and narrow resize, and captures scene and
narrow screenshots. The inspected screenshots show the player, guard, exit and
static floor. Both runs report no page or console errors. These are local
software GPU checks; physical browser GPUs and a public hosted build were not
validated.

The allocation tests intercept storage allocation in a separate native executable.
They inject each of the 26 candidate storage allocation failures, preserve the
active world, and verify prepared ticks, command commit and render extraction
perform no storage allocation. Sanitizers run in separate executables so the
allocation interceptor does not replace sanitizer hooks.

## Build-time budget

A local clean development profile with `CMAKE_BUILD_PARALLEL_LEVEL=2` reported
734.4 seconds of summed frontend parsing against the 170-second aggregate limit.
The reported project headers passed their existing limits. The profile preceded
the final instrumentation and fault-trace test edits; CI measures the submitted
commit on the calibrated runner. No budget or warning rule was relaxed.
The calibrated CI job subsequently passed on PR commit `caab92eb3f74`: 142.4
seconds of frontend parsing against the unchanged 170-second limit. See the
[CI budget log](https://github.com/Alegruz/Ludus/actions/runs/37109322822/job/111164101732).
The local result demonstrates why these absolute budgets must be interpreted on
the calibrated runner. Each later PR commit still requires its own CI pass.

```bash
CMAKE_BUILD_PARALLEL_LEVEL=2 ./scripts/check-build-budget --profile
```

Profiling deletes its build directory. Run it separately from tests, SDK
installation and static analysis. Disable `LUDUS_ENABLE_TIME_TRACE` when configuring
for clang-tidy; the profiling-only flag is not accepted by that syntax-only tool.

## Scope

The installed module provides entity identity and typed component storage. The
reference owns game-specific recipes, bounded JSON, incremental candidate loading,
phased simulation, completion tickets, diagnostics and procedural rendering.
Fiber scheduling, worker pools, dependency graphs, render threads, general sprite
or mesh rendering, external media leases and replay files remain future features.
