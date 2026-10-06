# Continuous integration

Native CI runs on pull requests, pushes to `main`, and manual dispatches.
Feature branches are validated through their pull requests, avoiding a duplicate
full run for every branch push. Superseded pull-request runs are cancelled;
`main` validation is allowed to finish.

Every run first tests the CI routing policy and compares the complete change.
Pull requests are compared from the merge base to their head; pushes to `main`
compare the previous and new commit. A documentation-only change skips native
engine and installed host-tooling validation, while the wiki workflow still
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
- ASan/UBSan builds and tests;
- ThreadSanitizer concurrency tests;
- macOS platform builds, Cocoa tests, static analysis, and sanitizers;
- full Clang static analysis, including test translation units;
- optional editor initialization and offscreen tests;
- Debug, Profile, and Release assertion policies;
- PCH builds and runtime tests;
- clean build-time profiles and the existing header/aggregate budgets.

The final `Ubuntu Clang` check always runs. It requires successful classification
and every native job to succeed for full validation, or every native job to be
skipped for documentation-only changes. Failed or cancelled dependencies and
missing classification outputs fail this gate. The installed host-tooling check
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
processes. The local default is one; CI uses two. Diagnostics are buffered per
translation unit and printed in source order. Every command finishes before
analysis reports failure, so one failing file does not hide later diagnostics.

GitHub's separate setup/build/test/analysis steps expose phase durations.
Compiler jobs publish cache statistics, and the budget job uploads its detailed
profile. Compare cold and warm runs across several commits when evaluating
changes; parallel jobs can lower completion time while increasing runner minutes.
The browser workflow continues to test the exact packaged Release ZIP and
cancels superseded pull-request runs independently.
