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

### Improvement backlog (measure each before/after)

- **`log.hpp` include surface** is the highest-leverage target: `<format>` and
  its transitive `<chrono>` dominate. Options: a precompiled header for the
  stable heavy STL headers; or shrinking `log.hpp`'s public include surface
  (e.g. moving `std::format` usage behind the .cpp boundary where feasible).
- **Precompiled headers** for `<format>`/`<chrono>`/`<string>` once more modules
  consume them.
- **ccache** warm-cache rebuild time (now enabled in CI; measure hit rate).
- **include-what-you-use** pass to prune transitive includes at the root.

These are candidates, not commitments — each must show a win in
`profile-build` output before it lands.
