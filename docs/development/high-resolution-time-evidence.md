# FoundationTime validation

October 4, 2026. Scope: shared monotonic clock, checked arithmetic and owner-local
stopwatch/deadline/frame helpers. Timing samples describe this shared Linux host;
they are not a portable latency or physical-resolution guarantee.

## Research

The initial architecture preceded the local Gems chapter review. The final
[architecture](../architecture/high-resolution-time.md) records selection,
printed/PDF pages, adapted ideas and departures. All three selected chapters were
read; clock figures were visually checked. No historical source was copied.
Source acknowledgments appear at the affected implementation boundaries.

## Performance and dependencies

A Clang 18 C++23 `-O2 -fno-exceptions` probe compared the previous out-of-line
steady-clock implementation against the linked production FoundationTime archive.
Each row is nine trials of one million clock reads with an observable XOR result.
Values include loop/call overhead; builds were running concurrently on the shared
host. Sequential order and load make these unsuitable for a speedup claim.

| Read path | Median ns/read loop | Min | Max |
| --- | ---: | ---: | ---: |
| Previous steady_clock | 120.99 | 86.31 | 141.43 |
| FoundationTime | 105.89 | 81.19 | 112.95 |
| Previous steady_clock, repeat | 110.21 | 88.28 | 128.66 |
| FoundationTime, repeat | 126.71 | 98.63 | 160.75 |

The ordering of medians reverses on repetition. This establishes no reliable
performance improvement or regression. The backend remains the same OS-backed
steady clock; centralization removes duplicate implementations and makes timer
policy independently testable. Direct TSC is not justified by these data.

`nm -C -u libludus_foundation_time.a` shows only the module's own checked-time
functions and `std::chrono::_V2::steady_clock::now()` as unresolved dependencies.
No allocator, logger, mutex or registry dependency appears in the archive.
This is a dependency/code inspection, not a runtime allocation-interception test.

## Validation commands and results

Local integration validation used the primitive-types base `69ede23` plus this
implementation. The published branch incorporates newer upstream work; required
CI checks validate the exact PR commit, including static analysis and the build
budget. Native configuration uses pinned Clang 18, CMake/Ninja and warnings as
errors. Browser configuration uses pinned Emscripten 4.0.23. The selected CMake's
actual configure/build/test preset listings were checked.

| Validation | Local result |
| --- | --- |
| Development full build and `ctest --preset linux-clang-development --no-tests=error` | Passed: 48 registered tests, two display-dependent skips. |
| ASan/UBSan full build and `ctest --preset linux-clang-asan-ubsan --no-tests=error` | Passed: 42 registered tests, two display-dependent skips; no sanitizer findings. |
| `./scripts/install-sdk linux-clang-development` | Passed: installed artifact/dependency audit, standalone consumer build and execution, runtime policy match. |
| `./scripts/check --format` | Passed, including the foundational include boundary. |
| `./scripts/check linux-clang-development --all` | Passed: format/include boundary and all 178 compiled native project sources under Clang 18 static analysis. |
| Browser Development full build | Passed with warnings as errors. |
| Browser Development full CTest suite | All 16 passed, including FoundationTime headers and shared-clock/helper semantics. |

After integrating the platform/editor base `2f602ea`, the warning-clean native
Development build and all 49 registered tests also passed (two display-dependent
skips). The full browser Development format/static-analysis check passed after
correcting the probe formatting and expressing the checked conversion as a
positive range test. Required CI validates the final PR commit across the other
profiles and the build-time budget; no budget was raised for this module.

Native process/socket tests and LeakSanitizer require ordinary OS access.
The restricted sandbox denied socket operations and LeakSanitizer process
inspection; rerunning the full native suites and SDK workflow with that access
passed. Display-dependent Wayland tests remain skipped on this headless host.

## Frame integration

October 16, 2026. Scope: the [frame integration stage](../architecture/high-resolution-time.md#implementation-before-research)
for the world reference and smoke application loops. Each callback reads one
FoundationTime timestamp. The world reference `TickDriver` owns the 250 ms
FrameClock; the smoke application owns a 100 ms FrameClock. Neither driver
applies a second clamp. The float64 tick accumulator is unchanged; the rational
integer driver is the next stage.

Integration tests in `apps/world_demo/tests/simulation_tests.cpp` (`[frame]`)
drive synthetic timestamps at a 2^62 ns epoch without OS sleeps or a mock clock:

| Behavior | Evidence |
| --- | --- |
| Startup | First Advance is a zero-elapsed baseline and runs no tick. Uneven integer partitions of one second accept exactly 1,000,000,000 ns; equal readings are valid zero frames. |
| Accepted/discarded time, no duplicate clamp | A 2 s stall accepts exactly 250 ms and clamps exactly 1.75 s. Catch-up trimming adds less than the accepted 250 ms to the discard total. The next frame measures from the stalled sample. |
| Pause/resume | Paused frames accept nothing; resuming yields a baseline, so paused time is never caught up. Single step still runs exactly one tick. |
| Visibility | Hidden frames and an `Interrupt()` (the browser visibility event while callbacks are suspended) exclude 89.99 s of hidden time with no catch-up tick. |
| Session replacement | Frames during a long load are excluded; activation resets the driver, and the replacement world starts from a baseline at tick zero. |
| Clock regression | A backwards sample adds no time and increments a counter. Recovery measures from the high-water mark. After a lifecycle reset, a regressed sample cannot become a baseline below it. |

For each monotonic sequence, accepted, clamped and lifecycle-excluded totals equal
the observed span exactly. Smoke tests cover its startup baseline, single 100 ms
clamp, hidden reset, return-to-visible baseline and regression.

| Validation (macOS arm64 host, pinned Clang 18, warnings as errors) | Result |
| --- | --- |
| `./scripts/build macos-clang-development` | Passed. |
| `./scripts/test macos-clang-development` | 82 of 85 passed. The Cocoa window suite skips every case without `LUDUS_TEST_COCOA=1` in a WindowServer session, so its runner reports failure. The header self-sufficiency gate exceeded its fixed 120 s timeout under a host load average above 100. Run directly, it passed all 153 public headers in 1 min 42 s. `ludus_target_detection` passed on rerun. |
| World reference with `-DLUDUS_BUILD_WORLD_DEMO=ON` | `ludus_world_demo_tests` (22 cases), allocation, shader layout, smoke simulation and FoundationTime tests passed. `--headless` completed 120 ticks; `--frames 3` presented three native frames through the migrated loop. |
| `python3 scripts/python/source_policy.py --base HEAD` | Passed. |
| `./scripts/check --format` | Passed, including the foundational include boundary and naming scopes. |
| clang-tidy 18 on the changed world reference and smoke sources | No findings. |
| `./scripts/build macos-clang-asan-ubsan` | Passed. |
| ASan/UBSan test execution | Not completed locally. On this macOS 26.6 host the Clang 18 ASan runtime deadlocks in its initializer (`AsanInitFromRtl` spin lock during dyld shared-cache iteration) before `main`, for every sanitized binary. |
| Documentation: `check-docs`, strict MkDocs build | Passed. `check-wiki --documentation-only` reported only the absent generated API reference, which requires the Doxygen bootstrap. |

The Linux reference profile, sanitizer execution, browser presets and build budget
are validated by the pull request's CI tiers.
