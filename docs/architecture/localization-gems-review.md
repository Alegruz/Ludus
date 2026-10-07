# Ludus Localization Literature Review

**Status:** Evidence informing the proposed [localization architecture](localization.md).
No implementation or Ludus performance measurements are included.

The initial design established readable message identity, typed formatting,
immutable snapshots, explicit locale policy, and separation from text layout.
After that design was written, the review searched
`references/game-dev-gems-toc.md` (local reference index), read the relevant
local chapters, and revised the architecture. The catalog covers books beyond
the Gems series; its direct text-localization entry is in *Game Programming
Golden Rules*.

The useful result is a more concrete content and tooling contract: generate code
bindings from the message schema, keep exact readable keys in release catalogs,
retain source provenance, prepare coherent content before activation, and let
narrative logic choose semantic lines independently of localized wording.

## Evidence and selection

The local reference index and book PDFs are not distributed with the engine.
PDF page numbers below are one-based physical pages in the local copies. Printed
page numbers are given separately. Text extraction located/read the chapters;
the scanned Golden Rules section was read visually and through OCR. Its title
page confirms Martin Brownlow as author. The dialog chapter's opening page was
also inspected visually to verify its author, title and printed page number.
Technical conclusions come from chapter contents, not TOC titles alone.

| Source | Read scope | Relevance |
| --- | --- | --- |
| Martin Brownlow, *Game Programming Golden Rules*, “Localization of Text Assets” | Printed 91–92; PDF 110–111 | Direct localization example and its limitations |
| James Boer, *Game Programming Gems 2*, §1.17 “A Flexible Text Parsing System” | Printed 112–117; PDF 109–114 | Authoring/cooked separation, source diagnostics, shared code/data symbols |
| Stefan Reinalter, *Game Engine Gems 3*, chapter 14 “Compile-Time String Hashing in C++” | Printed 197–205; PDF 184–192 | Literal binding and production collision checks |
| Jason Hughes, *Game Engine Gems 2*, chapter 20 “Pointer Patching Assets” | Printed 345–357; PDF 361–373 | Packed coherent resources, explicit dependencies and offline inspection |
| Baylor Wetzel, *Game Programming Gems 8*, §3.10 “Scalable Dialog Authoring” | Printed 323–334; PDF 338–349 | Separating dialog intention from wording and recording constraints |
| Jason Gregory, *Game Engine Architecture*, third edition, §§6.4.4.5–6.4.4.6 | Printed 466–470; PDF 485–489 | Supporting localization scope and text/voice authoring example |

The first five are the targeted readings; Gregory is supporting context from
the same catalog. Earlier [string-system review](strings-gems-review.md) is useful
context but did not substitute for reading the selected chapters in this task.

Font-instancing and texture-atlas chapters inform the existing Text architecture.
They do not establish message grammar, language fallback, translation workflow,
or snapshot ownership. The closest-string entry is a possible diagnostic aid,
already discussed in the strings review; its original chapter was not reread
here and it contributes no new localization decision. Spatial hashes, GPU
texture algorithms and network bit packing have no direct bearing on this
design. No inference is attributed to their unread contents.

## Changes after reading

| Initial design | Final refinement | Evidence and judgment |
| --- | --- | --- |
| Stable message keys and a checked binder | Retain counted literal descriptors; generate typed helpers from the same schema as content | Boer and Reinalter support shared symbols and early literal work; generated Ludus helpers are an adaptation |
| Validated immutable catalogs | Specify a sorted readable key directory, explicit key/schema relation and release collision proof | Brownlow's two parallel files expose an ordering risk; exact-key retention is Ludus's correction |
| Cooked offset storage | Detail contiguous record sections, coherent dependency closure, and an offline dump/footprint report | Hughes supplies packing, dependency and introspection ideas; portable offsets replace pointer patching |
| Authoring revisions and diagnostics | Carry optional file/record source maps through cooking and generated bindings | Boer motivates provenance; runtime status errors and digest-based conflict checks are Ludus policy |
| Separate text, subtitle and voice preferences | Introduce semantic `LineId`, independent subtitle/voice realization, recording revisions and boundary-based replacement | Wetzel separates intention and wording; Gregory demonstrates paired text/audio authoring; exact lifetime policy is Ludus's design |
| Candidate publication | Require visible-view readiness as well as catalog readiness | Coherent activation follows Hughes's dependency discussion; atomic screen preparation is a further correctness refinement |

