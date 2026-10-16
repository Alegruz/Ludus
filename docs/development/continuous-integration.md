# Continuous integration

This guide records the implemented workflows. The proposed
[engineering quality architecture](../architecture/engineering-quality.md) defines
the next tooling, evidence and platform improvements; its
[reference review](../architecture/engineering-quality-gems-review.md) records
the Game Programming Gems ideas incorporated after the initial design.
Q0 failure semantics are implemented below; later phases remain proposals.

Native CI runs on pull requests, the `main` merge queue, a nightly schedule,
cache-input pushes to `main`, and manual dispatches. Feature branches are
validated through their pull requests, avoiding a duplicate full run for every
branch push. Superseded pull-request runs are cancelled; merge-queue and `main`
validation is allowed to finish.

## Landing changes through the merge queue

Changes land on `main` only through GitHub's merge queue. A pull request is
validated twice:

1. **Pull-request tier** — fast feedback while the author (human or agent)
   iterates. It runs every native job except the slow ones listed in
   `MERGE_QUEUE_JOBS` in `scripts/python/ci_scope.py`: the macOS compile,
   sanitizer and analyzer jobs, Linux ASan/UBSan, the optional editor and the
   uncached build-time budget.
2. **Merge-queue tier** — the full set of jobs on the queue's candidate commit,
   which is the pull request merged on top of `main` and every pull request
   ahead of it in the queue. Only a passing candidate is pushed to `main`, so two
   pull requests that pass separately but conflict semantically cannot both land.

When a pull request is ready (checks green, review threads resolved), add it to
the queue with **Merge when ready** and move on; nobody has to watch it. If the
queued candidate fails, GitHub removes only that pull request from the queue and
the others continue. Fix it and enqueue it again.

The maintainer may add the `ci:full` label to run the merge-queue tier on the
pull request itself, for example when it changes the editor, macOS or sanitizer
paths and that feedback is wanted before queueing. Adding the label starts a
new run. Agents do not add it: a labeled pull request repeats the full tier,
and its macOS jobs compete with queued candidates for the account's few
concurrent macOS runners. Validate locally and let the queue run the full tier.

### Runner capacity and `main` runs

Wall time is usually dominated by waiting for a runner, not by job execution:
GitHub limits how many jobs, and especially macOS jobs, an account runs at once.
CI therefore avoids repeating work the queue already did:

- Every commit on `main` was produced by a passing merge-queue candidate, so
  `ci.yml` does not rerun the full tier on every push to `main`. It runs on
  `main` nightly, and on pushes that change shared cache-key inputs (Conan
  recipes and lock, pinned tool versions, presets, the CI workflow and setup
  actions). Those runs save the dependency and compiler caches that pull
  requests restore; caches saved by pull-request and merge-queue runs are not
  visible to other branches. Between refreshes, compiler caches restore by
  prefix from the latest `main` snapshot.
- The wiki's macOS documentation-tool checks and the macOS butler installs in
  `project-sdk.yml` run in the merge queue and on `main`, not on pull requests.
  Pull requests still build the wiki and install butler on Ubuntu.
- The wiki and `project-sdk.yml` keep running on every push to `main`; the wiki
  run publishes GitHub Pages.

### Repository settings

The queue is configured in a ruleset that targets the default branch
(**Settings → Rules → Rulesets**). Merge queues are only available for
organization-owned repositories.

- **Require merge queue** with the squash merge method, a build concurrency of
  5, a minimum group size of 1, a maximum group size of 5, and a 5-minute wait
  for a minimum group size. The status check timeout must exceed the slowest
  full-tier run; 90 minutes leaves room.
- **Require status checks to pass**: `Ubuntu Clang`, `Host tooling (ludus_tools)
  unit tests`, `Public API documentation`, `Build or reuse browser editor`,
  `Playable web samples (software GPU)` and `Build and check wiki`. Leave
  "Require branches to be up to date" off; the merge queue provides that
  guarantee without forcing every open pull request to rerun after each merge.
- Keep the existing Copilot review, deletion and non-fast-forward rules.

