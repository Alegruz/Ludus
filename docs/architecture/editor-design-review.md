# Editor design reference review

Review date: 2026-10-04. Baseline: `f2c34e9` plus the initial architecture and
interaction design recorded before reading the additional excerpts below.
The local [reference TOC](../../references/game-dev-gems-toc.md) selected these
entries. The PDFs/TOC are an ignored local library; links require that library.
This document records paraphrased lessons and Ludus decisions, not copied code.

## Ranked excerpts read for this increment

| Rank | Reference and inspected range | Relevance and adaptation |
| --- | --- | --- |
| 1 | *Designing the User Experience of Game Development Tools*, ch. 4 “Analysis”, PDF pp. 64–73 (printed pp. 53–62, with adjoining material) | Mental models, observing work, and task flows: organize work areas by authoring task, then evaluate with representative users. Do not expose internal architecture as the primary UI vocabulary. |
| 2 | Same book, ch. 5 “Design”, PDF pp. 90–96 (printed pp. 80–86) | Hierarchy, constraints, natural mapping: central authoring space; adjacent live controls; one shared capability-gated action for menu and toolbar; standard labeled controls. |
| 3 | Same book, ch. 6 “Evaluation”, PDF pp. 127–139 (printed pp. 116–129, including chapter transition) | Choose a working small change or prototype according to the question being measured; task-based heuristic review is an initial check, followed by actual users. Separate screenshot/logic evidence from native acceptance. |
| 4 | *Game Engine Toolset Development*, ch. 37 “Responsive UI During Intensive Processing”, PDF pp. 442–449 (printed pp. 423–430) | Worker completion, progress, cancellation and UI-thread boundaries. Retain existing Qt/process adapters; coalesce updates and preserve interaction state. Its Windows Forms/BackgroundWorker and exception examples are historical, not Ludus implementation guidance. |

The ch. 5 hierarchy page was also rendered and inspected visually. The UX PDF's
text extraction combines some page transitions; the physical PDF page ranges
above identify exactly what was inspected. TOC entries are at lines 3573, 3586,
3602 and 6447 respectively. These are targeted excerpt reviews, not claims to
have read each entire book.

Existing deeper reviews still support the backend design:
[workspace MVC/observer/RPC/debugging review](editor-workspace-research.md) and
[live reload, game tuning, and asset pipeline review](project-live-reload-research.md).
They retain their original reviewed ranges. The
[ranked reading guide](editor-reference-reading-guide.md) is a broader TOC-based
selection; its unreviewed entries remain reading candidates.

## Changes after the initial design

| Initial design | Revision following the review | Delivery |
| --- | --- | --- |
| Central Project/Audio tabs and side/bottom panels | Preserve discoverable commands for beginners; expose the same Play action objects in toolbar and menu; panel recovery is always available | S1 code and regressions |
| Keep platform styling and labeled controls | Add Project form mnemonics, accessible work-area/inspector/output names, and explicit inspector action labels with consequence tooltips | S1 code |
| Versioned local layout | Reject incompatible Qt versions as well as app versions and oversized settings; fallback restores all panels together | S1 code and corrupt/restart regressions |
| Future performance budgets | Add a concrete background-update invariant: no focused-field reset or scroll/selection loss on a heartbeat; measure it before adding a general event framework | S2 acceptance contract; current rendering still needs this follow-up |
| Generic user evaluation | Define an observable task script and score recovery/failure as well as completion; offscreen captures remain heuristic evidence | Interaction document and script below |

The shell's structure was retained after review. No excerpt justified switching
GUI stacks, adding a universal reflection/plugin system, or replacing working
process ownership. The chosen design favors concrete task improvements and
inspectable ownership over additional infrastructure.

## Current Qt API verification

Qt documents unique object names and the application version parameter for
[QMainWindow layout save/restore](https://doc.qt.io/qt-6/qmainwindow.html).
Its current documentation also limits restoration to trusted, locally generated
compatible state. The editor therefore never accepts layout data through a
project descriptor or host protocol. The private `workspace.json` contains base64 Qt
state/geometry blobs, app layout version, and exact Qt version; the whole file is
limited to 64 KiB, dock state to 32 KiB, geometry to 4 KiB. Bounds and version
checks provide recovery, not validation of an arbitrary untrusted Qt blob.

Verification prompted a persistence refinement: use a size-bounded JSON envelope
and [QSaveFile](https://doc.qt.io/qt-6/qsavefile.html) with direct-write fallback
disabled, matching recent-project history. This avoids rereading arbitrary old
settings during recovery writes. Canonical base64, envelope bounds, and decoded
blob bounds are checked before restoration. Accepted close atomically replaces
the owned file; failure logs a warning without blocking close or affecting
documents. These APIs compile against the project's Qt 6.4 minimum. This review did not
upgrade Qt or infer that newest upstream documentation changes the pinned build.

## Task script for native/user acceptance

Use a representative existing project. Record machine, display scaling, Qt
version, build profile, task time, mistakes, and assistance required.

1. Open a recent project; find its active preset and change/save an argument.
2. Switch to Audio, locate a resource, change a value, and distinguish draft save
   from preview. Launch Play and verify preview ownership is released.
3. Pause Play, inspect a property, apply to session, and explain what would
   persist only after copying to the tuning draft and saving it.
4. Hide Output and Live Inspector; recover each through View, then move/float
   panels and recover through Reset Layout. Restart and verify chosen layout.
5. Complete the same navigation with keyboard only; repeat at high DPI and
   light/dark palettes. Observe truncation, focus visibility, and panel reachability.

Native input, screen-reader operation, actual host rendering, and measured task
latencies remain acceptance work; unit tests and offscreen images do not certify
them. No external user study was performed in this change.
