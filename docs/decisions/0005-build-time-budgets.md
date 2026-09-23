# ADR 0005: Build-Time Budgets Instead of Include-Hygiene Linting

## Status

Accepted.

## Context

ADR 0004 removed the single biggest build-time cost (type-erasing `std::format`
in the logging boundary). To keep build time from silently regressing, we want a
systematic, enforced guard against the most common cause: a heavy header
(`<format>`, `<filesystem>`, `<chrono>`, `<regex>`, ...) leaking into a public
header and taxing every consumer.

The obvious candidate was clang-tidy's `misc-include-cleaner` (or IWYU) as a
gate. In practice it is too noisy for this codebase: it flags our own aggregation
headers (e.g. `log.hpp` re-exporting `category.hpp`) and re-exported type aliases
(`ludus::foundation::uint32`) as "not directly included," producing many findings
that we do not want to act on. Making it a warnings-as-errors gate would force
redundant include lists and fight the intended header structure.

## Decision

Enforce a **build-time budget** measured directly, rather than proxying it
through include hygiene.

- `config/build_budget.json` defines a default per-header average parse-time
  budget, optional per-header overrides, and a total-frontend-parse budget.
- `./scripts/check-build-budget` (engine.py `check-build-budget`) parses the
  ClangBuildAnalyzer report produced by `./scripts/profile-build`, and fails if
  any **project** header (under `modules/`) exceeds its average per-include
  budget, or if total frontend parse time exceeds the global budget.
- The gate keys on **average per-include** parse time, not total: total scales
  with how many translation units include a header (grows with the codebase even
  if the header is unchanged), whereas the average measures the header's
  intrinsic weight — the thing an author controls.

`misc-include-cleaner` and IWYU remain available as **advisory, non-gating**
tools (`./scripts/check --include-cleaner`) for pruning includes on demand.

## Where it runs

- **Not** in the fast local loop and **not** in the primary `Ubuntu Clang` CI
  job. `-ftime-trace` roughly doubles compile time and ClangBuildAnalyzer must be
  built, so gating the normal build would slow every iteration.
- A **separate `Build-time budget` CI job** runs `profile-build` +
  `check-build-budget` in parallel with the normal job, with Wayland installed so
  the full build graph is measured. Local developers run `./scripts/profile-build`
  and `./scripts/check-build-budget` on demand.

## Consequences

Regressions of the kind ADR 0004 fixed now fail CI with a specific, actionable
message naming the offending header, instead of being noticed months later.
Budgets carry deliberate headroom over the baseline and target regressions, not
run-to-run noise; absolute milliseconds are host-dependent, so the job is
advisory about exact timings but strict about the ratio to budget.

Raising a budget is allowed but must come with a `profile-build` measurement and
a justification in the PR — the same discipline as any other optimization
decision (see `docs/development/build-profiling.md`).
