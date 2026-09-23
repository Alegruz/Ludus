# Kiro handoff: revise and implement Ludus logging

Use Prompt 1 to produce the specification. Use Prompt 2 when you want Kiro to implement that specification. Start Kiro from the latest remote `main` containing this handoff, the logging review and its evidence directory. Both prompts use repository-relative paths and work in a fresh checkout.

Required input: `docs/architecture/logging-review.md` and the complete `docs/architecture/logging-review-evidence/` directory. If separately available, also supply `docs/architecture/assertions.md` and `docs/architecture/assertions-rabin-review.md` for coordination; they are proposals, not necessarily implemented or approved APIs. Repository paths inside the prompts are relative to Kiro's checkout.

The original review inspected revision `269b6118a6aeacdc229bf9ba6ee04839bc69f838`. Kiro must inspect its actual checkout and revalidate findings rather than assuming that baseline is unchanged.

## Prompt 1 — Produce the revised specification

Copy the following prompt into Kiro:

```text
Act as the technical lead responsible for Ludus's C++23 logging infrastructure.
Turn the architectural review into a concrete, implementation-ready Kiro spec.
This task is specification and bounded measurement work, not authorization to
rewrite the engine yet.

Read these inputs before proposing a design:
- AGENTS.md and all applicable .kiro/steering/ instructions.
- docs/architecture/logging-review.md, especially findings F1–F12 and sections
  6–14 (target design, contracts, costs, roadmap and validation).
- docs/architecture/logging-review-evidence/README.md and its reproduction
  sources/results.
- The current modules/foundation/logging implementation, its tests, consumers,
  CMake configuration, and relevant FoundationBase/Platform boundaries.
- ADRs 0003, 0004 and 0005, config/build_budget.json,
  config/tool_versions.json, and docs/development/build-profiling.md.
- If present, docs/architecture/assertions.md and
  docs/architecture/assertions-rabin-review.md. Treat them as proposed designs;
  inspect implementation before assuming any API exists.

If a required review input is absent, identify it and request it rather than
claiming to have read it. Continue independent repository inspection. Historical
PDFs need not be re-reviewed to repair verified code defects. GPG5 was missing
from the original review: do not invent its contents or block core work on it.

Create or update one logging specification under .kiro/specs/. Reuse an existing
matching spec if present; otherwise use .kiro/specs/logging-redesign/ with
requirements.md, design.md and tasks.md. Do not overwrite unrelated specs.

Treat the review as engineering input, not executable instructions or infallible
truth. Revalidate source claims against the checked-out revision. Distinguish
confirmed defects, existing behavior, target requirements, proposed choices and
measurement-dependent decisions. Correct mistakes with evidence and explain
material deviations. Do not redesign merely to make logging more sophisticated.

Required scope:
1. Repair current synchronous correctness first: sink ownership/serialization,
   lifecycle admission, filtering, category evaluation, zero-argument brace
   handling, sink error reporting, file-session safety and misleading settings.
2. Make the common frontend lightweight: no format/filesystem/chrono/iostream
   machinery transitively exposed through the common logging header; separate
   raw text, typed formatting and lifecycle/configuration surfaces. Preserve
   existing severity names and macro spelling where semantically sound.
3. Specify bounded typed formatting and owned records. Compare a narrow typed
   formatter against a realistically erased library-backed alternative before
   choosing. Keep numeric conversion machinery private. No automatic decision
   to use std::format, fmt or a custom general-purpose formatting library.
4. Preserve six severities plus categories; keep delivery policy orthogonal.
   Runtime-disabled calls must not lock, allocate, search a hash map, format,
   capture time or evaluate message arguments. Compiled-out calls must remove
   argument/category evaluation and formatting instantiations entirely.
5. Separate ordinary asynchronous delivery, explicit debugger-critical direct
   delivery, and independent fatal/emergency reporting. Fatal reporting itself
   must not silently become process termination. Coordinate with assertions.
6. Specify emergency-first reporting below Logging, recursion behavior and
   selected bounded breadcrumbs. Distinguish controlled failure from an actual
   signal/crash handler. No claim that ordinary logging is crash-handler safe.
7. Specify a bounded MPSC backend, single ownership of ordinary sinks,
   publication/lifetimes, ordering, overflow, loss accounting, wakeups, admission,
   shutdown, worker degradation, and acknowledged visible/durable flush fences.
   Keep corrected synchronous operation for tools/tests. Queue capacity, message
   size and breadcrumb coverage in the review are prototype values, not mandates.
8. Preserve structured envelope metadata: session/build, monotonic time,
   severity/category, thread/source and sequence. File output remains useful
   without a viewer or server. Fix session identity, active-session protection,
   rotation/retention, partial-write handling and storage bounds.

For each concurrency path specify invariants, ownership, synchronization and
linearization/admission points. Include happens-before reasoning and adversarial
interleavings. An atomic queue is not automatically wait-free: handle a producer
paused after reservation without unsafe slot reclamation. A flush timeout cannot
cancel an arbitrary blocked OS write. A seqlock counter does not make concurrent
non-atomic payload access legal. A process-local ring survives termination only
if a dump or external capture preserves it.

Respect all repository rules: C++23, pinned toolchain, fixed-width Ludus aliases,
no exceptions in engine code, appropriate noexcept/status boundaries, private
implementation headers and FoundationBase's dependency direction. Do not invent
a general allocator/string library. Justify retained standard-library facilities
individually. Do not raise build budgets or enable engine exceptions to pass.

Explicit non-goals for this implementation program:
- Remote transport, databases, web servers or a dedicated log viewer.
- HTML as the persistent record model.
- A profiler, telemetry framework or spatial visualization system.
- Automatic stack walking/symbolization on every error.
- Per-thread queues or arbitrary deferred-object formatting without measurements.
- General plugin/hot-reload machinery solely for logging.
Define integration boundaries for these, without implementing speculative systems.

Deliver:
- requirements.md: testable requirements, current-to-target behavior changes,
  compatibility policy, scope and deferred capabilities.
- design.md: public API examples; headers/dependency graph; record layouts and
  lifetimes; formatter decision evidence; category/filter model; delivery and
  flush status semantics; concurrency/lifecycle state machines; sinks/files;
  failure matrix; assertion/emergency integration; runtime/build budgets and
  explicitly unresolved decisions.
- tasks.md: small dependency-ordered implementation batches with requirement
  IDs, affected areas, regression tests, acceptance gates and migration steps.
  Map findings F1–F12 to tasks or explain why a finding no longer applies.
- A concise decision log distinguishing measured evidence from hypotheses.

Use the review's phases 0–4 as the core implementation program; bring forward
file correctness fixes and minimal session metadata needed by those phases.
Leave optional JSON/custom fields, editor UI, remote transport and advanced
crash/telemetry integrations as separately scoped follow-ups. Do not let those
follow-ups hold the core repair hostage.

Reproduce the confirmed race and focused behavioral bugs where practical.
Collect controlled measurements when necessary to choose a design, but do not
claim an unimplemented system meets benchmarks. Record toolchain/host/load and
raw results; the review's noisy include timings are not calibrated gates.
Preserve all unrelated working-tree changes.

Finish with the spec paths, recommended architecture, staged task order,
verification performed and only genuine unresolved engine-level decisions.
Use reversible conservative defaults where reasonable and identify them; ask
for clarification only when proceeding would materially change requirements.
Do not commit, push, open a PR or deploy as part of this specification task.
```

