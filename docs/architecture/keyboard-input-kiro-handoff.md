# Kiro handoff: build keyboard input

This package is ready to push as Markdown. Start Kiro from a checkout containing
these five files:

- `.kiro/specs/keyboard-input/requirements.md`
- `.kiro/specs/keyboard-input/design.md`
- `.kiro/specs/keyboard-input/tasks.md`
- `docs/architecture/keyboard-input-research.md`
- `docs/architecture/keyboard-input-kiro-handoff.md`

The design selects ordered events plus compact keyboard state, one simulation
consumer, and a small action map. It extends current Wayland and supports
headless injection. The research includes the locally read Gems findings;
Kiro does not need the gitignored PDFs or this Codex conversation. All code,
measurements, and implementation gates remain outstanding.

## Prompt 1 - Implement the complete milestone sequence

Copy this into Kiro after pushing the files:

```text
Implement Ludus's keyboard-only input system using the committed specification.
Read AGENTS.md and applicable .kiro/steering/ instructions first, then:
- .kiro/specs/keyboard-input/requirements.md
- .kiro/specs/keyboard-input/design.md
- .kiro/specs/keyboard-input/tasks.md
- docs/architecture/keyboard-input-research.md

Execute M0 through M5 in dependency order and keep the task/evidence ledger
current. This authorizes implementation; do not stop after drafting another
plan or require approval between ordinary milestones. Make batches small and
reviewable. Revalidate the current checkout rather than assuming the research
baseline still matches. Reuse compatible existing input work. Preserve unrelated
changes and specs. Record material deviations with technical evidence.

Build the smallest architecture that meets K01-K15: native events feeding a
backend-independent owned reducer, compact held/edge state, bounded ordered
transitions, main-thread consumption once per simulation step, and integer
button/Axis1D bindings with in-memory rebinding. Add native Wayland keyboard
support and display-free production-path tests; preserve nonblocking pump and
headless window behavior. Implement optional bounded diagnostics/replay fixtures.

Pay particular attention to a down/up pair between updates; multiple taps;
zero or multiple simulation steps per render; repeat suppression; physical key
mapping; focus enter held-key baselines; cancellation versus release; overflow
recovery; switching maps while held; callback ownership and destruction; seat
capability/global removal; keymap fd closure; Wayland prepare/read/cancel rules.
Every externally supplied enum/code/capacity must be validated before indexing.
Keep published snapshots stable until the next ConsumeStep. Do not query OS
input from gameplay or run gameplay from callbacks.

Follow C++23, Ludus aliases, existing naming, noexcept/status errors, no engine
exceptions, zero hot-path allocation/locks, lightweight public headers, correct
module dependencies, and installed SDK boundaries. Read memory/container policy
before choosing storage/ownership. Never raise budgets, weaken warnings, or
enable engine exceptions merely to pass. Do not add SDL/GLFW/XKB, a worker,
general event bus, ECS, allocator/string library, config parser, UI framework,
mouse/gamepad, text/IME, browser adapter, rollback, or durable replay files.

The source PDFs are deliberately unavailable in this checkout. Use the committed
paraphrases and primary source links; do not request PDFs, invent book content,
or block implementation on missing historical sample code. Treat research as
rationale, and requirements/design as the intended behavioral contract, subject
to authoritative repository standards and evidence-based corrections.

Finish with core/action/lifecycle regressions, real compositor verification,
allocation/performance evidence, an input demo, and an installed SDK consumer.
Run all required pinned build/test/format/tidy/sanitizer/header/build-budget/SDK
checks listed in tasks.md, with Wayland both enabled and disabled. Report every
unavailable, skipped, or failed check accurately; do not call M5 complete without
its gates. Return changed paths, actual API usage, verification evidence, and
remaining limitations. Do not push/merge/deploy unless separately instructed.
```

## Prompt 2 - Continue one batch at a time

Use this instead of Prompt 1 if smaller Kiro runs are preferable; repeat it to
advance through the six milestones without resetting the specification:

```text
Continue .kiro/specs/keyboard-input. Read AGENTS.md, applicable steering, the
requirements/design/tasks files, and docs/architecture/keyboard-input-research.md.
Inspect the checked-out implementation and the evidence ledger. Implement the
next incomplete milestone whose dependencies pass; finish all its tests and
acceptance gates, then update the ledger and report changes/evidence. If a
previous box is checked without evidence, verify it rather than assuming done.
Do not rewrite the spec or broaden keyboard-only scope. PDFs are not required.
Follow all repository standards and preserve unrelated work. Record unavailable
checks honestly. No automatic commit/push/merge/deploy is requested.
```

## Prompt 3 - Final independent review within Kiro

Use after implementation, before merging:

```text
Review the implemented keyboard system against AGENTS.md and K01-K15 in
.kiro/specs/keyboard-input, using actual code/tests and the evidence ledger.
Look for lost taps, repeated catch-up-step activations, phantom focus-enter
presses, stale action state after overflow/reset/rebinding, cross-window routing,
callback lifetime bugs, leaked keymap fds, incorrect native-code conversion,
unbounded allocation/work, and private/heavy headers leaked into the SDK.
Trace each finding to a reproducible sequence and source location. Check native
and headless builds, sanitizer/format/tidy/header/budget/SDK results, and real
compositor evidence. Distinguish complete input replay from wrapped/lost traces
and whole-game determinism. Repair verified defects with focused regression
tests; rerun affected checks and update evidence. Report any remaining blockers
without marking skipped gates passed. Preserve keyboard-only scope and unrelated
work. Do not push/merge/deploy unless separately instructed.
```