A required check must report on `merge_group` events, or queued pull requests
wait until the timeout. `ci.yml`, `project-sdk.yml` and `wiki.yml` therefore
trigger on `merge_group`, and `test_ci_scope.py` checks this. The path-filtered
browser package and release workflows stay advisory pull-request checks outside
the queue.

## Change routing

Every run first tests the CI routing policy and compares the complete change.
Pull requests are compared from the merge base to their head; merge-queue runs
compare the queue's base commit with its candidate; pushes to `main`
compare the previous and new commit; nightly runs validate everything. A
documentation-only change skips native engine and installed host-tooling
validation, while the wiki workflow still
builds the complete site, generates the API reference, checks documentation
coverage, and validates links and search.

The exemption is an explicit allowlist in `scripts/python/ci_scope.py`: `docs/`,
`README.md`, MkDocs configuration, the wiki workflow, the pinned Doxygen
configuration, and the API/wiki generation and validation scripts and tests.
Engine, build, dependency, general Python tooling, and CI routing policy changes
always require full validation. Mixed changes also require it. Manual runs,
empty comparisons, and unavailable or invalid comparison metadata default to
full validation. Rename detection is disabled so moving a file into the docs
folder still checks its original path.

For changes requiring native validation, source formatting, foundational include
boundaries, and Python tooling tests run first. Independent jobs then validate:

- Development builds, all native tests, and the installed SDK consumer, without PCH;
- ASan/UBSan builds and tests, with clean-startup and known-defect controls;
- ThreadSanitizer concurrency tests and a known-race control;
- macOS platform builds, Cocoa tests, static analysis, and sanitizers;
- full Clang static analysis, including test translation units;
- optional editor initialization and offscreen tests;
- Debug, Profile, and Release assertion policies;
- PCH builds and runtime tests;
- clean build-time profiles and the existing header/aggregate budgets.

The final `Ubuntu Clang` check always runs. It requires successful classification
and every native job to succeed for full validation, or every native job to be
skipped for documentation-only changes. In the pull-request tier the deferred
merge-queue jobs must be skipped and every other native job must succeed.
Failed or cancelled dependencies and missing classification or tier outputs
fail this gate; malformed pull-request metadata selects the full tier. The installed host-tooling check
also retains its name and reports success after routing tests for docs-only
changes, without installing compilers or SDK dependencies.

