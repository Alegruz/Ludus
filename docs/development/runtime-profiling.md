# Runtime profiling

Ludus runs a fixed set of runtime scenarios on a schedule and keeps their
measurements, Perfetto traces and logs as CI artifacts. This guide covers the
local command, the scheduled workflow, how to read a report and how to add a
scenario. Build-time measurement is separate; see [Build Profiling](build-profiling.md).

Timings on shared GitHub-hosted runners are **reported, never gated**. A run
fails only when a scenario is incorrect: a non-zero exit, a timeout, a missing
or malformed trace, or output the runner cannot parse. This follows the
hardware/performance lane in the
[engineering quality architecture](../architecture/engineering-quality.md#4-validation-lanes-and-scheduling):
timing gates need controlled hardware and a calibrated variability bound, which
the hosted lane does not provide.

## Run it locally

Prepare the Profile preset once, plus the pinned shader tools for the world demo
scenarios:

```bash
./init.sh --cli linux-clang-profile --preset-only --with-tests
```

```bash
./scripts/shader-probe bootstrap
```

Then build and run every scenario this host supports:

```bash
./scripts/profile-engine
```

The default preset is the host's `-profile` preset (`linux-clang-profile` or
`macos-clang-profile`). The command configures that preset with the CMake options
the selected scenarios declare, builds only their targets, runs each scenario's
warmups and measured repetitions, and writes `out/runtime-profile/<preset>/`.

| Option | Effect |
| --- | --- |
| `--list` | List scenarios, owners and required capabilities. |
| `--scenario ID` | Run only this scenario (repeatable). It must be runnable on this host. |
| `--capability NAME` | Declare a capability the runner cannot detect, such as `presentation` for a desktop session with a GPU (repeatable). |
| `--repetitions N` | Override every scenario's measured repetitions (1–50). |
| `--baseline FILE` | Compare medians against an earlier `report.json`. |
| `--output DIR` | Write elsewhere. The runner only replaces a directory it created. |
| `--no-build` | Run already-built executables. |

For example, on a macOS desktop session:

```bash
./scripts/profile-engine --capability presentation --baseline out/previous/report.json
```

## Output

| Path | Contents |
| --- | --- |
| `report.json` | Schema 1 report: commit, dirty state, preset, compiler, host identity, CI run identity, capabilities, and per-scenario metrics (median, min, max, relative spread and every sample). With `--baseline`, also per-metric deltas. |
| `summary.md` | The same results as Markdown tables; the workflow appends it to the job summary. |
| `traces/<scenario>.json` | Chrome/Perfetto trace from the last measured repetition of each trace scenario. Open it at <https://ui.perfetto.dev>. |
| `logs/<scenario>-<run>.log` | Combined stdout/stderr of every warmup and measured run. |

Metric names are paths: `iterations=10000/workers=4/median_ns`,
`case=xxh3_64/ns_per_operation`, `scope=WorldTick/p95_ns`. Names ending in `_ns`
or `ns_per_operation` are nanoseconds. Every scenario also reports
`process/wall_ns`. Trace scenarios report each scope's `count`, `median_ns` and
`p95_ns`, plus `frames/count` and frame-interval percentiles from
`LUDUS_PROFILE_FRAME()` markers. Percentiles use the nearest-rank convention
shared with `scripts/benchmark-rhi`.

A metric is marked **notable** against its baseline when all of these hold:

- the median moved by at least 10%;
- the measured repetition ranges do not overlap;
- for trace-derived timings, the shift is at least 250 ns. Trace timestamps are
  quantized by the platform clock (41.67 ns per tick on Apple Silicon).

"Notable" is a prompt to look at the traces, not a regression verdict. Compare
like with like: the summary warns when the baseline host identity differs.

## Scheduled workflow

[`.github/workflows/performance.yml`](../../.github/workflows/performance.yml)
runs nightly at 03:17 UTC on `main` and can be started manually from the
**Actions → performance → Run workflow** button with optional scenario ids and
a repetition override. Named scenarios must be runnable on both lanes; for
example, `world-presented` fails the Ubuntu lane because it needs `presentation`.
GitHub runs schedules from the default branch only, so a change to the workflow
takes effect after it lands.