The final architecture incorporates these changes directly. The review does
not preserve a competing obsolete design that an implementer must reconcile.
Some readings reinforce baseline choices rather than replace them; the table
distinguishes concrete refinements from source claims.

## Direct localization example

**Source:** Martin Brownlow, “Localization of Text Assets,”
*Game Programming Golden Rules*,
printed pages 91–92, PDF pages 110–111.

The example separates a file of identifiers from a language-specific file of
text. It loads matching lines into a hash list, discards the identifier file
afterward, and returns the input key when a lookup fails.

**Apply:** Keep language-independent identity and language-specific presentation
separate. Define understandable missing-text behavior in development.

**Improve:** Author each translation against an explicit key, validate the common
schema, and generate index relationships during cooking. Keep readable exact
keys in release catalogs for collision-safe binding and diagnosis. Return a
status plus a deliberate presentation fallback; returning a key is not proof
that required localization succeeded.

**Reject:** Parallel-line order as identity, removing the only exact key evidence,
hash-only equality, and treating all messages as unformatted flat strings.
The short example does not supply plural semantics, bidi layout, concurrency,
translation review, or modern language-pack ownership.

## Authoring and compiled content

**Source:** James Boer, §1.17 “A Flexible Text Parsing System,”
*Game Programming Gems 2*,
printed pages 112–117, PDF pages 109–114.

The chapter explains human-editable text, shared definitions between code and
data, source file/line diagnostics, and conversion of processed tokens to a
binary representation with character values in a lookup table. Its parser also
allows preprocessing and case-insensitive dictionary compilation.

**Apply:** Generate message descriptors/typed helpers from the canonical schema,
validate source files in tools, and preserve source provenance in development
packages. Cook readable authoring records to compact records with dictionary
indices and byte storage.

**Adapt:** Localization keys remain exact case-sensitive bytes. All numeric
ranges, output limits, and source versions need validation. Do not serialize
backend-private MessageFormat objects: dynamic ICU patterns still parse during
explicit context preparation.

**Reject:** A new general scripting preprocessor, linked token lists, assertions
for malformed translation inputs, unchecked numeric conversion, and the
chapter's historical loading-speed estimate as a Ludus prediction. JSON and
the maintained MessageFormat parser already solve the relevant parsing tasks.

## Literal binding and collision handling

**Source:** Stefan Reinalter, chapter 14 “Compile-Time String Hashing in C++,”
*Game Engine Gems 3*,
printed pages 197–205, PDF pages 184–192.

The chapter moves repeated literal hashing to compile time, discusses overload
selection around arrays and pointers, and calls for production measures to check
collisions through a string database/wrapper.

**Apply:** Describe fixed message keys once, retain counted spelling and schema
expectations, and bind them explicitly on the cold path. A generated helper may
carry a fingerprint to narrow candidates; a runtime binding then uses a checked
owner/index instead of rehashing a literal each frame.

**Adapt:** Use simple C++23 generation/constant evaluation when appropriate.
Keep the exact key collision proof in every build and test forced collisions.
The generated schema is more valuable than making a hash itself the public ID.

**Reject:** Recursive templates, a universal 32-bit FNV policy, hash-only
persistence, and implicit runtime string-to-ID conversion. These would add
build cost or hide allocation, identity and failure. The chapter provides no
evidence that a localization binder needs a particular hash or table algorithm.

## Packed coherent catalogs

**Source:** Jason Hughes, chapter 20 “Pointer Patching Assets,”
*Game Engine Gems 2*,
printed pages 345–357, PDF pages 361–373.

The chapter moves resource preparation to tools, collects coherent data into
contiguous blocks, describes symbolic binding and explicit dependencies, and
recommends metadata and offline introspection. Its sample has limitations in
pointer width and per-structure alignment; its runtime patches pointers.

**Apply:** Pack catalog directories/literals/patterns coherently, declare fallback
dependencies before loading, and publish only after required contributors are
ready. Expose catalog coverage, packing utilization and memory estimates in an
offline inspector. Retain provenance for debugging.

