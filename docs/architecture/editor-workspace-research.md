# Ludus Editor workspace research and design decisions

Historical E0 baseline: later implementation expanded this milestone. The
[2026-10-06 GUI systems target](editor-gui-systems.md) and
[reference review](editor-gui-reference-review.md) guide future authoring/core
extraction and platform qualification; they do not retroactively change the E0
scope or claim its later contracts are implemented.

Research date: 2026-10-01. Design baseline is committed main at
`e33320a0cec1cd9be6d47bd89042eff77c393ce4`. This package defines a first Editor
workspace for Kiro; it implements no Editor code and makes no performance claim.
Read the [handoff](editor-workspace-kiro-handoff.md) and
[architecture](../../.kiro/specs/editor-workspace/design.md).

## Design before the literature review

The initial design selected an optional same-repository C++ Qt Widgets app,
a single controller/state owner, a small Python CLI adapter reusing build tools,
a versioned project descriptor, and a separate runtime window. This gives a
complete open/edit/save/build/run workflow while scene/asset systems develop.
The reference commit has native Platform/RHI and tooling but no completed native
Editor GUI/rendering integration. Locally uncommitted shader/resource work was
not treated as an implemented dependency of this plan.

Qt is chosen for E0's forms, menus, text input and desktop interaction. Dear ImGui
would be reasonable for a later renderer-connected tool, but this milestone
would first need native input/text/render backend integration. A web/Electron
shell introduces another application/runtime stack; a custom Ludus GUI makes
E0 depend on a substantial engine feature. A Python-only GUI would reduce C++
work but introduce a separate bindings dependency and move the eventual native
Editor away from the repository's application/toolchain conventions. These are
Ludus-specific tradeoffs, not rankings of those technologies.

The design deliberately avoids a generic command framework, a runtime editor
module, global event bus, custom GUI/reflection system and full debugger service.
Small typed actions, explicit phases and a one-operation bridge are enough.

## Article discovery and review method

Discovery used the local ignored `references/game-dev-gems-toc.md`, searching
editor, GUI, middleware, state, IPC, debugging and data-driven topics. Five
articles below were actually reviewed. The four Game Engine Gems 1 chapters
were read by text extraction; the MVC explanation and RPC sequence diagram were
also rendered and inspected. Game Programming Gems 4 is image-based apart from
watermark text, so all fourteen pages of Rabin's chapter were rendered and read
visually. A TOC title alone was not treated as evidence of an article's content.

PDF page numbers below are one-based file positions; printed page numbers are
separate. The following are original paraphrases and Ludus design decisions,
not copied sample code. Private PDFs, extracted text, screenshots and the
ignored TOC are not part of this commit. Kiro needs none of them: normative
behavior, schemas, operations, failure rules and tests are committed in the spec.

## Five articles actually read

### 1. Middleware integration and maintenance cost

Jason Hughes, "What to Look for When Evaluating Middleware for Integration,"
Game Engine Gems 1 (2010), chapter 1, printed pp. 3-14, PDF pp. 31-42.

The article evaluates middleware through integration/modularity, memory/I/O,
logging and error behavior, stability, source quality, platform support,
licensing and support costs. Its strongest contribution here is evaluating
actual integration burden rather than a feature list. Its runtime/console
allocation advice is not automatically a requirement for a desktop Editor.

Apply: select only Qt Core/Gui/Widgets and prove a minimal window with the actual
compiler, exception/warning flags and platform plugins. Make Editor OFF avoid
dependency discovery entirely. Record exact packages/versions/notices and keep
Qt out of installed runtime interfaces. Explicitly handle ordinary file/process
errors; inspect allocation/error limitations rather than assuming all desktop
failure paths are recoverable.

Reject: a custom per-widget allocator or VFS adaptation project for E0; hiding
Qt in an engine-wide wrapper solely to pretend it is interchangeable; adopting
the chapter's historical license opinions as legal conclusions. Use current
upstream dependency documentation and the installed package's notices instead.

Design impact: strengthened the dependency spike and the measurable Editor-OFF/
SDK boundary gates (E01/E02), plus explicit OOM limitations. The book does not
recommend this specific Qt version or prove its compatibility with Ludus.

### 2. Separating the GUI from application state

Adrian Hirst, "A GUI Framework and Presentation Layer," Game Engine Gems 1
(2010), chapter 6, printed pp. 123-141, PDF pp. 151-169.