| Lane | Runner | Preset | Declared capabilities |
| --- | --- | --- | --- |
| `ubuntu` | `ubuntu-24.04` | `linux-clang-profile` | none (headless) |
| `macos` | `macos-14` | `macos-clang-profile` | `presentation` (hosted macOS already presents Metal frames in CI) |

Each lane:

1. Prepares the locked Profile preset with the shared native or macOS setup
   action, and the pinned shader tools.
2. Downloads the `runtime-profile-<lane>` artifact from the most recent completed
   run on `main` as the baseline, if one exists.
3. Runs `./scripts/profile-engine` and appends `summary.md` to the job summary.
4. Uploads `out/runtime-profile/<lane>/` as `runtime-profile-<lane>` with 90-day
   retention, even when a scenario failed.

The workflow never runs on pull requests and is not a required check. To compare
a branch, run the command locally on the same machine for the base and head
commits, then pass the base `report.json` as `--baseline`.

## Scenarios

Scenarios live in
[`config/runtime_profile_scenarios.json`](../../config/runtime_profile_scenarios.json).
The runner rejects unknown keys, duplicate ids and inconsistent settings before
it builds anything.

| Id | Owner | Measures | Requires |
| --- | --- | --- | --- |
| `world-headless` | `apps/world_demo` | 2000 simulation ticks with a trace of the world tick phases | `shader-tools` |
| `world-presented` | `apps/world_demo` | 240 presented frames with a trace of frame, tick and extraction scopes | `shader-tools`, `presentation` |
| `threading-jobs` | FoundationThreading | Job graph submit-to-completion by worker count and work size | — |
| `strings-corpus` | FoundationStrings | String construction, sharing, hashing and lookup per operation | — |
| `input-reducer` | Input | Keyboard reducer step and batch latency | — |
| `containers-associative` | FoundationContainers | Sorted/hash/tree lookup and insertion by element count | — |

The world demo scenarios use the demo's `--trace-output <path>` option. With
`--headless`, `--frames N` selects the number of simulation ticks (see the
[world demo guide](../examples/world-demo.md#run-the-application)).

### Add a scenario

Add an entry with every field:

| Field | Meaning |
| --- | --- |
| `id` | Lowercase id, unique in the manifest. |
| `owner` | The module or application directory that owns the workload. |
| `description` | What the workload measures. |
| `target`, `executable` | CMake target and its path relative to the preset build directory. |
| `arguments` | Fixed command-line arguments. |
| `cmake_options` | `LUDUS_*` options (`ON`/`OFF`) the target needs; every runnable scenario must agree. |
| `requires` | Capabilities: `shader-tools` is detected from `out/shader-tools`; anything else is declared with `--capability`. |
| `trace` | `true` appends `--trace-output <path>`; the executable must write a Perfetto trace there. |
| `parser` | How to read metrics (below). |
| `warmup`, `repetitions`, `timeout_seconds` | Unmeasured runs, measured runs and the per-run deadline. |

| Parser `kind` | Fields | Reads |
| --- | --- | --- |
| `perfetto-scopes` | — | The requested trace (only with `trace: true`). |
| `key-value` | `identity`, `metrics` | Lines of `key=value` tokens; identity keys name the row. |
| `csv` | `header`, `key`, `metrics`, `ignore` | The table after the exact `header` line; rows whose key is in `ignore` are skipped. |
| `regex` | `pattern` | Lines matching the pattern; named groups other than `label` are metrics. |

Keep workloads deterministic and bounded: fixed seeds and inputs, no network, and
a few seconds per run on a hosted runner. Instrument engine code with
FoundationProfiling scopes (see the [module guide](../modules/foundation/profiling.md))
so trace scenarios break the work into named phases. Run
`PYTHONPATH=scripts/python python3 -m unittest test_runtime_profile -v` after
changing the manifest or the runner.

## Limits

- Hosted runners are shared virtual machines. Their timings vary between runs and
  between image updates, and they carry no GPU timing evidence. The macOS lane
  presents through a virtual display; its frame intervals follow display pacing,
  not GPU cost.
- Linux hosted runners have no display, so `world-presented` is skipped there.
- The filesystem benchmark needs a generated pack corpus, and the smoke app
  runs until its window closes; neither is a scenario yet.
- Controlled-device timing (self-hosted runners with declared GPUs) and gating
  on calibrated bounds remain future work in the engineering quality plan.