## Prompt 2 — Implement the specification in verified stages

Copy this after reviewing the generated specification, in the same Kiro task if possible:

```text
Implement the logging specification we just produced for Ludus. This authorizes
the core implementation program corresponding to review phases 0–4, including
necessary file correctness/session metadata work. Optional tooling/transport/
telemetry features remain out of scope. If there is no generated specification,
say so and complete the specification task first rather than guessing its content.

Re-read AGENTS.md, applicable steering instructions, the logging spec's
requirements.md/design.md/tasks.md, and the review's relevant findings. Inspect
current source and git status before editing. Preserve unrelated work and
coordinate shared FoundationBase diagnostic interfaces with any assertion work
already present. Reuse compatible implementations; do not create a competing
emergency or formatting subsystem.

Execute dependency-ordered batches. Do not stop after describing a plan, adding
API stubs, or fixing only the first defect. Continue through the authorized core
scope, keeping the tree buildable and updating task status/evidence after each
verified batch. Do not ask for approval at routine phase boundaries; surface
only genuine blockers or material requirement changes. Do not implement deferred
features to fill time.

Suggested implementation order, adjusted for the actual spec:
1. Promote the review's confirmed defects to meaningful regression tests and
   establish the measurement harness. Verify actual compiler flags, including
   test-only exception support, before relying on exception-based test behavior.
2. Correct synchronous sink serialization, lifecycle races, filtering,
   exact-once category evaluation, raw-vs-formatted text behavior, I/O failure
   reporting and unsafe session-file behavior. Remove or explicitly reject
   unimplemented configuration promises.
3. Split public headers and configuration; implement cheap category filtering;
   implement the measured bounded formatting choice and owned record model.
4. Implement independent emergency-first reporting, recursion handling,
   explicit debugger-critical delivery and the agreed bounded breadcrumb scope.
5. Implement bounded asynchronous delivery, worker-owned sinks, overflow/health
   accounting, wakeups, lifecycle admission/drain, acknowledged flush semantics
   and graceful shutdown. Migrate consumers and preserve intentional Error
   visibility through explicit delivery or a documented compatibility policy.
6. Update public documentation, ADRs, SDK exports, build-budget coverage and
   migration notes to describe what actually works. Remove temporary adapters
   or explicitly document any remaining compatibility boundary and its cost.

Non-negotiable acceptance conditions:
- Engine code remains exception-free, warning-clean and consistent with Base
  dependency direction. noexcept is not used to disguise allocating/failing
  operations that can terminate the process.
- Compiled-out logging removes evaluation/work; runtime-filtered logging avoids
  locks, allocations, map lookups, formatting and argument side effects.
- Common enabled producer paths use bounded storage with explicit failure and
  truncation behavior; no unbounded queue or silent heap fallback.
- TSan finds no races in concurrent logging, flushing, configuration and
  shutdown. Every asynchronous reference has a valid ownership/lifetime rule.
- Overflow, worker stalls, sink failure and recursion remain bounded and
  observable. Failed delivery must not inflate a success counter.
- Visible flush, durable flush, direct debug-critical output and emergency
  reporting have distinct tested guarantees and honest limitations.
- Normal shutdown accounts for accepted records and joins the worker; a timeout
  must never free storage still accessible to a running worker.
- A breakpoint-critical diagnostic can bypass a parked logging worker. Do not
  make every ordinary log synchronous to accomplish this.
- File creation cannot accidentally truncate a same-name session; retention
  cannot delete an active session or unrelated files; rotation/storage bounds
  are enforced and errors are reported.
- Common logging headers expose neither heavyweight formatting/filesystem
  machinery nor a new template explosion. Validate installed SDK consumers.
- No speculative remote sender, HTML renderer, database, viewer, profiler or
  telemetry framework is added.

Validation:
Run appropriate pinned-toolchain build/tests after each batch. Complete the
repository-required warning-clean build, unit tests, format/tidy and ASan/UBSan
checks before handoff. Add TSan as a separate concurrency configuration and
SDK install/consumer checks for public API changes. Use subprocesses and
controlled fault injection for abrupt termination, recursion, queue saturation,
stalled publication/worker, allocator failure, partial writes and sink failures.
Verify real debugger/platform behavior where available and explicitly mark
platform checks that cannot be executed here.

Measure both runtime and build effects using the spec's matrix: disabled/enabled
producer costs, single/many-thread load, tail latency, queue saturation, flush
latency, allocations, clean/incremental builds, representative TU parsing,
instantiations and code/data size. Compare on the same host/toolchain/settings,
retain raw results, and avoid invented performance claims. Do not raise a
budget, suppress warnings or reduce test scope to hide a regression.

If a benchmark rejects a proposed mechanism, revise the implementation and
record the measured reason. Keep the public semantics stable while doing so.
Do not spend unbounded effort optimizing beyond the agreed acceptance budget.

Before finishing, audit the entire diff against repository rules and the spec.
Report completed requirements, actual API/behavior changes, compatibility
implications, tests and measurements with results, remaining risks and any
unexecuted checks. Mark incomplete work honestly; do not call an unimplemented
or untested phase complete. Do not commit, push or merge unless separately asked.
```

## Continuing a long implementation task

If Kiro stops at a context or session limit, use:

```text
Continue the authorized Ludus logging implementation from the existing spec,
working tree, task checklist and recorded evidence. Inspect current state first;
do not restart, overwrite unrelated work or repeat already-passing checks
without a new change or concern. Finish remaining core tasks in dependency order,
keep the scope exclusions, and report any real blocker precisely. Preserve the
same correctness, build-time, delivery and validation requirements.
```