The chapter explains model/view/controller responsibilities and the maintenance
cost of coupling logic to presentation. It distinguishes visible/highlightable/
selectable controls and presents a reusable component/composite GUI framework.
It also discusses text/localization and the overhead of unnecessarily elaborate
message passing. Its sample framework is an in-game GUI, not a desktop editor.

Apply: keep project/operation state in WorkspaceState, actions/transitions in
EditorController, and presentation in MainWindow. Compute capabilities from state
instead of independently toggling controls in many callbacks. Use ordinary Qt
layouts and controls for focus/text input. Typed local function calls/signals are
sufficient; a general message bus is unnecessary. Updating widgets must not feed
back into unintended actions.

Reject: copying its raw-pointer component hierarchy, bespoke rendering/font/
input backend, CRC-based GUI IDs or GUI-authoring editor. Qt supplies those
presentation concerns for this milestone. The game viewport remains separate.

Design impact: added a capability/transition contract, QSignalBlocker rendering,
reentrancy rules and a UI test seam (E04/E09). The MVC split was in the baseline;
the review made its ownership and feedback-loop limits concrete.

### 3. Explicit state transitions and notifications

Ron Barbosa, "The Game State Observer Pattern," Game Engine Gems 1 (2010),
chapter 26, printed pp. 415-431, PDF pp. 443-459.

The chapter shows why scattered conditions become fragile when system behavior
depends on state, then uses state-change notifications to coordinate components.
Its illustrative implementation is a C# singleton with subscribed observers and
runtime subject-type dispatch. Those mechanisms are examples, not mandatory
parts of the useful principle.

Apply: one owner validates transitions, stores operation phase and publishes a
state change. Keep document loaded/dirty facts and last result separate from
operation phase. Derive allowed actions centrally. Define notification lifetime
and reentrant-action behavior; callbacks carry job identity so old notifications
cannot affect a new workspace. Test event sequences without real widgets/tools.

Reject: global singleton state, arbitrary observer subscriptions/casts, a
reflection/event framework, and independently mutable busy booleans. For this
small app, one controller and context-bound typed Qt connections suffice.

Design impact: the explicit Idle/Starting/Configuring/Building/Launching/Running/
Stopping/CleanupUnknown enum, stale-job rules and sequence tests (E09/E10).

### 4. Process boundaries and fallible remote work

Kurt Pelzer, "Inter-Process Communication Based on Your Own RPC Subsystem,"
Game Engine Gems 1 (2010), chapter 28, printed pp. 445-457, PDF pp. 473-485.

The article explains request/response marshalling, procedure/version identity,
transport separation and why remote calls can fail differently from local calls.
It discusses both blocking and asynchronous calls and examples connecting editors,
games and diagnostic tools. Its broad cross-machine RPC system is larger than E0.

Apply: send owned serializable values with protocol version, operation and job
identity; never pass pointers or hide remote latency as a synchronous UI call.
Treat missing/invalid terminal results as failures, not successful work. Do not
retry build/run after an ambiguous outcome. Keep stdout protocol framing separate
from child output and control cancellation. Supervise process lifetime and make
uncertain cleanup observable.

Reject: custom network RPC, generic remote procedure registries, reconnect,
shared memory and a game-side command server. A private stdio adapter suffices;
there is no live gameplay state editing in this milestone.

Design impact: explicit asynchronous protocol, result/exit consistency, framing
limits, bounded output credit, cancellation priority and CleanupUnknown handling (E07/E10/E11/E14).
Linux session/deadline/non-reaping mechanics are our implementation choices,
verified through current APIs and future real-process tests, not supplied by
this historical chapter.

### 5. Reproducible debugging and regression evidence

Steve Rabin, "The Science of Debugging Games," Game Programming Gems 4
(2004), chapter 1.1, printed pp. 5-18, PDF pp. 22-35.

The chapter develops a disciplined loop of reproducing a failure, collecting
clues, testing causes, repairing the underlying problem and verifying the fix.
It emphasizes minimizing interacting systems/randomness, inspecting boundaries,
useful state/event logging and preventing regressions. Broader examples include
runtime diagnostics, input recording and memory debugging.

Apply: record exact commands/cwd, selected settings, descriptor digest, transitions
and actual exit/signal/cleanup outcomes. Reproduce adapter failures independently
of the GUI using controlled subprocess fixtures. Test build failures with a stale
binary already present, late events after cancellation, invalid arguments/files,
output overflow and lost parent/supervisor. Add a regression for each demonstrated
lifecycle defect. Preserve useful terminal errors even when ordinary logs roll off.

