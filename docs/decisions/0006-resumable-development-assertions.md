# ADR 0006: Resumable Development Assertions (Continue Once)

## Status

Accepted (documentation/spec milestone). Amends the assertion behavior contract
in [`docs/architecture/assertions.md`](../architecture/assertions.md) and is
detailed by the Kiro spec under
[`.kiro/specs/assertions-interactive-development/`](../../.kiro/specs/assertions-interactive-development/requirements.md).
No runtime or UI code changes are authorized by this ADR alone; it records the
decision and its scope so the design, spec, and later implementation milestones
can proceed against one authoritative policy.

## Context

The accepted assertion design shipped an intentionally strict contract: an
enabled invariant assertion "never returns on failure, including after a
debugger resumes execution," and "resumable fatal assertions" were listed as an
explicit non-goal (see `assertions.md` §1, §3, §12). `ASSERT`, `REQUIRE`, and
`FATAL` therefore all funnel through the same terminal fatal path
(`BeginFatal` → `FinishFatal*` → optional inspection break → `abort()`), and the
implementation comments that "debugger continuation never escapes"
(`modules/foundation/base/src/assert.cpp`). `CHECK` is the only recoverable form.

That contract is correct for shipping and for CI, but it removes a workflow that
is valuable during local iteration: when a developer hits a *development-only*
invariant (`ASSERT` / `ASSERT_F`) on their own machine with no debugger attached,
the only outcome is process death. Re-running to the same point, or attaching a
debugger after the fact, is often expensive (long scene loads, hard-to-reproduce
state). Historically this is exactly what an "assert dialog" with
*Continue / Break / Abort* provided; the assertion Rabin review
(`assertions-rabin-review.md`) discussed this idea and the original design
rejected it to keep invariants strictly terminal.

We now want the narrow, controlled version of that workflow back — without
weakening the shipping/CI contract, and without turning invariants into a
best-effort "ignore and continue" facility.

## Decision

Make **only** enabled `ASSERT` / `ASSERT_F` *resumable through explicit
developer action*, and only in a tightly scoped configuration. Concretely:

1. **Scope by kind.** `ASSERT` / `ASSERT_F` become resumable. `REQUIRE`,
   `REQUIRE_F`, `FATAL`, and `FATAL_F` remain unconditionally terminal in every
   build. `CHECK` / `CHECK_F` remain recoverable exactly as today (evaluate once,
   report best-effort, return `false`). The four-name taxonomy and macro
   expansions are otherwise unchanged.

2. **Explicit action only.** "Resumable" means a developer must make a
   deliberate, per-hit choice to continue. There is no automatic continue, no
   per-site "ignore always" registry, and no configuration that silently skips a
   failed invariant. The two mechanisms are:
   - **Debugger continuation.** When a debugger is attached, the inspection
     breakpoint is the resume point: continuing past it returns from the failed
     `ASSERT` to the caller, instead of proceeding to termination. `REQUIRE` /
     `FATAL` continue to terminate after the break.
   - **A separate Continue-once / Terminate dialog** presented **only when no
     debugger is attached**, and **exclusively in a local, non-CI Debug build**.
     "Continue once" resumes the failing `ASSERT` for that single hit;
     "Terminate" takes the normal terminal fatal path. Absent or non-interactive
     I/O (headless, no controlling terminal, no helper) is treated as
     "Terminate": there is no default-continue.

3. **Configuration matrix (the only place resume is enabled).**

   | Flavor (`LUDUS_BUILD_FLAVOR`) | Config | `ASSERT` enabled | Resumable `ASSERT`? | Mechanism |
   | --- | --- | --- | --- | --- |
   | Debug | `Debug` | Yes | **Yes, local non-CI only** | Debugger continue, or Continue-once dialog when detached |
   | Development / sanitizer | `RelWithDebInfo` | Yes | **No — report-only** | Report + inspection break if attached, then terminate |
   | Profile | `RelWithDebInfo` | No (`REQUIRE`/`CHECK`/`FATAL` only) | n/a | n/a |
   | Release / MinSizeRel | `Release`/`MinSizeRel` | No | n/a | n/a |

   Resumable behavior lives *only* in the Debug flavor, and even there only for
   locally built, non-CI binaries. Development is deliberately **report-only**:
   `ASSERT` reports and (if a debugger is attached) breaks, then terminates like
   today. This keeps automated Development runs deterministic while preserving
   the interactive workflow for hands-on Debug sessions.