These workflows use job/step conditions rather than workflow-level path filters
so their existing required checks remain available to branch protection. See
GitHub's [job condition guidance](https://docs.github.com/en/actions/how-tos/write-workflows/choose-when-workflows-run/control-jobs-with-conditions)
and [workflow path-filter behavior](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax#onpushpull_requestpull_request_targetpathspaths-ignore).

`.github/actions/native-setup` installs prerequisites and prepares only the
job's selected preset. `CI=true` enables warnings as errors without invoking
`init.sh --ci`, which performs full validation. The profiling job uses setup
only and disables compiler caching explicitly, so it does not duplicate native
validation before measuring a clean build.

## Dependencies and caches

CI uses `init.sh --locked` to consume the committed `conan.lock` without
regenerating it or permitting dependencies outside the lock. Update dependencies
through normal initialization and review the resulting lockfile separately.
A missing or incompatible lock fails CI rather than silently resolving new
versions.

Conan cache keys include the preset, local recipes and sources, lockfile,
profile, tool versions, and presets. A commit suffix permits refreshed snapshots
after restoring a compatible older cache. Compiler caches are separated by
preset and configuration (ordinary, PCH, editor). CMake honors
`LUDUS_ENABLE_CCACHE=1` from the environment for native builds; browser presets
keep their separate cache policy. The native setup action installs ccache.

## Analysis and timing

`LUDUS_TIDY_JOBS` sets the maximum number of concurrent native clang-tidy
processes. The local default is one; CI uses two. Diagnostics are saved per
compile command and printed in source/variant order.
The selected preset's CMake File API codemodel is reconciled with its compilation
database. All compiled first-party C, C++, Objective-C and Objective-C++ sources
under `modules/`, `apps/`, `tools/`, `tests/` and `examples/` are inventoried;
platform-disabled sources are outside that configured scope. CMake-declared
generated sources and prepared/external dependencies are reported as exclusions.
This does not claim coverage of optional targets or separately configured SDK
consumers absent from that preset; their existing dedicated checks remain.

Missing files, malformed/stale databases, missing source or target variants, and
empty required scopes fail. Exact duplicate commands are deduplicated; distinct
commands for the same source are analyzed using separate single-entry databases.
`LUDUS_TIDY_SHARD_COUNT` and `LUDUS_TIDY_SHARD_INDEX` select deterministic shards
only after the full inventory is validated. Empty shards fail.

Ignored `out/quality/analysis/run-*/` contains `inventory.json`, the exact
single-entry compile databases, original combined stdout/stderr byte prefixes
and `results.json`. Each process has a 300-second deadline and a 1 MiB diagnostic
limit; the invocation has a shared 100 MiB raw-log budget. A timeout, truncation,
missing log, launch failure or omitted result fails even if the process returns
zero. Independent commands still report findings. CI always uploads each shard's
available evidence with 14-day retention; a missing artifact fails the upload.
These are Q0 inventory/control records, not the proposed Q2 full run schema.

## Sanitizer failure controls

`LUDUS_ENABLE_UBSAN=ON` propagates `-fno-sanitize-recover=undefined` through the
same project options as sanitizer instrumentation. Recoverable UB therefore
fails the invoking test. Thanks to LLVM's
[Clang 18 UBSan usage contract](https://releases.llvm.org/18.1.8/tools/clang/docs/UndefinedBehaviorSanitizer.html#usage)
for the fatal recovery policy and signed-overflow control.

Enabled ASan, UBSan and TSan builds register `sanitizer-control` CTest tests.
Their deliberately defective children live only in `tests/quality/`; they are
never installed or called by production. Each Python driver requires clean
startup to exit zero, then requires a separate defect process to exit nonzero
and emit the specific bounds/overflow/race diagnostic. A recovery exit, unrelated
crash, runtime startup failure, truncated output or timeout is a failed control.
`WILL_FAIL` alone is insufficient and is not used.

Run the controls after building the selected sanitizer preset:

```bash
./scripts/build linux-clang-asan-ubsan
out/host-tools/venv/bin/ctest --preset linux-clang-asan-ubsan --label-regex sanitizer-control --output-on-failure --no-tests=error
```

On macOS use `macos-clang-asan-ubsan`. TSan remains a separate build tree;
its existing CI job now builds and runs `ludus_tsan_control_child` before the
concurrency acceptance suite. It follows LLVM's [Clang 18.1.8 ThreadSanitizer
usage example](https://releases.llvm.org/18.1.8/tools/clang/docs/ThreadSanitizer.html#usage):
parent and worker write one shared global volatile slot before joining, using
direct `pthread_create`/`pthread_join` calls with explicit error checks. The
worker polls a separate relaxed atomic gate set after the parent's write. This
forces the conflicting access to follow the live parent's write in wall-clock
time without a release/acquire happens-before relationship. Using release/acquire
for this gate would remove the intentional defect. Volatile stores remain
visible in optimized builds. CI requires 16 consecutive successful clean/fault
control pairs with `--repeat until-fail:16`; any failure stops acceptance rather
than retrying to obtain a pass. Native Clang controls explicitly locate the
compiler's LLVM symbolizer; missing symbolizers fail configuration. Control
children receive explicit sanitizer options with no inherited suppression/log
redirection; UBSan requests runtime recovery to prove the compiler policy still
terminates. This environment applies only to the test children.

Each child has a 15-second deadline and 1 MiB log ceiling; each two-process
control has a 2 MiB raw-log ceiling. Its unique run directory under
`out/build/<preset>/tests/quality/<kind>-evidence/` retains clean/fault logs and
`results.json`. Sanitizer and TSan CI jobs preserve that evidence even on failure.
The macOS sanitizer preset runs and preserves controls before native runtime and
Behavior acceptance, so a later application failure cannot prevent their capture.
Its separate Debug diagnostic configure retains its startup/decision tests.

## Q1 analysis and host tooling

Platform/tooling owns the new baseline checks and finding triage. They remain
advisory for at least two weeks; promotion requires reviewed Linux/macOS/browser
inventories and findings, an accountable maintainer and measured runtime cost.
Existing format, tidy, compiler, sanitizer and source-inventory gates stay
required. Advisory commands still exit unsuccessfully for findings, missing
tools, omissions, timeouts or incomplete output; CI explicitly tolerates their
step failure during rollout and preserves their evidence. Artifact failures are
not advisory. Complete macOS source admission has a separate required
`--inventory` step; Linux/browser inventories run inside their required tidy
checks. Analyzer findings cannot excuse source omissions.

```bash
./scripts/setup-quality-tools   # explicit network install into ignored out/
./scripts/check --tooling       # no installation, configure or engine build
./scripts/check linux-clang-development --analyzer
```

`config/tool_versions.json` pins Ruff and actionlint, including SHA-256 checksums
for actionlint's Linux/macOS x86_64/arm64 archives. Checks verify exact executable
versions and record executable digests. `ruff.toml` enables selected syntax,
undefined-name, duplicate-definition/key and invalid-control-flow rules. It
does not introduce formatting or import-order churn. The command inventories
tracked Python files and Python entry points without extensions; vendor code is
excluded explicitly. Stage new files before a local check to include them.
actionlint checks every `.github/workflows/` YAML workflow's syntax and expression
contracts. Embedded Python/ShellCheck adapters are disabled so optional unpinned
tools cannot silently change the check. Composite action YAML is not claimed as
actionlint coverage. ShellCheck remains a separate future rollout.

`config/clang-analyzer.yml` selects stable LLVM 18 `core`, `cplusplus` and `unix`
families. Experimental/opt-in checkers are not enabled.
The released Move check uses `WarnOn=KnownsOnly`: it models known standard types
without treating Ludus' documented empty/reusable moved-from states as errors.
`--analyzer` uses the same
complete per-configuration native inventory and bounded execution as required
tidy. Browser checks now reconcile their database with the independent CMake
File API too, retaining explicit Emscripten flags. Linux shards, the complete
macOS Development build and both browser profiles produce advisory baselines;
no baseline suppression file is generated. Fix a finding or justify a narrow
source suppression before promotion.

The initial local macOS Development scan examined 267 sources / 291 compile
commands, including fault-test variants, with no inventory omissions. After
reviewing the Move model, four commands report findings in three sources:
`diagnostic_format.cpp` (null argument/count relationship), `namespace.cpp`
(empty span versus entry allocation, two variants), and `jobs.cpp` (graph
destruction/initialization). These remain unsuppressed advisory findings for
maintainer path review; this scan does not establish that they are defects or
that other supported-platform baselines are clean. The same build passes its
native headless tests; device availability is reported separately.

Unique `out/quality/tooling/run-*/` directories retain Ruff/actionlint logs,
commands, versions, digests, source counts and status. Analyzer evidence remains
under `out/quality/analysis/run-*/`. CI uploads both for 14 days, including failed
checks. Regression controls prove actual Ruff/actionlint/analyzer diagnostics
reject known-bad inputs and cover checksum, empty/missing scope and editor setup
boundaries. [VS Code setup](building.md#vs-code) uses the same prepared preset
for clangd and Ludus tasks without rewriting custom editor files.

## Measuring feedback

GitHub's separate setup/build/test/analysis steps expose phase durations.
Compiler jobs publish cache statistics, and the budget job uploads its detailed
profile. Compare cold and warm runs across several commits when evaluating
changes; parallel jobs can lower completion time while increasing runner minutes.
The browser workflow continues to test the exact packaged Release ZIP and
cancels superseded pull-request runs independently. Renderer browser coverage runs
in separate baseline (L0–L4) and scaling (L5) jobs, each with a ten-minute
budget and its own evidence artifact. All image oracles and backend cases remain
required; the packaged browser lane retains RHI, camera and scripting coverage.
