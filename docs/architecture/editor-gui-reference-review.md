# Editor GUI systems: design and reference review

Reviewed 2026-10-06 against the browser editor baseline `7b420da` and the local
reference library; delivered on current main including the merged browser port. This review
supports [GUI systems](editor-gui-systems.md) and
[ADR 0023](../decisions/0023-editor-presentation-and-document-core.md).
It changes architecture documents, not editor implementation. There are no
frontend comparison benchmarks or newly qualified platforms in this review.

## Proposal recorded before reading

Before this new article pass, the proposal selected a private Qt-free document/
command core; Qt Widgets native presentation; a bounded Qt/Wasm preview with
full-authoring gates and a conditional DOM alternative; engine-rendered scene
viewports; explicit host capabilities; and isolated gameplay. It proposed
validated gestures, revision-aware jobs, a task-focused layout and measured
large-data/idle behavior. Qt Quick, ImGui, custom widgets and dynamic plugins
were not default dependencies.

This baseline was recorded first in a separate scratch document. The sections
below distinguish what the readings changed from ideas already present. The
initial proposal was not a performance result or a claim that its interfaces
were implemented.

## Method and source locators

The ignored [combined TOC](../../references/game-dev-gems-toc.md) selected focused
readings. Then the actual local PDF excerpts were extracted and read, including
figures/footnotes where present. Wihlidal's architecture Figure 8.1 was rendered
and inspected; Lightbown's reflowed PDF was also rendered to verify its page
layout. Printed and PDF pagination differ. PDF numbers below are one-based;
selected excerpts do not imply the rest of a chapter or book was reviewed.
Copyright/title pages were checked rather than trusting PDF metadata.

The private PDFs/TOC are not required to use this committed design, and are not
copied into the source tree. Thanks to the authors for the ideas described below;
these are original paraphrases and explicit adaptations, not transcriptions or
historical sample-code ports.

## Actually reviewed excerpts

### Graham Wihlidal — Game Engine Toolset Development