**Adapt:** Keep checked offsets and lengths as offsets. Immutable bytes work
with a native mapping or an owned wasm buffer without modifying the package.
Use versioned fixed-width encodings and validate all arithmetic. Measure actual
load/preparation and old-plus-new peaks.

**Reject:** Raw native object dumps, pointer/vtable patching, historical optical
drive assumptions, and a new generic pointer-fixup system. ICU object layouts
are not portable catalog data. Compression and mapping remain optional measured
delivery choices, not prerequisites for the first localization runtime.

## Dialogue intention and localized realization

**Source:** Baylor Wetzel, §3.10 “Scalable Dialog Authoring,”
*Game Programming Gems 8*,
printed pages 323–334, PDF pages 338–349.

The chapter separates response intention from the actual words spoken. It uses
topics, response types and authored behavioral groups for intent selection. It
explicitly leaves linguistic realization outside its scope and observes that
recorded voice introduces a production bottleneck. Intent tests need not depend
on free text or translated wording.

**Apply:** Narrative code decides the semantic line/realization. Localization
resolves its text and voice resources. Gameplay tests assert semantic outcomes;
translation tests assess presentation. Keep speaker, performance revision and
translator context attached to that line.

**Adapt:** Treat subtitle/voice selection, cue alignment and mid-line switching
as explicit application policies with retained revisions. A stable `LineId`
supports shared context without using translated sentences as behavior keys.

**Reject:** Moving the chapter's AI planner, trust model or cultural-group
hierarchy into Localization. The chapter does not implement modern message
grammar, translation review, or voice/subtitle synchronization; those contracts
are Ludus's own design. Product locale never selects a character's gameplay
beliefs or behavior implicitly.

## Supporting engine architecture reference

**Source:** Jason Gregory, §§6.4.4.5–6.4.4.6,
*Game Engine Architecture third edition*,
printed pages 466–470, PDF pages 485–489. The nearby encoding/string sections
were consulted for context; the findings here concern the named localization
sections and case-study text.

The discussion expands localization beyond strings to audio, images, direction
and layout. Its Naughty Dog example describes an authoring system for text and
speech assets, with stable identifiers and UTF-8 text/subtitles.

**Apply:** Include typed localized assets, voice/subtitle identity, context and
translator-facing search in the product architecture. A language picker alone
does not establish localization completeness.

**Adapt:** Keep explicit per-context preferences and deterministic release inputs.
The authoring service exchanges records; it does not become a runtime dependency.

**Reject:** A process-global language column, mandatory MySQL, generic `wchar_t`
storage, unchecked hash identity, or historical market/rating examples as current
requirements. Modern standards and current product policy define Unicode and
market behavior; the book is architecture context, not legal guidance.

## Contemporary sources and remaining evidence

The design's contemporary sources were inspected separately from the books:

- [Unicode MessageFormat 2](https://unicode.org/reports/tr35/tr35-messageFormat.html)
  establishes the standardized syntax/model; the
  [released ICU4C API](https://unicode-org.github.io/icu-docs/apidoc/released/icu4c/classicu_1_1message2_1_1MessageFormatter.html)
  exposes a technology-preview boundary. This supports a versioned future codec
  without treating that implementation as an initial production dependency.
- [ICU message guidance](https://unicode-org.github.io/icu/userguide/format_parse/messages/),
  [error design](https://unicode-org.github.io/icu/userguide/icu/design.html), and
  [data build tooling](https://unicode-org.github.io/icu/userguide/icu_data/buildtool.html)
  inform complete-message authoring, the exception-free adapter, and measured
  locale/feature slicing.
- [BCP 47](https://www.rfc-editor.org/rfc/rfc5646),
  [RFC 4647](https://www.rfc-editor.org/rfc/rfc4647.html),
  [UAX 9](https://unicode.org/reports/tr9/),
  [UAX 14](https://unicode.org/reports/tr14/), and
  [UAX 29](https://unicode.org/reports/tr29/)
  define the corresponding tag, matching and Unicode algorithms. The message
  fallback graph and publication protocol are additional Ludus policy.
- [HarfBuzz scope](https://harfbuzz.github.io/what-harfbuzz-doesnt-do.html) separates
  shaping from paragraph layout. Repository code confirms Ludus's current
  paragraph/font/browser gaps.

No chapter establishes a universally fastest modern localization system, and
no API documentation proves native/wasm behavior or allocation guarantees for
Ludus. Dependency integration, translator round trips, conformance, real rendered
language coverage and workload measurements remain explicit implementation gates
in the final architecture. No companion-CD source code was copied into the engine.

## Post-implementation research backlog

Recorded 2026-10-06. Implement and validate the initial localization architecture
through L0-L5 first, then use the resources below to investigate improvements in
L6. The [architecture](localization.md#implementation-phases-and-revisit-criteria)
owns the feature contracts and exit gates. Capture its accepted implementation
revision, representative corpus, toolchain/backend/data versions and workload
measurements before comparing alternatives. Required dependency and standards
checks remain part of implementing the initial design.

This queue preserves the conference/journal recommendations for that later pass.
Official venue programs, session descriptions and paper metadata/abstracts were
screened; the full talks and papers below have not been reviewed for adoption.
The questions and milestone mappings are Ludus research proposals. Keep the
completed Gems review above intact and add adoption findings only after reading
and validating the relevant source.

### Venues to search

| Venue | Priority and topics | Ludus work it could inform |
| --- | --- | --- |
| [Unicode Technology Workshop](https://unicode.org/events/utw/2026/) and historical Internationalization and Unicode Conference proceedings | First: ICU/CLDR data packaging, locale behavior, Unicode algorithms and multilingual text display | L0 backend/data integration; L2 formatting; L4 paragraph layout and font policy |
| [FOSDEM localization talks](https://archive.fosdem.org/2025/schedule/event/fosdem-2025-5561-solving-the-world-s-localization-problems/) | First: MessageFormat design, UI adoption and localization tooling | L2 profile/API alternatives; L5 interchange/tooling; L6 richer-profile evaluation |
| [GDC Vault localization sessions](https://www.gdcvault.com/play/1015787/Localization-Microtalks-Around-the-World) | Game integration: typography and language-specific production constraints | L4 rendered test scenes; L5 editor, assets and dialogue requirements |
| [LocWorld / Game Global](https://gameglobal.events/) | Workflow: game localization and QA practices; also screen LocWorld's [technical track](https://locworld.com/conference-tracks/) | L5 translator handoffs, review workflow and release acceptance criteria |
| [Digital Translation: International Journal of Translation and Localization](https://benjamins.com/catalog/dt) | Research: digital translation workflows and game-localization studies | L5 message context, authoring/editor workflow and linguistic QA |
| [The Journal of Specialised Translation (JoSTrans)](https://www.jostrans.org/article/view/7269) | Research: game translation and audiovisual/linguistic constraints | L5 translator metadata, subtitle/dialogue realization and quality evaluation |
| [TACL / WMT evaluation research](https://aclanthology.org/2021.tacl-1.87/) | Conditional: expert review, error classification and translation-quality evaluation if machine-assisted authoring is introduced | L5 linguistic QA and approval evidence; no automatic runtime translation dependency |

Digital Translation continues the former *Journal of Internationalization and
Localization*; search both archives. Conference links are reading entry points,
not requirements to attend an event. Check current programs and access before
planning attendance. A talk about MessageFormat 2 does not establish production
readiness of a particular C++/wasm backend; the architecture's existing adoption
gates still apply.

### Initial reading queue

| ID | Source and attribution | Full-review status | Question for a later review and trial |
| --- | --- | --- | --- |
| LR-01 | Markus Scherer, **Putting ICU to Work**, Unicode Technology Workshop 2026. [Tutorial](https://www.unicode.org/events/utw/2026/talks/putting-icu-to-work/) | Queued; tutorial description screened | Can ICU integration or linguistic-data packaging be simplified or reduced while preserving supported behavior? Compare initialization, code/data size, resident memory, explicit failure handling and native/wasm parity. |
| LR-02 | Peter Constable and Ned Holbrook, **Demystifying Unicode Text Display: From Unicode Code Points to Positioned Glyphs**, Unicode Technology Workshop 2026. [Tutorial](https://www.unicode.org/events/utw/2026/talks/demistifying-unicode-text-display/) | Queued; tutorial description screened | Which font-selection, bidi and shaping interactions expose gaps in our paragraph integration? Add mixed-script/direction, missing-font and contextual-shaping cases to the rendered corpus; compare correctness and layout cost. |
| LR-03 | Robin Leroy, **Thirty years of line breaking**, Unicode Technology Workshop 2026. [Talk](https://www.unicode.org/events/utw/2026/talks/thirty-years-of-line-breaking/) | Queued; talk description screened | Which implementation strategies and historical pitfalls matter for our wrapping path? Compare proposed changes against pinned Unicode conformance fixtures and representative paragraphs, including memory and tail latency. |
| LR-04 | Eemeli Aro and Ujjwal Sharma, **Solving the world's (localization) problems**, FOSDEM 2025, Inclusive Web devroom. [Session, slides and recording](https://archive.fosdem.org/2025/schedule/event/fosdem-2025-5561-solving-the-world-s-localization-problems/) | Queued; session description screened | Would a versioned MF2 profile or different tooling improve authoring and interchange? Trial only after backend/profile gates pass; compare semantics, translator round trips, output preservation, preparation/formatting cost and migration effort. |
| LR-05 | Carmen Mangiron and Minako O'Hagan, **Game Localisation: Unleashing Imagination with "Restricted" Translation**, *The Journal of Specialised Translation* 6, 2006, pp.10-21. [Article](https://www.jostrans.org/article/view/7269); [DOI](https://doi.org/10.26034/cm.jostrans.2006.735) | Queued; metadata/abstract screened | Which translator constraints should our context, preview and dialogue tools expose? Review real messages with qualified translators, then assess proposed metadata/UI changes through complete-message and voice/subtitle authoring journeys. |
| LR-06 | Markus Freitag, George Foster, David Grangier, Viresh Ratnakar, Qijun Tan and Wolfgang Macherey, **Experts, Errors, and Context: A Large-Scale Study of Human Evaluation for Machine Translation**, *Transactions of the Association for Computational Linguistics* 9, 2021, pp.1460-1474. [Paper](https://aclanthology.org/2021.tacl-1.87/); [DOI](https://doi.org/10.1162/tacl_a_00437) | Conditional queue; metadata/abstract screened | If machine-assisted translation is added, how should contextual expert review and error/severity labels support approval? Validate an adapted QA procedure on game messages; do not assume results from the paper's translation corpus transfer unchanged to games. |

### Review and experiment record

After the initial implementation gates pass, screen the venue archives for
specific limitations observed in that baseline. Retain relevant sources with
stable IDs, exact attribution, primary URL/DOI, access information and reading
status. Record archive search scope/date so the shortlist can be revisited.
Use **Queued**, **Reading**, **Reviewed**, **Trial planned**, **Trial complete**,
**Adopted**, **Deferred**, or **Rejected**. Abstract screening is not a completed
review, and a promising hypothesis is not a measured improvement.

For each completed review or experiment, record:

1. Source ID, review date and exact sections/pages or talk timestamps consulted.
2. The idea, assumptions, evidence, limitations and departures from Ludus's
   design; distinguish inspiration from copied or adapted code.
3. Affected module/milestone, a testable hypothesis and a bounded prototype.
4. Baseline revision/corpus, reproduction commands, toolchain/backend/data
   versions and the relevant tests, benchmark or PR. Generated results stay in
   ignored `out/`.
5. Before/after evidence appropriate to the change: linguistic/rendered
   correctness, translator round-trip/conflict behavior, native/wasm parity,
   failure/output preservation, CPU percentiles, allocations, package sizes,
   startup and old-plus-new/retained-generation memory.
6. An adopt/defer/reject decision with its reason and links to any updated
   architecture, profile, implementation and source acknowledgement.

No source in this queue has a completed adoption or improvement trial yet.
Preserve exact identities, explicit exception-free errors, snapshot lifetimes,
Unicode conformance and private backend boundaries in every trial. Use the
architecture's existing gates rather than treating a favorable microbenchmark
or an attractive conference abstract as proof of improvement.
