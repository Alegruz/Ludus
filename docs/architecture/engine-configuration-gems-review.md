# Configuration literature review

The original architecture favored plain typed options, explicit layers, prepared
transactions, module ownership, and bounded control-plane work. The local
[game-dev-gems-toc.md](../../references/game-dev-gems-toc.md) identifies relevant
chapters. The complete selected chapters, rather than only their titles, informed
the refinements below. Thank you to their authors. No source code was copied.

| Consulted source | Printed / physical PDF pages | Adopted or adapted idea |
| --- | --- | --- |
| Wessam Bahnassi, “Game Tuning Infrastructure,” Game Engine Gems 2, ch.16 | 263–277 / [279–293](../../references/Game%20Engine%20Gems%202.pdf#page=279) | Share typed metadata and validation between authoring and runtime; distinguish inspection from application and resource lifecycle |
| Lasse Staff Jensen, “A Generic Tweaker,” Game Programming Gems 2, §1.18 | 118–126 / [115–123](../../references/Game%20Programming%20Gems%202.pdf#page=115) | Transparent use of ordinary options; attach type/range/help metadata outside hot values |
| Peter Dalton, “Registered Variables,” Game Programming Gems 8, §4.2 | 363–372 / [378–387](../../references/Game%20Programming%20Gems%208.pdf#page=378) | Skip dependent work when semantic values do not change; replace shared mutable dirty bits with per-owner generations |
| James Boer, “A Flexible Text Parsing System,” Game Programming Gems 2, §1.17 | 112–117 / [109–114](../../references/Game%20Programming%20Gems%202.pdf#page=109) | Separate authoring, schema validation, and runtime representation; retain source information |
| Charles Cafrelli, “A Property Class for Generic C++ Member Access,” Game Programming Gems 2, §1.7 | 46–50 / [43–47](../../references/Game%20Programming%20Gems%202.pdf#page=43) | Keep inspection metadata and ordinary member access separate; avoid generic property access in engine loops |
| Jason Gregory, Game Engine Architecture, 3rd edition, §6.5 | 470–479 / physical PDF 489–498 | Save deliberate overrides, preserving inheritance from future defaults |

Bahnassi improves the baseline by making application status a distinct concern:
a knob accepted by tools is not necessarily applied by the renderer. The core
has requested/active/pending state, but requires owner-selected publication and
resource preparation. GameHost's first schema deliberately uses NextLaunch
rather than claiming live device changes without a lifecycle contract.

Jensen supports the choice to keep `HostOptions` and future subsystem options
ordinary data. His historical hierarchical tweaker implementation is inspiration,
not a requirement to create a mutable singleton database or expose direct member
pointers. Ludus descriptors are immutable borrowed metadata with explicit module
lifetime. Local bindings carry context identity to reject accidental reuse.

Dalton's redirector chains solve broader cross-system variable wiring. Ludus
adopts semantic change observation rather than those chains: options snapshots
stay plain and per-group generations let consumers remember their own progress.
Provenance-only edits still advance the context revision for conflict detection.

Boer's separate preprocessing/compiled-token phases strengthen the authoring
boundary. Ludus uses TOML in an optional tool and validates cooked typed records
with the engine's actual schema. V1 uses the existing bounded JSON backend at
startup instead of introducing generic token replay, macro expansion, or another
binary codec without measurements. File/source labels survive parser destruction;
unknown line locations remain explicitly zero.

Cafrelli motivates reusable inspection while also exposing the cost and ownership
questions of general member wrappers. Ludus exposes finite typed descriptors and
copies owner options. It does not use property proxy reads in gameplay loops,
raw offsets, dynamic casts, or writable object addresses.

Gregory's configuration discussion makes sparse persistence a requirement rather
than an optimization. Saving merged effective state would pin platform/project
values and defeat default evolution. Reset therefore removes the preference
assignment; it does not copy the inherited value into the preference file.

Modern primary references reinforce these choices:

- Epic Games, [Console Variables and Commands](https://dev.epicgames.com/documentation/en-us/unreal-engine/console-variables-cplusplus-in-unreal-engine): source priorities, explicit typed usage, batch change observation, and safe change handling. Ludus uses fixed ranks and prepared owner transactions instead of hidden callbacks or a global console manager.
- O3DE, [Settings Registry](https://www.docs.o3de.org/docs/user-guide/settings/): explicit merge and application settings separation. Ludus keeps a finite exact schema and typed module options rather than a universal JSON tree in hot code.
- [TOML 1.0 specification](https://toml.io/en/v1.0.0): tables, comments, strict duplicate-key handling, and finite authoring syntax. This is a tool format only.
- Python, [tomllib](https://docs.python.org/3/library/tomllib.html): read-only TOML parsing available from Python 3.11; no source-location or preserving writer API is assumed.

The chapters improve metadata sharing, debugging, change observation, sparse
persistence, and separation of authoring from shipping. They do not establish a
need for generic reflection, XML, scripting, live RPC, raw member pointers, or
variable redirector chains. Historical benchmark figures are not Ludus results.

## Post-implementation research queue

Complete and validate the initial architecture through
[C1-C5](engine-configuration.md#initial-architecture-completion-and-improvement-sequence)
first. Capture that revision, its supported schemas/targets, workloads and
measurements as the comparison baseline, then investigate the resources below.
Backend/API checks needed to implement the existing design remain part of the
initial work; this research queue does not put a broader redesign ahead of it.

Unlike the consulted chapters above, these resources have only been screened
through their abstracts, publication metadata or official talk descriptions.
Their full readings and Ludus trials are queued. The possible applications are
our research questions, not claims that their ideas have been adopted or that
results from server/cloud systems transfer unchanged to a game engine.

### Venues to search

| Venue | Priority after the initial baseline | Topics and Ludus application |
| --- | --- | --- |
| [ICSE](https://conf.researchr.org/details/icse-2023/icse-2023-technical-track/47/Understanding-and-Detecting-On-the-Fly-Configuration-Bugs) | First | Runtime configuration updates, stale derived state, behavioral testing and update failures; examine production Live adapters and owner publication. |
| [USENIX OSDI](https://www.usenix.org/conference/osdi20/presentation/sun) | First | Testing actual configuration changes in the code that consumes them and detecting latent errors early; extend subsystem and bundle acceptance tests. |
| [GDC tools sessions](https://www.gdcvault.com/play/1026441/Tools-Tutorial-Day-The-System) | First for workflow | Tools as a connected workflow, iteration cost and usability; evaluate authoring, cooking, inspection, persistence and launching together. |
| [Journal of Systems and Software](https://doi.org/10.1016/j.jss.2021.111044) | When evaluating presets | Sampling, measurement and learning configuration spaces; plan bounded quality/device preset experiments and choose original papers from the survey bibliography. |
| [Empirical Software Engineering](https://doi.org/10.1007/s10664-023-10338-3) | When evaluating evolution | Configuration-dependent performance changes across releases; compare representative supported configurations rather than only defaults. |

These links are historical publication/session entry points, not a verified
upcoming conference schedule. Priorities and the mapping to Ludus are our
selection criteria, not venue endorsements of this architecture.

### Initial reading queue

Begin with CR-01 and CR-02 after C5. Review CR-03 alongside the integrated Editor
workflow; screen CR-04 through CR-06 for the relevant preset, evolution and
infrastructure work. None is a prerequisite to implementing the initial design.

| ID | Source and attribution | Full-review status | Question and possible experiment after review |
| --- | --- | --- | --- |
| CR-01 | Teng Wang, Zhouyang Jia, Shanshan Li, Si Zheng, Yue Yu, Erci Xu, Shaoliang Peng and Xiangke Liao, **Understanding and Detecting On-the-Fly Configuration Bugs**, ICSE 2023, pp.628-639. [Official session and preprint](https://conf.researchr.org/details/icse-2023/icse-2023-technical-track/47/Understanding-and-Detecting-On-the-Fly-Configuration-Bugs); [author artifact](https://github.com/wangteng13/Parachute) | Queued; abstract/metadata screened | For settings explicitly supported as Live, does startup with a value and changing to it at runtime produce equivalent observable behavior after owner application completes? Compare owner options and subsystem outputs under controlled initial conditions; test stale derived state, paired group updates and failed preparation. Respect documented transition semantics rather than assuming all settings or histories are equivalent. |
| CR-02 | Xudong Sun, Runxiang Cheng, Jianyan Chen, Elaine Ang, Owolabi Legunsen and Tianyin Xu, **Testing Configuration Changes in Context to Prevent Production Failures**, OSDI 2020, pp.735-751. [Paper, slides and talk](https://www.usenix.org/conference/osdi20/presentation/sun) | Queued; abstract/metadata screened | Can tests using actual cooked configuration inputs reveal failures that descriptor validation cannot? Parameterize suitable existing host/subsystem tests with representative bundles and assert behavior, while preserving tests whose intent depends on fixed values. |
| CR-03 | Laura Teeples, **Tools Tutorial Day: The System of Tools: Reducing Frustration in a Daily Workflow**, GDC 2019, 343 Industries. [Official session](https://www.gdcvault.com/play/1026441/Tools-Tutorial-Day-The-System) | Queued; official session description screened | Where does the author-to-launch workflow lose time or user intent? Evaluate cooking, preview, source explanation, reset, sparse save and launch as one task; compare completion time, errors and recovery steps before/after a bounded workflow change. |
| CR-04 | Juliana Alves Pereira, Mathieu Acher, Hugo Martin, Jean-Marc Jézéquel, Goetz Botterweck and Anthony Ventresque, **Learning Software Configuration Spaces: A Systematic Literature Review**, Journal of Systems and Software 182, article 111044, 2021. [DOI](https://doi.org/10.1016/j.jss.2021.111044); [2019 open preprint](https://arxiv.org/abs/1906.03018) | Queued; abstract/publication metadata screened | Which sampling and measurement methods fit bounded engine preset spaces? Follow promising survey references to original papers, then compare a sampled quality/device matrix with a small exhaustive reference using the same workloads; record coverage, measurement cost and prediction error if a model is trialed. |
| CR-05 | Christian Kaltenecker, Stefan Mühlbauer, Alexander Grebhahn, Norbert Siegmund and Sven Apel, **Performance Evolution of Configurable Software Systems: An Empirical Study**, Empirical Software Engineering 28, article 152, 2023. [Open-access article](https://doi.org/10.1007/s10664-023-10338-3) | Queued; abstract/metadata screened | Which performance regressions appear only for particular option combinations? Compare representative supported configurations across engine revisions with stable scenes/jobs and fixed hardware; record frame/job latency, memory and option interactions rather than extrapolating from the default configuration. |
| CR-06 | Tianyin Xu, Xinxin Jin, Peng Huang, Yuanyuan Zhou, Shan Lu, Long Jin and Shankar Pasupathy, **Early Detection of Configuration Errors to Reduce Failure Damage**, OSDI 2016, pp.619-634. [Paper, slides and presentation](https://www.usenix.org/conference/osdi16/technical-sessions/presentation/xu) | Queued; abstract/metadata screened | Which valid-looking infrastructure settings fail only when fallback, recovery or resource exhaustion occurs? Exercise those paths with candidate options in isolated tests before publication; evaluate useful early owner checks without adding side effects to pure schema validators. |

### Review and trial record

For each completed reading or experiment, record:

1. Source ID, review date and exact sections, pages or talk timestamps consulted.
2. The relevant idea, assumptions, limitations and differences from Ludus;
   distinguish inspiration from adapted source code.
3. The affected gate/module and a testable improvement hypothesis.
4. A bounded prototype, baseline revision, workloads, failure scenarios and
   comparison criteria chosen before evaluating the change.
5. Reproduction commands, seeds, hardware and toolchain/backend versions, plus
   links to tests, measurements and the implementation PR. Keep generated results
   in ignored `out/`.
6. Correctness outcomes and applicable before/after measures: initialization,
   prepare/commit/apply latency, allocations/storage, subsystem performance or
   workflow completion/error/recovery results. Include relevant tail behavior.
7. An adopt, defer or reject decision with evidence and links to any changed
   contracts and implementation attribution.

No source in this queue has a completed full review, trial or adoption record
yet. Preserve the consulted-source review above and extend it when a completed
reading actually informs implementation. Trials retain exception-free engine
code, bounded control work, ordinary owner options, sparse persistence and
explicit resource/publication ownership. A paper does not by itself justify a
global registry, scripting/constraint interpreter, automatic device mutation or
another shipping codec.