Thomson Course Technology, 2006, ISBN 1-59200-963-8.
[Author-hosted book](https://www.wihlidal.com/files/getd_full_book.pdf).
TOC entries at lines 6227, 6246, 6329, 6353, 6447 and 6484 identify these chapters.

| Chapter / printed pages | Local PDF pages | Useful idea | Concrete adaptation or departure |
| --- | --- | --- | --- |
| 6, Measurement Metrics for Tool Quality, 43–48 | 62–67 | Judge quality through maintenance, testability, portability and reliability | Add a property/command maintenance exercise and correlated failure diagnostics; no unsupported quality score or historical standards claim |
| 8, Distributed Componential Architecture Design, selected 57–61 | 76–80 | Separate reusable logic from tool entry points; Figure 8.1 illustrates composition | GUI and headless authoring share operations; no COM, remoting stack or obligatory web service |
| 20, Using the Property Grid Control with Late Binding, selected 199–203 | 218–222 | Property metadata controls grouping, description and editing | Explicit schema/editor adapters; metadata cannot bypass validated transactions; no .NET reflection |
| 25, Using Direct3D Swap Chains with MDI Applications, selected 243–247 | 262–266 | Share resources while managing individual view surfaces and failure | Add multiple-surface/device acceptance and explicit per-view state; no copied D3D9 reset assumptions |
| 37, Responsive UI During Intensive Processing, selected 423–428 | 442–447 | Worker progress and cancellation keep the interface responsive; completion races matter | Bound/coalesce progress, preserve terminal results, distinguish cancellation request from acknowledgement; reuse current supervision |
| 43, MVC Object Model Automation with CodeDom, selected 531–537 | 550–556 | Presentation-independent object operations allow automation/testing | Headless commands before macros; no Application singleton, CodeDom/JIT or arbitrary GUI-object scripting |

The remaining plugin, asset-browser and worker examples are candidates in the
[reading catalogue](editor-reference-reading-guide.md), not evidence of
implemented behavior. The book's old managed APIs and graphics claims cannot
establish modern Qt/Metal/WebGPU compatibility.

### David Lightbown — Designing the User Experience of Game Development Tools

CRC Press, 2015, local first-edition text. This is not the later edition now
listed on the [author's site](https://www.uxofgametools.com/).
TOC line 3602 identifies Evaluation; ch. 5's indexed subsections guide the design
reading.

| Section / printed pages | Local PDF pages | Useful idea | Concrete change |
| --- | --- | --- | --- |
| Ch. 5, Excise and Progressive Disclosure, selected 104–114 | 114–125 | Repetitive navigation costs time; contextual actions reduce travel, but hidden actions harm discovery | Remove permanent unused rows/panels; keep common commands in visible stable menus/context strips; narrow layouts use tabs/drawers |
| Ch. 6, Evaluation, selected 117–129 | 128–139 | Match evaluation to the question; drawings cannot prove efficiency or large-data behavior | Use sketches for layout comprehension and runnable prototypes for latency, IME, accessibility, large lists and compositor integration; test role-appropriate tasks |

The interface direction is now qualified by task success, recovery and actual
keyboard/text behavior, rather than screenshot preference. Context menus and
command search supplement discoverable commands. We do not adopt an arbitrary
universal participant count, copy another application's UI, or treat the book's
illustrations as contemporary toolkit performance evidence.

### Adrian Hirst — A GUI Framework and Presentation Layer

*Game Engine Gems, Volume One*, edited by Eric Lengyel, ch. 6; local Jones and
Bartlett edition, copyright 2011, ISBN 9780763778880. The catalogue groups this
volume under its 2010 publication entry; the inspected edition has the 2011
copyright. [Series editor's site](https://gameenginegems.com/).
TOC line 5585 identifies the article. Read selected sections 6.2 and 6.3,
printed pp. 125–128, PDF pp. 153–156; checked the chapter author/title.

Thanks to Hirst for the separation of data, presentation and control, and the
warning that communication mechanisms have overhead. This supports small direct
operations within one owner and messages only across real boundaries. The
article targets a runtime front end; its base component/update/render example
does not justify implementing an entire editor toolkit or a fixed-rate GUI loop.
Ludus runtime UI and editor presentation keep separate responsibilities.

### Robert Nystrom — Command

*Game Programming Patterns*, ch. 2, local reflowed PDF; read PDF pp. 35–36 and
40–49, including actor decoupling and Undo/Redo. The reviewed local PDF does not
provide stable corresponding printed pagination. TOC line 7825 identifies the
chapter. [Author's chapter](https://gameprogrammingpatterns.com/command.html).

Thanks to Nystrom for making intentions explicit and retaining the state needed
to reverse them. This sharpens the catalogue/operation distinction and one
history item per gesture. Ludus adapts the principle with stable identities,
explicit errors, bounded history and atomic preparation. Raw runtime pointers,
unbounded per-action allocation and replaying arbitrary external effects are
not adopted.

## What changed after the reading

| Initial proposal | Post-reading refinement | Acceptance that exposes mistakes |
| --- | --- | --- |
| Separate core and shell | One usable authoring API, exercised without Qt, GPU or window; direct calls before event machinery | Headless and GUI fixtures produce the same document results |
| Metadata-driven properties | Reusable semantic editors feed validated commands; no bypass through reflective setters | Mixed multi-selection, invalid buffer and failed batch preserve prior state |
| Gesture Undo | Explicit begin/preview/accept/cancel; one authoritative history; revision differs from savepoint | Undo back to saved content is clean despite a newer revision; cancelled drag never saves |
| Async cancellation | Request/acknowledgement/terminal outcome and reserved terminal delivery | Completion races, stale identities and progress floods cannot publish incorrectly |
| Engine viewport adapter | Shared compatible device/resources, independent surface/state, failure invalidates dependants | Resize, detach, multiple views and device loss; no stale picking or leaked ownership |
| Contextual, quieter UI | Common commands stay discoverable; stable menus/context strip plus optional panels | Novice discovery and expert repeated tasks; no clipped form in narrow browser |
| Performance targets | Separate usability sketches from integration/performance prototypes and measure maintenance | Actual datasets, real text/accessibility and renderer tasks; record property/command change effort |

Some contracts, such as save completion after further edits, follow from Ludus's
ownership/failure requirements and engineering analysis. They are not attributed
to an unread paragraph merely because a chapter title is related.

## Contemporary primary documentation checked

Checked the current documentation on 2026-10-06, using the Qt 6.10 family relevant
to the browser build. Upstream API facts are separated from Ludus design judgment.

| Primary source | Verified fact used | Design implication |
| --- | --- | --- |
| [Qt model/view](https://doc.qt.io/qt-6.10/model-view-programming.html) | Data can live in a separate repository behind model/view adapters | Toolkit models project domain state |
| [QAbstractItemModel](https://doc.qt.io/qt-6.10/qabstractitemmodel.html) | Model APIs are not thread-safe; view-connected updates belong on the GUI thread; row notification and incremental fetch APIs exist | Copied worker results, UI-thread deltas and lazy models |
| [QQuickWidget](https://doc.qt.io/qt-6.10/qquickwidget.html) | This embedding adds an offscreen render pass and disables the threaded render loop | Do not mix UI stacks just for a new visual skin |
| [Qt WebAssembly](https://doc.qt.io/qt-6.10/wasm.html) | Browser presentation requires WebGL; documented accessibility support is basic and complex widgets may lack support | Full browser authoring must pass semantic/text/viewport gates; current preview is not proof |
| [Qt styling](https://doc.qt.io/qt-6.10/stylesheet.html) | Qt style sheets customize widget presentation | Bind a small shared visual system; outer HTML styling does not theme Qt widgets |
| [Dear ImGui](https://github.com/ocornut/imgui) | Upstream lists accessibility and full international shaping/bidirectional limitations | Diagnostics rather than the primary authoring interface |
| [KDDockWidgets](https://github.com/KDAB/KDDockWidgets) | Advanced Widgets/Quick docking implementation exists | Evaluate only against a needed workflow; no dependency added |

These sources do not establish which toolkit is fastest for Ludus. No Electron,
Tauri, Qt Quick, DOM or ImGui prototype was benchmarked in this documentation
change. The [system design](editor-gui-systems.md) defines the experiments that
would justify a different choice. Existing native process cleanup, project setup,
configuration, audio and reload contracts retain their original owners.

## Deferred review after the first implementation

Status: **planned; revisit after the first usable implementation**. This section
preserves the follow-up research context requested on 2026-10-06. Implement the
accepted [GUI systems design](editor-gui-systems.md) and
[ADR 0023](../decisions/0023-editor-presentation-and-document-core.md) first.
The absence of a deadline allows careful implementation and evaluation; it does
not require expanding the architecture before a working baseline exists.

### Revisit trigger and baseline

Start this review when **M1–M4** in the
[delivery sequence](editor-gui-systems.md#performance-evidence-and-delivery) have
passed their acceptance gates: portable authoring core, redesigned shell,
qualification of the declared host/frontend capabilities, and one end-to-end
asset/scene workflow. This is a milestone trigger, not a calendar reminder.
Required M3 platform/frontend experiments still happen during implementation;
research follow-up must not postpone those checks. Record unsupported hosts or
features explicitly rather than treating a preview as full authoring support.

At that handoff, record the baseline commit, supported host/frontend matrix,
reproducible datasets, task results, performance measurements and unresolved
issues. Link to implementation/validation evidence instead of copying the
contracts here. Include observed editing friction and the effort to add or debug
one property and command. That evidence gives the research review a concrete
problem to improve. Revisit specialist resources again when M5 introduces a
specific material, animation or audio workflow.

### Resources to return to

The order below is a Ludus relevance judgment. **GDC Tools Summit and ACM UIST
are the first destinations**; the other venues serve specific questions. These
are places to select relevant work, not a requirement to read every proceedings
volume. The linked editions are starting points; find the relevant newer
programs and publications when the review actually begins.

| Venue / publication | What to look for | Baseline to compare |
| --- | --- | --- |
| [GDC Tools Summit / GDC Vault](https://gdcvault.com/) | Shared document models, Undo, tool reuse, pipelines, iteration and tools testing; start with the four talks below | M1 core, M4 workflow and the maintenance exercise |
| [ACM UIST — User Interface Software and Technology](https://uist.acm.org/2025/) | Direct manipulation, selection, interactive programming and creative authoring systems | M2 interactions, M4 scene manipulation and M5 specialist tools |
| [ACM CHI — Human Factors in Computing Systems](https://chi2026.acm.org/) | Task evaluation, discoverability, accessibility and expert workflows | M2 task success and M3 keyboard, text and accessibility results |
| [SIGGRAPH Courses](https://www.siggraph.org/wp-content/uploads/2024/09/Courses.html) and [Production Sessions](https://s2026.siggraph.org/program/production-sessions/) | Artist workflows, production pipelines and specialist authoring; select editor-relevant work | M4 asset/scene loop and M5 authoring requirements |
| [Qt World Summit](https://www.qt.io/qt-world-summit-2026/schedule) / [Qt resource library](https://www.qt.io/development/resources) | Production Qt architecture, model/view, rendering integration, UI testing and platform behavior | Actual M2/M3 implementation issues; supplement the domain design |
| [ACM TOCHI — Transactions on Computer-Human Interaction](https://dl.acm.org/journal/tochi) | Interaction models and rigorous evaluation of creative/expert tools; first journal to search for editor UX questions | An observed usability problem with a repeatable task |
| [ACM TOG — Transactions on Graphics](https://dl.acm.org/journal/tog) | Interactive modeling, animation and design systems that fit a named authoring feature | M4/M5 workflows and measured preview/interaction constraints |
| [Computer Graphics Forum](https://www.eg.org/wp/eurographics-publications/cgf/) | Graphics research and review articles for comparing specialist techniques | Alternatives for a specific M5 feature before selecting an algorithm |

#### First four talks

Titles, speakers, years and official descriptions were checked on 2026-10-06.
**The recordings and slides have not been reviewed in this design pass.** The
questions below are proposed Ludus experiments, not findings attributed to the
speakers. Access to a description does not establish access to the full source.

| Read first | Verified source | Question for Ludus after implementation |
| --- | --- | --- |
| 1 | Niklas Gray, Our Machinery, GDC 2020: [Tools Summit: Writing Tools Faster: Design Decisions to Accelerate Tool Development](https://gdcvault.com/play/1026597/Tools-Summit-Writing-Tools-Faster) | Can common document operations and view adapters reduce the effort of adding a second tool? Compare with the M1 maintenance result; assess the costs before adopting a broader framework. |
| 2 | Kai Zhang, NetEase Games, GDC 2022: [Tools Summit: Immutable Data for Editors for 'EVE Echoes'](https://gdcvault.com/play/1027628/Tools-Summit-Immutable-Data-for) | Would persistent immutable data simplify Undo and UI updates enough to justify its allocation, memory and migration costs? Existing immutable preview snapshots alone do not answer this; compare against the implemented command/history model. |
| 3 | David Lightbown, Ubisoft, GDC 2021: [Tools Summit: How Ubisoft Builds Tools that Are More "Intuitive"](https://www.gdcvault.com/play/1027312/Tools-Summit-How-Ubisoft-Builds) | Can a small set of consistent interaction patterns improve discovery, repeated editing and error recovery? Evaluate actual M2 tasks with users rather than inferring success from a cleaner screenshot. |
| 4 | David Paris, Playground Games, GDC 2020: [Tools Summit: From 0-1000: A Test Driven Approach to Tools Development](https://www.gdcvault.com/play/1026631/) | Which headless, host and end-to-end tests would catch the failures observed in M1–M4 with acceptable runtime and maintenance cost? The title's test count is not a Ludus target. |

The [book/article catalogue](editor-reference-reading-guide.md) remains the
candidate inventory for `game-dev-gems-toc.md`. The earlier sections of this
review record excerpts actually consulted and changes already adopted. Select
additional chapters for an observed problem, read the relevant text and verify
its attribution before treating a catalogue title as design evidence.

### Review procedure and decision record

1. Pick a baseline problem from the recorded task failures, friction, performance
   or maintenance evidence. Define what improvement would be meaningful before
   selecting a technique.
2. Read the full relevant talk/slides or paper sections. Record exact authors,
   title, venue/year, stable URL/DOI and section/page/timestamp locators. Mark an
   unavailable source as pending; an abstract supports selection, not adoption.
3. Extract the useful idea, its assumptions and departures needed for Ludus.
   Compare it with the existing implementation and simpler local fixes. Preserve
   the repository's error, ownership and module-boundary contracts.
4. Build a bounded experiment against the same baseline tasks and datasets.
   Record the commit, machine, OS/browser, build profile and tool versions.
   Measure the quantities relevant to the problem: task success/time/errors,
   input/frame latency and stalls, idle CPU/wakeups, peak memory/allocation,
   browser payload/startup, or property/command change and debugging effort.
   Exercise affected native and browser paths, including macOS when claimed;
   preserve text/accessibility, Undo/save and failure/recovery gates.
5. Decide **adopt, reject or defer** using the results and implementation,
   migration, dependency and ongoing maintenance costs. Keep negative results.
   Do not replace a toolkit or introduce a general framework on venue reputation
   alone. For a deferred item, name the missing evidence and revisit condition.
6. Update the existing canonical design/interaction/workflow owner and its
   acceptance criteria when an idea is adopted. Amend or supersede an ADR only
   when the decision changes. Thank and credit consulted authors near affected
   code as required by [the contributor guide](../../AGENTS.md); this review
   supplements those comments. Deliver each improvement in a reviewable change.

Append completed investigations here using this compact record; the resource
list above is not a completed investigation:

```text
Review ID and date:
Baseline commit, hosts, task/dataset and observed problem:
Source: authors; exact title; venue/year; URL/DOI; consulted locators:
Source status: description only / full relevant material reviewed / unavailable:
Useful idea, assumptions and Ludus departures:
Candidate change and simpler alternative:
Experiment commit, environment, method and acceptance criteria:
Results against baseline; correctness/accessibility/recovery checks:
Decision: adopt / reject / defer; reasons and maintenance/migration cost:
Design/ADR/code/PR links; owner and next action or revisit condition:
```

### Context for the next implementation/research session

Read the contributor rules, the current GUI systems design, ADR 0023 and this
section first. Check the implementation and milestone evidence before assuming
that the portable core or a frontend is complete. Finish the first usable
architecture while retaining its required qualification experiments. Once the
M1–M4 baseline is accepted, use its observed problems to select the sources above
and run the review procedure. Improve the system through measured, bounded
changes and preserve rejected/deferred findings for later work.
