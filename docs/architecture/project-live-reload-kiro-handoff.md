# Kiro handoff: project loading and editing during play

Implement the committed project-live-reload package. It is self-contained;
private Gems PDFs/TOC, extracted pages and conversation history are unnecessary.
Architecture-only source commit: implementation tasks start unchecked.

Read these files together:

- `AGENTS.md` and applicable `.kiro/steering/` instructions
- `.kiro/specs/project-live-reload/{requirements,design,tasks}.md`
- `docs/architecture/project-live-reload.md`
- `docs/architecture/project-live-reload-research.md`
- `docs/decisions/0012-game-host-and-live-reload.md`
- Existing Editor E0, project-sdk-workflow and developer-tools contracts

## Prompt 1 Implement and publish the PR

```text
Implement Ludus project-live-reload, following all files listed in
docs/architecture/project-live-reload-kiro-handoff.md. Read AGENTS.md/steering
first and inspect actual current main/code/tools. Execute L0-L6 in dependency
order; implement the feature rather than return another plan. All implementation
tasks initially unchecked. Record actual evidence before marking complete.

Create codex/project-live-reload from current main. Preserve unrelated work,
commit focused batches, push and publish one implementation PR targeting main.
These branch/commit/push/PR operations are authorized. Keep it draft while required
acceptance is incomplete. Do not merge, tag, publish releases or push implementation
directly to main. Do not modify a separate SDK/other PR to solve this task.

Implement the optional editor document/tooling client, separately supervised
Qt-free GameHost, small versioned C-compatible gameplay function table, passed
host service API, immutable module/debug-symbol generations and static shipping
dispatch of the same game implementation. Preserve v1 executable Run, exact
argv/cwd, SDK policy, optional Qt and current static browser workflows. Use the
optional ludus.play.json sidecar; do not churn the concurrent v2 SDK descriptor.

Project-sdk-workflow may be a concurrent/unmerged PR. First support local installed
SDKs with existing canonical tooling. Reuse its installed backend when actually
available; do not duplicate SDK store/templates/release management or claim its
relocation acceptance from metadata. If a dependency is essential and unavailable,
record it, continue independent phases and leave that integration gate incomplete.

Prove ABI layout/no-exceptions/warnings/hidden symbols/dependency closure and
two-generation coexistence before extending reload. Engine modules remain static
in the host; gameplay must not link a second engine singleton runtime. Only the
matching SDK/architecture/runtime/policy/sanitizer ABI is accepted. No exported
C++ classes, STL objects, borrowed lifetime escapes or cross-module delete.

Build while playing by ending the canonical build lock after successful verified
atomic publication and protecting active images with leases. Resolve actual
MODULE_LIBRARY/host artifacts through the shared CMake File API. Watch events
are hints; never activate failed/cancelled/superseded/stale/partial artifacts.

Implement frame-boundary Quiesce/Snapshot/Stage/Commit/Retire. Keep old instance A
alive until B validates; staging must not mutate active resources or perform
external/gameplay side effects. Prepare all fallible work before host commit.
Recoverable rejection discards B and resumes A unchanged. Native crashes/hangs
are host failures, not catchable reload statuses. Retire old objects/callbacks/
resource references before releasing code; disable reload and require restart
when cleanup/residency is uncertain. Document cooperative limits rather than
build a universal shadow ECS/physics/audio world.

Use bounded tagged checkpoints/stable IDs and explicit migration or RestartRequired.
Add a small copied property schema and Qt inspector, atomic conditional typed edits,
source tuning documents, document undo/redo, session-only live edits and explicit
Apply to Document. Reconcile lost replies with bounded retained results/Status;
do not replay ambiguous mutations. Add one supported real asset replacement using
existing public resource APIs; no general importer suite, ECS/reflection generator,
global event bus/RPC framework, scripting, embedded viewport or binary patching.

Run all meaningful failure fixtures and native acceptance journeys in tasks.md:
load/build errors, stale callbacks, migration rejection, candidate crash/hang,
source edits while building, stop/close/switch races, protocol bounds/slow readers,
duplicate/lost results, property/undo/save conflicts and in-flight resource retirement.
Run 100 reloads under ASan/UBSan and measure resources/RSS plus actual iteration
timings. Prove real rendered change and real debugger stepping/symbol changes;
mocks/offscreen/process start do not prove native window/debugger behavior.

Use pinned C++23/Clang 18, Ludus aliases, no-exceptions, required warnings/format/
tidy/include/header/build-budget and SDK rules. Enable current optional test targets.
Run Debug/Development/ASan-UBSan, Editor ON/OFF, SDK/external consumer and affected
E0/RAD/Python/browser regressions. Write usage/recovery and evidence documents
specified in tasks.md. Distinguish PASS/FAIL/SKIP/UNAVAILABLE honestly.

After opening the PR, continue until all applicable checks for its exact current
head succeed and GitHub reports no merge conflicts. Read actual failing logs,
repair the implementation, resolve conflicts against latest main and rerun affected
gates. Do not use continue-on-error, skip required jobs, narrow triggers to hide
failures or confuse a green workflow summary with passed constituent acceptance.
If hardware/tooling acceptance cannot be completed, keep draft and identify the
concrete blocker instead of calling it ready. Return PR URL, final head/checks,
usage commands, evidence and remaining limitations. Do not merge.
```

## Prompt 2 Continue and repair the existing PR

```text
Continue project-live-reload on its existing implementation branch/PR. Read the
committed handoff/spec/research, current code and evidence. Verify checked tasks
with missing evidence; implement the next incomplete supported phase. Fetch main,
resolve conflicts without losing changes and inspect exact-head CI failures.
Repair real root causes and rerun affected pinned gates. Preserve required checks,
scope and other projects/PRs. Update the existing PR and evidence; no duplicate PR,
direct implementation push to main, merge, tags or release. Keep draft while any
required native/SDK/debugger/CI acceptance is blocked and report the actual blocker.
```
