# Continuous integration

Native CI runs on pull requests, pushes to `main`, and manual dispatches.
Feature branches are validated through their pull requests, avoiding a duplicate
full run for every branch push. Superseded pull-request runs are cancelled;
`main` validation is allowed to finish.

Source formatting, foundational include boundaries, and Python tooling tests
run first. Independent jobs then validate:

- Development builds, all native tests, and the installed SDK consumer, without PCH;
- ASan/UBSan builds and tests;
- full Clang static analysis, including test translation units;
- optional editor initialization and offscreen tests;
- Debug, Profile, and Release assertion policies;
- PCH builds and runtime tests;
- clean build-time profiles and the existing header/aggregate budgets.

The final `Ubuntu Clang` check requires every native job to succeed. Failed,
cancelled, or skipped dependencies fail that check. Its existing check name remains
available to branch protection after splitting the work.

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