4. **CI veto at runtime.** The continue path (dialog *and* debugger-resume-to-
   caller) is vetoed at runtime whenever the process detects a CI environment,
   even for a locally built Debug binary that is later executed under CI. A CI
   veto forces the terminal fatal path. This prevents an interactive dialog from
   hanging a CI runner and prevents "continue" from masking a real invariant
   failure in automation.

5. **SDK policy versioning.** Resumability is part of the SDK's assertion policy
   contract, not a per-translation-unit knob. It is expressed through the
   existing generated `assert_config.hpp` surface (a new numeric policy value
   alongside `LUDUS_ENABLE_ASSERTS` / `LUDUS_BREAK_ON_CHECK`), is recorded in the
   SDK manifest, and bumps `LUDUS_ASSERT_POLICY_VERSION`. As with the existing
   policy macros, predefinition/override is forbidden and multi-config
   generators remain rejected. Changing this policy intentionally recompiles
   consuming translation units.

The design document (`assertions.md`) and the spec carry the precise runtime
control flow, the owned-report lifetime rules across a resumed `ASSERT`,
headless/recursion/contention behavior, transport/helper loss, and the
validation gates. This ADR fixes the *policy*.

## Consequences

- The old "enabled invariant never returns" statement is **superseded for
  `ASSERT` / `ASSERT_F` only**, and only under the Debug-local-non-CI scope
  above. The statement stands unchanged for `REQUIRE` and `FATAL`, and for
  `ASSERT` in Development/Profile/Release. The design text is updated to state
  this precisely rather than leaving two contradictory claims.
- `ASSERT` gains a genuine "return to caller" path. That means an `ASSERT`
  failure is no longer proof that execution stopped: code after a resumed
  `ASSERT` runs with the invariant known-broken. This is acceptable **only**
  because the scope is a developer's own Debug session and the action is
  explicit; it is why Development stays report-only and why REQUIRE exists for
  invariants that must hold even in optimized local builds.
- The runtime gains a new failure sub-path (present/parse a decision, resume vs
  terminate) that must preserve every existing safety property: no allocation on
  the success path, bounded owned reporting before any prompt, recursion/owner
  contention still terminates immediately, and transport/helper loss degrades to
  Terminate. These are specified as requirements, not left to implementation.
- A new dependency on interactive I/O and CI detection is introduced **only** on
  the Debug failure path. It must not touch the success path, must not be a
  public-header dependency, and must reuse the existing detached OS-primitive
  boundary (`diagnostics_linux.cpp`) rather than adding STL to `assert.hpp`.
- This ADR does not authorize any code change. Implementation is sequenced by
  the spec's tasks and remains gated by the standard warning-clean build, unit,
  ASan/UBSan, format/tidy, and SDK-consumer checks, plus the new
  configuration/behavior gates the spec adds.

## Alternatives considered

- **Keep invariants strictly terminal (status quo).** Simplest and safest, but
  discards the interactive iteration workflow the team explicitly wants. This
  ADR chooses the narrow, scoped return of that workflow instead.
- **Make resume available in Development too.** Rejected: Development builds run
  in automation and as a shared "fast optimized" profile; a continue prompt or
  resume-to-caller there would make automated runs nondeterministic and could
  mask defects. Development is report-only.
- **A reusable per-site "ignore always" registry / dialog with Ignore.**
  Rejected, consistent with the original design: mutable per-site ignore flags
  tax successful calls and introduce shared-state/lifetime concerns. Only a
  per-hit, non-persistent "Continue once" is offered.
- **A single dialog for all fatal kinds.** Rejected: `REQUIRE` / `FATAL` mean
  "no valid continuation exists," so offering continue for them would contradict
  their contract. Only `ASSERT` / `ASSERT_F` are resumable.
