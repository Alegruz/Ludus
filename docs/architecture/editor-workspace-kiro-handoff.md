# Kiro handoff for Ludus Editor workspace

Implement the first Editor milestone: open/edit/save/reopen a project descriptor,
configure/build/run an executable in a separate process, stop it, and recover
from failures. The design selects optional C++23 Qt Widgets, one state owner,
and a small asynchronous Python tooling adapter. Scene editing follows later.

Read these committed files together:

- `.kiro/specs/editor-workspace/requirements.md`
- `.kiro/specs/editor-workspace/design.md`
- `.kiro/specs/editor-workspace/tasks.md`
- `docs/architecture/editor-workspace-research.md`
- `docs/architecture/editor-workspace-kiro-handoff.md`

The research includes original paraphrases of five actually reviewed Gems
articles and the decisions adopted/rejected from them. Kiro does not need the
ignored TOC, PDFs, screenshots, historical sample code or this conversation.
This package contains specifications only; every implementation/acceptance task
starts unchecked. Existing developer-tools D3/D4 cover more than this milestone.

## Prompt 1 Implement the milestone

```text
Implement Ludus's first Editor workspace milestone from the committed package.
Read AGENTS.md and applicable .kiro/steering instructions first, then read all
five files listed in docs/architecture/editor-workspace-kiro-handoff.md.

Execute E0.0 through E0.5 in dependency order. This authorizes implementation;
do not stop after writing another plan or ask for approval between ordinary
batches. Keep batches small and reviewable, preserve unrelated work, inspect the
actual current code/tool versions and record material deviations with evidence.
Do not mark a task complete without its required acceptance results.

Implement an optional OFF-by-default apps/editor/ludus_editor C++23 Qt 6 Widgets
application with private Core/Gui/Widgets dependencies, a single UI state owner,
typed actions/events, saved/draft project settings, failure-preserving atomic
save and a bounded log panel. Prove Qt compatibility under pinned Clang 18,
warnings-as-errors and -fno-exceptions first; do not weaken Ludus standards.
Do not require Qt in runtime-only/browser builds, Conan dependencies or the SDK.
Use Qt 6.4-compatible APIs and explicit prerequisite documentation/setup only.

Build the version-1 descriptor, exact argv-list editor and supported native
preset/provider contracts described in design.md. Open executes no project code.
Reuse existing managed tooling/bootstrap/environment/cache-repair helpers and
extract only shared CMake File API logic from RAD. Keep RAD behavior/tests intact.
Ordinary Run must preserve empty, quoted, Unicode, whitespace and shell-looking
arguments; it does not use RAD's restricted parser. Validate the executable
through the actual codemodel artifact path after each successful target build.
Never launch an old artifact after a failed/cancelled build.

Use one asynchronous Python tooling operation via QProcess and the specified
bounded JSON-lines/control protocol. No GUI-thread process waits, shell command
concatenation, detached runtime, silent setup/download, global event bus,
generic RPC/plugin framework or new engine process/GUI module. The runtime has
its own window; the Editor's status area is not an embedded framebuffer.

Implement and test all cancellation/close/EOF/lifetime rules. Stop accepted
before spawn prevents a later launch. Clean ordinary descendants with owned
process groups and non-reaping exit observation; keep process identity valid
until cleanup. Supervisor loss or unverified cleanup is CleanupUnknown, not
success. Handle stale timers/results, duplicate actions, reentrant widget updates,
bounded output without newlines, invalid UTF-8 and protocol backpressure.
Provide job details and explicit truncation/error diagnostics without telemetry.

The private PDFs and ignored game-dev-gems-toc.md are deliberately unavailable.
Use the committed self-contained paraphrases, contracts and primary API links.
Do not request PDFs or invent article content; no historical book code is needed.
Do not add scenes/ECS/reflection, assets/importing, gizmos, undo/redo, scripting,
embedded rendering, a code editor, debugger UI, hot reload or browser Editor.

Finish with shared parser/failure fixtures, state/widget tests, real process-tree
and argument tests, the engine sample and an external installed-SDK sample, plus
native Wayland GUI/runtime acceptance. Run all applicable pinned build/test/
format/tidy/sanitizer/header/build-budget/SDK gates in tasks.md with Editor ON
and an independent OFF tree. Offscreen tests do not prove interactive/native
acceptance. Record unavailable/skipped/failed gates honestly in the evidence doc.
Return changed paths, actual usage/preparation commands, verification evidence
and remaining limitations. Do not push/merge/deploy unless separately instructed.
```

## Prompt 2 Continue one implementation batch

```text
Continue .kiro/specs/editor-workspace. Read AGENTS.md, steering, the requirements,
design/tasks, research and current implementation/evidence. Implement the next
incomplete E0 batch whose prerequisites pass, including its acceptance gates,
then update the ledger and report actual results. Verify previously checked work
if evidence is absent. Preserve unrelated changes and scope. PDFs and ignored
TOC are not required. Do not rewrite the specification just to mark it complete,
weaken standards, auto-install dependencies or push/merge without instruction.
```

## Prompt 3 Review the completed implementation

```text
Review and repair Ludus Editor E0 against E01-E15 and design.md using actual
code, tests and recorded evidence. Check optional dependency/SDK isolation,
no-exceptions/warning/header standards, descriptor bounds and conflicting/failed
saves, exact argv/cwd preservation, codemodel target/artifact identity, failed
build with a stale binary, and separate-process runtime ownership.

Trace Stop/Close/EOF through every operation stage and demonstrate no late
launch, unowned signaling or ordinary child leak. Check non-reaping group cleanup,
TERM/KILL deadlines, abnormal supervisor handling, partial/version-invalid
protocol, double starts, reentrancy, stale timers/results, infinite/no-newline
output and slow readers. Give every finding a reproducer and source location;
repair verified defects with focused regressions and rerun affected gates.

Verify actual native Wayland acceptance, external SDK sample, Editor ON/OFF,
Debug/Development/sanitizer/format/tidy/header/budget checks. Distinguish passes
from skips and process start from a rendered frame. Confirm usage/recovery docs
and the evidence ledger match the implementation. Preserve E0 scope and unrelated
work. PDFs are unnecessary. Do not push/merge/deploy unless separately instructed.
```
