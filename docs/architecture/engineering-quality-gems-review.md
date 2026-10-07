# Engineering quality reference review

Review date: October 7, 2026. Repository baseline: `01f5ecf` plus the current
working tree. The [engineering quality architecture](engineering-quality.md)
was drafted before reading the selected chapters, then revised using the
decisions below. This is a design review, not runtime or CI performance evidence.

Thanks to Steve Rabin, Greg Hjelstrom, Byon Garrabrant, Ricky Lung, J.L. Raza,
Peter Iliev Jr. and Matthew Jack for the debugging, profiling and QA ideas that
materially improved this proposal. Future code changes should retain relevant
attribution beside the implementation and link here; see
[contributor requirements](../../AGENTS.md).

## Discovery, source access and limits

Discovery used [game-dev-gems-toc.md](../../references/game-dev-gems-toc.md),
searching debugging, profiling, tests, builds, automation, coverage, memory,
assertions and error reporting. The index includes non-Gems books, but the five
readings actually used here are from Game Programming Gems 3, 4 and 8. A table
of contents located readings; it did not establish their claims.

Read the complete selected chapter ranges. Gems 3 and 4 were rendered and read
through local macOS Vision OCR; title/author/printed-page identity was also
visually checked. Gems 8 provided extractable text, with the coverage title page
visually checked. The Gems 8 local copy's copyright page identifies Course
Technology, part of Cengage Learning, and copyright 2011; do not infer its date
from secondary catalogs or the embedded 2010 control number. The ranges below
distinguish physical one-based PDF pages from printed pages.

The ignored local `references/` library is not shipped with a fresh clone or
offline documentation bundle. These source links require that library. All
essential design contracts and reading conclusions are written here and in the
architecture, so implementation does not require a book download. No PDF,
companion-CD source, historical binary or substantial source transcription is
copied into the repository. OCR/rendered research intermediates stay ignored
under `out/quality-design/`.

| Author(s), exact article | Publication/section | Printed pages | PDF pages |
| --- | --- | --- | --- |
| Steve Rabin, *The Science of Debugging Games* | Game Programming Gems 4, 1.1 | 5-18 | 22-35 |
| Greg Hjelstrom and Byon Garrabrant, *Real-Time Hierarchical Profiling* | Game Programming Gems 3, 1.17 | 146-152 | 149-155 |
| Ricky Lung, *Design and Implementation of an In-Game Memory Profiler* | Game Programming Gems 8, 4.6 | 402-408 | 417-423 |
| J.L. Raza and Peter Iliev Jr., *A More Informative Error Log Generator* | Game Programming Gems 8, 4.7 | 409-415 | 424-430 |
| Matthew Jack, *Code Coverage for QA* | Game Programming Gems 8, 4.8 | 416-427 | 431-442 |

## The Science of Debugging Games

