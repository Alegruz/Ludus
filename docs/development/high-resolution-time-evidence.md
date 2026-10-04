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
| Browser Development full build | Passed with warnings as errors. |
| Browser observability semantics and four standalone-header checks | All five passed, including FoundationTime and the shared profiling epoch. |

The full `./scripts/check linux-clang-development --all` and browser profile
checks are required alongside these results; their final status is recorded on
the PR. Native process/socket tests and LeakSanitizer require ordinary OS access.
The restricted sandbox denied socket operations and LeakSanitizer process
inspection; rerunning the full native suites and SDK workflow with that access
passed. Display-dependent Wayland tests remain skipped on this headless host.
