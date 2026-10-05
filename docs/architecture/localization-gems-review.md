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
