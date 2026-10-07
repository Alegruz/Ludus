# Build Profiling

Ludus treats build time as a measured quantity, not a feeling. This page
describes how to profile a build and records the current baseline so future
changes can be judged against numbers.

## How to profile

```bash
./scripts/profile-build linux-clang-profile
```

This performs a **clean** build of the preset (the build tree is wiped first so
runs are comparable) and:

- times CMake **configure** and **build** separately;
- parses Ninja's `.ninja_log` for per-target timings, total wall-time, and
  achieved parallelism (summed compile time / wall time);
- compiles with Clang `-ftime-trace` (via `LUDUS_ENABLE_TIME_TRACE=ON`) and
  aggregates the per-translation-unit traces with
  [ClangBuildAnalyzer](https://github.com/aras-p/ClangBuildAnalyzer) into a
  ranked report of the most expensive headers and template instantiations.

Outputs land under `out/profile/`:

| File | Contents |
| --- | --- |
| `<preset>-profile.json` | machine-readable summary (timings, Ninja stats) — diff this between runs |
| `<preset>-analysis.txt` | ClangBuildAnalyzer ranked report (headers, templates, frontend/backend split) |
| `<preset>-cba.bin` | raw aggregated trace (input to `--analyze`) |

Flags:

- `--no-time-trace` — skip `-ftime-trace`/ClangBuildAnalyzer (Ninja timings only; faster).
- `--ccache` — enable ccache for the profiled build (measure warm-cache rebuilds).

ClangBuildAnalyzer is fetched and built on demand into `out/host-tools/`; it is
**not** part of `./init.sh`, so normal onboarding is unaffected.

## How to improve, quantitatively

1. Run `profile-build` to get a baseline.
2. Change **one** variable (prune an include, add a PCH, enable a lever).
3. Re-run `profile-build` and diff `<preset>-profile.json` and the analysis
   report. Keep the change only if the numbers improved.

Do not adopt an optimization without a before/after measurement from this
harness.

## CI tracking

CI builds with ccache enabled (`LUDUS_ENABLE_CCACHE=1`) and a persistent cache.
Each run records total build wall-time and `ccache --show-stats` (hit rate) in
the job summary, so cache effectiveness and build-time trends are visible per
commit.

The build-budget job uses `CMAKE_BUILD_PARALLEL_LEVEL=2` to keep CPU contention
stable when summing elapsed frontend timings. Reproduce that job locally with:

```bash
CMAKE_BUILD_PARALLEL_LEVEL=2 ./scripts/check-build-budget --profile
```

The previous unrestricted CI profile in [run 36767757513](https://github.com/Alegruz/Ludus/actions/runs/36767757513)
reported 129.2 s frontend time, 32.98 s build wall time, and 5.71 effective
parallelism. All reported project headers passed their existing limits. The
aggregate limit remains 120 s; compare future profiles at the same concurrency
before treating timing changes as include regressions.

## Baseline

Captured with the pinned toolchain (Clang 18, libstdc++ 14, Ninja, lld) on the
`linux-clang-profile` preset (RelWithDebInfo, `-ftime-trace` on, ccache off),
clean build. Absolute seconds are host-dependent — treat the **ratios and the
ranked lists** as the durable signal.

- Configure: ~0.7 s
- Build (Ninja wall): ~5.0 s across 64 edges, ~7.2x achieved parallelism
- Frontend (parsing) vs backend (codegen): ~23 s vs ~10 s summed across TUs

### Top compile-time costs (baseline)

Most expensive headers, by aggregate parse time across all TUs:

| Header | Aggregate | Note |
| --- | --- | --- |
| `foundation/logging/log.hpp` | ~8.3 s (7×, avg ~1.2 s) | pulls in `<format>` → `<chrono>` + Unicode/format machinery; taxes every logging consumer |
| `catch2/catch_test_macros.hpp` | ~2.7 s (4×) | test-only |
| `<chrono>` | ~2.3 s (6×) | dragged in transitively by `<format>` and Catch2 |
| `foundation/logging/src/sinks/file_sink.hpp` | ~1.7 s (2×) | pulls `<filesystem>` |

### Applied optimizations

- **Type-erase `std::format` at the logging boundary** (ADR 0004). `Log(...)`
  now erases to `std::format_args` and calls a single non-template `VLog` in
  `logger.cpp`, so `std::vformat` is instantiated once instead of per TU.
  Measured on `linux-clang-development`: frontend 52.5 s → 22.5 s (−57%),
  backend 21.9 s → 9.0 s (−59%), `log.hpp` aggregate ~19 s → ~9 s.

### Improvement backlog (measure each before/after)

- **`log.hpp` residual cost** is now `<format>` being *parsed* (still included
  for `std::format_string`/`std::make_format_args`) plus transitive `<chrono>`.
  Next lever: a precompiled header for the stable heavy STL headers, or trimming
  the header further.
- **Precompiled header** for the foundational vocabulary is now available
  (`-DLUDUS_ENABLE_PCH=ON`; `cmake/EnginePch.cmake`, ADR 0007). It precompiles
  `core.h` only — a build accelerator, not a place to park heavy STL. Extending
  a PCH to cover `<format>`/`<chrono>`/`<string>` for the modules that consume
  them would be a *separate*, measured decision, and must not turn the PCH into
  the dependency model.
- **`file_sink.hpp`** still pulls `<filesystem>`/`<format>` into a header — move
  those into the `.cpp`.
- **`version.hpp`** is unexpectedly heavy for a version header — IWYU candidate.
- **ccache** warm-cache rebuild time (enabled in CI; watch hit rate).
- **include-what-you-use** pass to prune transitive includes at the root.

These are candidates, not commitments — each must show a win in
`profile-build` output before it lands.

## Assertion test graph budget recalibration

The supplied assertion-branch CI log shows successful compilation followed by
an aggregate-budget failure: **73.8 s summed frontend time against 65 s**. Build wall
time was **18.51 s**. These are different metrics: the frontend number adds
time across concurrent compiler invocations, including tests and dependency
scanning; it is not the developer's elapsed build time.

All project headers listed in that CI report passed their existing limits:
`log.hpp` averaged 2443 ms (limit 3200), `file_sink.hpp` 1748 ms (2400),
`version.hpp` 547 ms (2000), `debugger_sink.hpp` 1250 ms (2000), and
`formatter.hpp` 623 ms (2000). The aggregate limit had been calibrated to
approximately 45 s before adding the assertion runtime, unit tests, native and
fake backend death tests, allocation probes, logger-lifetime tests, and build
contract tests. The new assertion headers do not include the heavy standard
headers seen in the report. In particular, `<barrier>` is test-only; its two
expensive inclusions came from compiling the same death-test driver twice.

### Optimization and measurement

Compile `assert_child.cpp` once in the private `ludus_assert_test_driver` object
target and use that object in both child executables. Their compiler flags were
identical before this change and remain identical afterward. The driver does
not link FoundationBase: the native child still links the production archive,
while the fake child still compiles its isolated runtime and fake platform
backend. Both executables retain their own state, native termination, and
complete test coverage. No runtime, public API, or installed target changes.

Clean before/after runs used the pinned Clang 18 toolchain, time traces on,
ccache off, and a fixed six-job limit on the same development host:

```bash
CMAKE_BUILD_PARALLEL_LEVEL=6 ./scripts/profile-build linux-clang-development
./scripts/check-build-budget
```

| Metric | Before | After |
| --- | ---: | ---: |
| Summed frontend | 52.1 s | 49.4 s |
| Summed backend | 21.4 s | 22.1 s |
| Build wall (script) | 14.67 s | 14.06 s |
| Compile database entries | 30 | 29 |
| ClangBuildAnalyzer compilation events, including scans | 56 | 54 |

This is one controlled pair, not a statistically established speedup. Removing
the duplicate compiler invocation is deterministic; the timing deltas include
host noise. An initial run without the six-job limit measured 133.0 s frontend
and is not comparable to this pair. Neither local timing is used to calibrate
the CI budget. Raw local evidence is retained under the ignored
`out/assert-budget-review/` directory.

### Budget decision

Raise only `total_frontend_seconds` from **65 to 100**, approximately 35%
headroom over the supplied **73.8 s CI measurement**. This accepts the expanded
test graph while retaining the original policy of headroom for runner noise.
It conservatively uses the observed CI run before the driver optimization;
post-optimization CI timing is not yet measured. All per-header limits and all
tests remain enabled. Local `check-build-budget` passed with 49.4 s frontend.

Existing Logging header costs still warrant their separately reviewed
optimization work; this adjustment does not declare them cheap or raise their
limits. Recalibrate again after integrating the Logging redesign on `main`,
using the actual combined CI graph rather than adding speculative allowances.

### Follow-up validation

`./scripts/test` passed 12/12 for each of `linux-clang-debug`,
`linux-clang-development`, and `linux-clang-profile`, and 11/11 for
`linux-clang-asan-ubsan`. The existing dedicated build trees were validated with
the following commands (the ordinary Release preset disables tests):

```bash
out/host-tools/venv/bin/cmake --build out/build/linux-clang-release-assert-tests -j2
out/host-tools/venv/bin/ctest --test-dir out/build/linux-clang-release-assert-tests --output-on-failure -j2
out/host-tools/venv/bin/cmake --build out/build/linux-clang-assert-tsan -j2
out/host-tools/venv/bin/ctest --test-dir out/build/linux-clang-assert-tsan --output-on-failure -j2
./scripts/check linux-clang-development --all
./scripts/install-sdk linux-clang-development
git diff --check
```

Release passed 12/12, TSan 11/11, format/tidy passed, and the installed SDK
consumer built and ran. Socket/death and sanitizer tests required execution
outside the sandbox's socket/process-inspection restrictions. Replaying the
supplied CI report through the budget checker reproduced the 65 s failure and
passed at 100 s; probes at 100.1 s total and 3201 ms for `log.hpp` still failed.

## Audio content CI calibration

PR #62 adds native streaming/device sources, Content/AudioContent, a pinned C
parser, the packager/sample and three native Catch2 translation units. Its full
uncached CI profile uses the same Ubuntu 24.04 runner class, pinned Clang 18,
Development preset and two-job concurrency as main:

| Measurement | Main `41dd763` | Audio `2da869a` |
| --- | ---: | ---: |
| ClangBuildAnalyzer compilation events | 304 | 336 |
| Summed frontend parsing | 139.6 s | 174.3 s |
| `audio_types.h` average/include | 172 ms | 167 ms |
| `audio_system.h` average/include | 134 ms | 150 ms |
| `core.h` average/include | 29 ms | 42 ms |

Sources: [main CI profile](https://github.com/Alegruz/Ludus/actions/runs/37128917880/job/111221053785)
and [audio CI profile](https://github.com/Alegruz/Ludus/actions/runs/37129607360/job/111222059306).
These run on separate hosts; their difference is not an exact attribution of
feature cost. The audio run fails only the aggregate 170 s limit. All ranked
project headers pass, including `audio_source.h` at 123 ms. New content public
headers contain no `<format>`, `<filesystem>` or `<chrono>` includes.

Recalibrate `total_frontend_seconds` to **210 s**, about 20% headroom over the
measured expanded graph, consistent with the preceding main headroom (170 s over
139.6 s). Per-header budgets, overrides, profile inputs and tests are unchanged.
No header is ignored. This replaces the earlier heavily contended local 792.6 s
measurement as the CI calibration input; it does not claim that local measurement
passes. The next CI profile must verify the updated limit; tighten it when repeated
measurements justify doing so.

## Network core CI calibration

The full PR #82 graph at `79ab51b` measured **214.6 s summed frontend parsing**
over **419 compilation events**, with **59.4 s backend time** and **162.21 s build
wall time** on ubuntu-24.04, pinned Clang 18, and two uncached jobs. The evidence
is [CI run 37256537947, Build-time budget job](https://github.com/Alegruz/Ludus/actions/runs/37256537947/job/111594875472);
its `native-build-profile` artifact retains the raw traces and report. Only the
aggregate 210 s limit failed. The preceding audio-content calibration measured
174.3 s over 336 events. These are separate runners and different complete build
graphs: the difference is not an isolated measurement of NetworkCore overhead.

The reported project headers pass unchanged 2000 ms limits: `audio_types.h`
averaged 165 ms, `core.h` 43 ms, and `audio_system.h` 148 ms. NetworkCore adds four
implementation translation units and one Catch2 test translation unit; its
public headers include fixed-width types and `<span>` only. No heavy standard
header was added to them. Other modules merged on main since the previous
calibration also contribute to the measured aggregate.

Recalibrate only `total_frontend_seconds` from **210 to 260 s**, approximately
21% headroom over this measured complete graph, consistent with the previous
~20% calibration. All per-header limits, overrides, profile inputs and tests
remain unchanged. This accepts the measured graph growth; it makes no runtime
optimization or isolated networking compile-time claim. Verify the new limit
in CI and tighten it when repeated comparable profiles support doing so.

## Native Content CI calibration

PR #114's full native profile at `cb697f2` measured **267.2 s summed frontend
parsing over 502 compilation events**, with **76.7 s backend time**, on the
ubuntu-24.04 runner, pinned Clang 18, Development preset, two uncached jobs.
The [CI profile job](https://github.com/Alegruz/Ludus/actions/runs/37555651368/job/112581449768)
failed only the aggregate 260 s limit; its `native-build-profile` artifact retains
the complete report. The preceding
[F3 profile](https://github.com/Alegruz/Ludus/actions/runs/37548495788/job/112558762024)
measured **251.4 s over 493 events**, leaving just **3.4% headroom** at 260 s.
These are separate hosts and different complete graphs; the difference does not
isolate Content overhead or establish an optimization measurement.

The 260 s calibration originated with NetworkCore's 214.6 s over 419 events.
Subsequent modules, pack tests and native persistence coverage increased the
graph. Content adds a private failure-injection backend and two native test
translation units; its public header changes are documentation, with no new
heavy includes or templates. The reported project headers pass their unchanged
limits: `core.h` averages 43 ms and `audio_types.h` 171 ms, both below 2000 ms.
The ranked costs remain existing Catch2/filesystem and standard formatting
consumers. No production header overage was reported.

Recalibrate only `total_frontend_seconds` from **260 to 320 s**, approximately
20% headroom over the observed 267.2 s graph, consistent with the preceding
aggregate policy. All per-header limits, overrides, exclusions, profile inputs
and tests remain unchanged. No performance improvement is claimed. Verify the
next full CI profile and tighten the aggregate when repeated comparable runs
support doing so.