Steve Rabin, *Game Programming Gems 4*, section 1.1, printed pages 5-18.
[Local chapter](../../references/Game%20Programming%20Gems%204.pdf#page=22).

The article organizes debugging around reproduction, collecting evidence,
isolating the cause, repairing it and testing the repair. Its examples distinguish
the invalid pointer observed at failure from the lifetime error that caused it.
It discusses controlled randomness/time/input, checking recently changed code,
optimized-build failures, capturing intermittent failures and testing related
cases rather than only the first symptom. It also emphasizes tools usable by
testers/designers and learning to collect useful failure information.

**Adopt:** Treat reproducibility as a CI output contract. Preserve the first
failure; record initial state, content, tick policy and input alongside a seed.
Use source history, reduction and known-good comparisons. Regressions verify
both the original repro and neighboring boundary/lifetime cases. Preserve
optimized and platform-specific tests and symbols rather than assuming Debug
success validates shipping behavior.

**Adapt:** Concurrent execution and real devices can remain nondeterministic.
Capture conditions/frequency and isolate scheduling failures without promising
that a seed reproduces thread schedules. A developer can deliberately serialize
or perturb a workload as an investigation; the production implementation still
needs correct synchronization and bounded progress.

**Reject as platform policy:** Automatic retries/rebuilds/reinstalls that erase
the original evidence, unreviewed latest-tool upgrades, broad warning pragmas,
printf diagnostics, crash-path allocation and automatic email delivery. These
historical suggestions do not supersede pinned toolchains, explicit errors,
Ludus logging or controlled evidence upload. The source's assertions about
Debug initialization are not a C++ guarantee.

**Architecture change:** Section 5 gains a precise reproduction contract and
original-plus-adjacent regression verification. Section 8 preserves first
occurrences and distinguishes observed failure from underlying cause.

## Real-Time Hierarchical Profiling

Greg Hjelstrom and Byon Garrabrant, *Game Programming Gems 3*, section 1.17,
printed pages 146-152.
[Local chapter](../../references/Game%20Programming%20Gems%203.pdf#page=149).

The article distinguishes sampling from explicit scoped timing and organizes
instrumented calling contexts into a browsable hierarchy. It displays parent
and total percentages, time/frame, time/call, calls/frame and unlogged remainder.
The implementation uses RAII, a current node, persistent topology and resettable
statistics; recursion and repeated children receive explicit treatment. It
acknowledges that this does not achieve sampling's resolution.

**Adopt:** Define profiler outputs around meaningful subsystem/calling context
and call frequency. Show unaccounted work. Combine scope events with sampling
and wait analysis, instead of treating one profiler view as complete execution
evidence.

**Adapt:** Derive hierarchy and inclusive/exclusive statistics offline from the
existing per-thread event stream. Identify the capture interval, lost events and
unfinished scopes. Overlapping workers are not additive frame wall time; nested
inclusive durations are not exclusive cost. Sampling/annotation overhead and
modern host clocks require measurement on the actual toolchain/hardware.

**Reject:** Another dynamically allocated producer-side tree, a single shared
current-node pointer, static-name-pointer equality as a portable event identity,
and Pentium tick/clock conversion. The article's clock explanation and historical
overhead are not adopted as modern CPU behavior or a performance guarantee.

**Architecture change:** Section 6 specifies capture completeness and metric
interpretation. The existing [profiling owner](profiling-final.md) and its
[literature review](profiling-literature-review.md) remain authoritative for
runtime recording; this review does not redesign that module.

## Design and Implementation of an In-Game Memory Profiler

Ricky Lung, *Game Programming Gems 8*, section 4.6, printed pages 402-408.
[Local chapter](../../references/Game%20Programming%20Gems%208.pdf#page=417).

The article separates live allocation count/bytes from allocation activity per
frame and relates allocations to a calling context. It explains why a CPU
scope's entry/exit memory difference cannot describe lifetimes: allocations can
be freed elsewhere. It records allocation-origin metadata and discusses freeing
from another thread. Capture, attribution and presentation are separate tasks.
The sample uses x86 allocation-function patching, in-band metadata and locked
statistics, with an external visualization client.

**Adopt:** Allocation origin survives a cross-thread free. Live/peak footprint,
churn and retained allocations are distinct diagnostics. A stable heap size
does not imply an allocation-free frame or absence of lifetime leaks.

**Adapt:** For CI, define expected quiescent boundaries for voice retirement,
reload generation disposal and content lifetimes. Report exact domain coverage
and incomplete/lost observations. Use existing tests and external heap tools
first; future internal observation follows the
[memory architecture](memory-profiling.md) and its
[reference review](memory-profiling-gems-review.md).

**Reject:** Runtime instruction patching, mandatory allocation headers and
global/per-origin locks inside hot allocation paths as the default Ludus design.
The article's sample cannot establish complete coverage of modern allocators,
drivers or browser memory. CPU scope nesting is not an ownership boundary.

**Architecture change:** Section 6 defines separate allocation-traffic,
live-footprint and post-quiescence retention acceptance metrics without adding a
new allocator implementation.

## A More Informative Error Log Generator

J.L. Raza and Peter Iliev Jr., *Game Programming Gems 8*, section 4.7, printed
pages 409-415.
[Local chapter](../../references/Game%20Programming%20Gems%208.pdf#page=424).

The article argues that a single message may omit the caller chain that explains
bad state, especially on artist/tester machines without a debugger. Its example
captures and symbolizes runtime stack information through a Windows XP/x86
DbgHelp wrapper and illustrates how callers can repeat generic failure messages.
The sample assertion prints a stack and waits for input.

**Adopt:** Failure reports should be useful away from the programmer's machine.
Pair an execution chain with exact executable/module/symbol identity and the
original failing operation. Avoid replacing the original cause with generic
errors emitted at every higher layer.

**Adapt:** Capture bounded safe addresses/records through supported platform
facilities and symbolize externally. Keep status/resource/configuration context;
the native stack does not describe asynchronous causality by itself. Preserve
correlation IDs where the existing subsystem supplies them. Report missing
symbols honestly and retain the first instance when grouping similar failures.

**Reject:** Inline register assembly, mutable singleton symbol handling in a
crash-critical path, loader work in a signal handler, `printf`, `getchar()` and
automatic network reporting. No C++ exception boundary is introduced. Existing
[assertion contracts](assertions.md) and
[diagnostic-helper workflow](../development/diagnostics.md) retain their safety
and noninteractive CI semantics. See the existing
[logging review](logging-review.md) for its broader evaluation of this chapter.

**Architecture change:** Section 8 makes stack context supplementary, binds
symbols to the build identity, and preserves safe capture versus external
postprocessing.

## Code Coverage for QA

Matthew Jack, *Game Programming Gems 8*, section 4.8, printed pages 416-427.
[Local chapter](../../references/Game%20Programming%20Gems%208.pdf#page=431).

The article connects QA playthroughs to named execution markers and expected
sets per level. Results identify reached, remaining and unexpected markers, and
separate runs can be merged. Stable feature labels help programmers and QA
communicate despite source refactoring. The chapter distinguishes unit tests,
functional playback and human observation, and notes that a level only uses a
subset of engine code. Its example uses static markers, a tracker and a small
feedback display; it proposes result history and offline text processing.

**Adopt:** Compiler line coverage and scenario coverage answer different
questions. A successful test process must prove the intended setup, reload,
resource or backend path ran. Give journeys small expected checkpoint sets,
content/build identity, missing/unexpected evidence and readable outcomes.

**Adapt:** Reuse existing test/host observations before adding runtime markers.
Pair every essential checkpoint with an invariant: reaching commit code does
not prove state publication was correct. Merge only compatible scenario/build
identities and keep per-run completeness visible. Review expectation changes in
source control; do not automatically redefine expected coverage from a new run.
Keep source coverage through LLVM as an independent, precise compiler facility.

**Reject:** A universal percentage across unrelated scenes/platforms, automatic
baseline growth from observed markers, always-on global trackers, a mandatory
QA GUI/database/network collector, and claims that marker hits prove correct
behavior. No companion implementation or frame-rate claim is transferred.

**Architecture change:** Section 5 adds semantic scenario evidence and section
10 adds negative controls for missing checkpoints, false outcomes, identity
mismatches and incomplete runs. This is the strongest addition beyond the
initial CI proposal.

## Selection and modern tooling checks

Other catalog entries, including *Using CppUnit To Implement Unit Testing*,
*Advanced Debugging Techniques* and *Squeezing More Out of Assert*, were located
but not read for this review. No claim is attributed to their contents. Ludus
already has Catch2 and a detailed assertion design; replacing them requires a
specific demonstrated gap. The catalog's rendering/network/allocator algorithms
do not justify adding unrelated CI services or engine architecture changes.

Modern tool mechanics were checked against primary documentation, separately
from the books:

- [LLVM UBSan](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html):
  diagnostics may recover; the executed check needs a deliberate failure policy.
- [LLVM source coverage](https://clang.llvm.org/docs/SourceBasedCodeCoverage.html):
  source mapping/profiles use a different backend from gcov-style `--coverage`.
- [Valgrind Memcheck](https://valgrind.org/docs/manual/mc-manual.html):
  uninitialized-value checks and custom-allocator annotations inform its scope.
- [clangd configuration](https://clangd.llvm.org/config):
  compilation-database selection and include diagnostics inform editor setup.
- [GitHub secure use](https://docs.github.com/en/actions/reference/security/secure-use):
  reviewed action pins, least privilege and isolation constrain CI execution.

The scenario model, report schema, phased rollout, lane ownership, limits and
cache/platform identity policies are Ludus engineering decisions informed by
these readings. No article prescribes the complete CI system, and none supplies
evidence that this proposal has achieved faster builds or fewer defects.