Reject: a deterministic whole-game replay system, automatic crash upload, generic
memory debugger or exception-based crash handler. Existing sanitizers/assertions
and external debuggers remain the tools for those jobs. Historical advice to
upgrade compilers or suppress warnings is not authorization to change pinned
Ludus policies.

Design impact: Copy Job Details, failure-first fixtures, noisy-output bounds and
an evidence ledger that cannot turn skips into passes (E05/E07/E11/E12/E15).

## Candidates deferred without content claims

The TOC also lists The Magic of Data-Driven Design, Save Me Now!, the Game Asset
Pipeline, Context-Sensitive HUDs for Editors and Game Tuning Infrastructure.
Those may be useful for scene/document authoring, importing or live tuning.
They were not reviewed for this package, and no design claim is attributed to
them. Keeping E0 a workspace avoids turning a title search into a reason to add
reflection, assets or runtime editing prematurely.

## Current primary technical references

These sources support API facts; the bounded budgets, descriptor shape, workflow
and ownership decisions remain Ludus proposals. Latest online docs may include
APIs newer than the reference Qt/Python/CMake versions; implementation must prove
availability with the pinned/reference installations.

| Source | Verified fact and use |
| --- | --- |
| [Qt Widgets](https://doc.qt.io/qt-6/qtwidgets-index.html) | Widgets supplies desktop controls/layout facilities; select it for the private shell. |
| [QProcess](https://doc.qt.io/qt-6/qprocess.html) | Async process signals, separate output channels and program/argument-list launch exist. Synchronous waits can freeze the UI. Use its async surface. |
| [QSaveFile](https://doc.qt.io/qt-6/qsavefile.html) | Temporary-file replacement reports write/commit failure; direct-write fallback removes atomic replacement protection. Keep fallback disabled. |
| [QJsonDocument](https://doc.qt.io/qt-6/qjsondocument.html) and [QJsonParseError](https://doc.qt.io/qt-6/qjsonparseerror.html) | JSON parsing reports errors; schema and Unicode/size validation remain application responsibilities. |
| [Qt exception safety](https://doc.qt.io/qt-6/exceptionsafety.html) | Ordinary errors use codes, but allocation/exception recovery has limitations. Verify selected usage with no-exceptions compilation; do not claim comprehensive OOM recovery. |
| [QTextDocument](https://doc.qt.io/qt-6/qtextdocument.html#maximumBlockCount-prop) | A block-count limit exists; also enforce byte limits for huge single-line output. |
| [Python 3.10 subprocess](https://docs.python.org/3.10/library/subprocess.html) | Argument lists, session creation and pipes are supported; complete-output capture can consume unbounded memory. Use bounded streaming. |
| [Python 3.10 selectors](https://docs.python.org/3.10/library/selectors.html) | Read/write readiness multiplexing supports the private supervisor loop. |
| [Python 3.10 os](https://docs.python.org/3.10/library/os.html#os.waitid) | waitid/WNOWAIT permits exit observation without immediate reaping, supporting safe group cleanup identity. |
| [CMake File API](https://cmake.org/cmake/help/latest/manual/cmake-file-api.7.html) | Named queries return codemodel/target artifact information. Use codemodel-v2 fields also supported by managed CMake 3.29.6. |

Local code evidence: engine.py supplies managed tools/tool_env, bootstrap checks,
configure policy and preset paths; rad_debugger.py already owns codemodel query/
executable resolution and its tests. Existing developer-tools.md proposes a
process-based tooling bridge and separate-process play. Reuse those boundaries,
without inheriting RAD's restricted argv parser for ordinary Run.

## Verification status and residual work

Design/research are complete enough for implementation planning. Qt compatibility,
implementation, real process cleanup, native GUI acceptance, resource bounds and
SDK integration are unverified until Kiro performs tasks.md. Qt is absent on the
research host; apt-cache lists base 6.4.2+dfsg-21.1build5 and Wayland 6.4.2-5build3
as candidates. This package installs no dependency or changes engine code.

The design now specifies ordinary descendant cleanup and explicit uncertainty
for supervisor loss. It does not pretend that process groups contain escaped
sessions or that stdout proves the runtime presented a frame. Future embedded
viewports, scene authoring and debugger providers need their own requirements;
they do not justify widening E0.
