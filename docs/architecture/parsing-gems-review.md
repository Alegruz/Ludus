# Ludus Parsing Literature Review

**Status:** Source review informing the [parsing architecture](parsing.md).
Completed on 2026-10-04. It implements no parser and contains no Ludus
performance measurements.

The initial architecture was drafted from Ludus's current codecs, ownership
rules, and current primary documentation. Then
[game-dev-gems-toc.md](../../references/game-dev-gems-toc.md) was searched for
parsing, lexing, tokenization, schemas, serialization, and fast data loading.
Seven relevant chapters were read and the architecture was revised. The table
of contents located sources; chapter contents support the findings below.

## Evidence and selection

Local PDF links use one-based physical PDF pages. Printed page numbers are
listed separately because scanned copies have missing pages and variable
offsets. GPG1, GPG3, GPG4, and GPG6 required rendered-page OCR; chapter/author
identity was checked visually on rendered chapter openings for GPG1/GPG3/GPG4.
GPG2 and GEG2/GEG3 have extractable text. The selected page ranges below cover
the complete chapters, including examples and conclusions. No source code or
long copyrighted passages are copied into the proposed design.

| Article | Author | Local chapter pages | Role |
| --- | --- | --- | --- |
| §1.17, A Flexible Text Parsing System | James Boer | [GPG2 PDF 109–114](../../references/Game%20Programming%20Gems%202.pdf#page=109); printed 112–117 | Direct parsing and preprocessing/cooking design |
| §1.11, Using Lex and Yacc To Parse Custom Data Files | Paul Kelly | [GPG3 PDF 85–93](../../references/Game%20Programming%20Gems%203.pdf#page=85); printed 83–91 | Direct grammar-generation and tooling design |
| §1.13, Using XML Without Sacrificing Speed | Mark T. Price | [GPG4 PDF 136–146](../../references/Game%20Programming%20Gems%204.pdf#page=136); printed 125–135 | Direct text/binary schema and production workflow |
| §1.8, Fast Data Load Trick | John Olsen | [GPG1 PDF 86–89](../../references/Game%20Programming%20Gems%201.pdf#page=86); printed 88–91 | Supporting offline preparation and buffer reuse |
| Chapter 20, Pointer Patching Assets | Jason Hughes | [GEG2 PDF 361–373](../../references/Game%20Engine%20Gems%202.pdf#page=361); printed 345–357 | Supporting packed representation and inspection |
| Chapter 14, Compile-Time String Hashing in C++ | Stefan Reinalter | [GEG3 PDF 184–192](../../references/Game%20Engine%20Gems%203.pdf#page=184); printed 197–205 | Supporting finite vocabulary dispatch |
| §1.6, Closest-String Matching Algorithm | James Boer | [GPG6 PDF 74–82](../../references/Game%20Programming%20Gems%206.pdf#page=74); printed 69–77 | Supporting authoring diagnostics |

The GPG1 TOC catalog explicitly notes that its original contents pages are
absent. Its catalog entry is not evidence of the chapter's section number;
§1.8 and its author were established from the actual opening page.

The TOC also lists a command-line tokenizer, object introspection/serialization,
and scripting surveys. They do not establish a better grammar or storage
strategy for current Ludus JSON and were not treated as technical evidence.
CLI argument conventions, reflection, and scripting-runtime selection remain
separate tasks. Graphics/spatial parsing and GPU articles address other
workloads. This is a targeted review of relevant chapters, not a claim that
every indexed book or historical implementation was evaluated.

## What changed

| Question | Initial design | Revision from the reading |
| --- | --- | --- |
| Sharing names between code and authored data | Explicit per-domain decoders | One domain-owned vocabulary can feed runtime dispatch and authoring/cooker metadata; Boer and Price |
| Parser generator adoption | Handwritten small grammars; reconsider large/ambiguous ones | Add reproducibility, conflict, reentrancy, stack/OOM, action cleanup, and SDK consumer gates; Kelly |
| Debugging after decoding/cooking | Syntax offsets and semantic paths | Carry source digest/path/optional spans into typed origins and artifact-bound debug metadata; Boer and Hughes |
| Shipping binary representation | Optional per-domain cooker and checked reader | Resolve typed records and exact string dictionaries offline; generic token replay remains a tool cache; Boer, Olsen, and Price |
| Testing the shipped representation | Generic binary round-trip/corruption coverage | Exercise the exact cooked path in integration/acceptance before release and allow authoring-code exclusion from opted-in runtime targets; Price |
| Binary maintainability | Portable checked offsets | Add an offline inspector and explicit dependency/dictionary/provenance sections; Hughes |
| Field dispatch | Exact comparisons for small objects | Optional precomputed discriminator for measured larger vocabularies, with exact-byte confirmation; Reinalter |
| Misspelled names | Unknown-field/reference errors | Optional bounded suggestions on the failure path without autocorrection; Boer, GPG6 |

The core strategy survived the review: a small shared foundation, existing
yyjson backend, independent grammars, owned typed candidates, explicit limits,
and optional editor/cooker representations. The chapters improved the handoff
and authoring workflow; they did not establish a need for a universal parser,
mandatory XML, a global symbol service, or a new scripting language.

## Boer: flexible text parsing

The chapter describes distinct token meanings, configurable keywords/operators,
separate source/header/macro passes, file/line debugging, and a TokenFile
representation. Processed textual tokens are compiled into a lookup table and
binary numeric values. Shared headers/constants are used to keep authored
definitions and code consistent. Optional phases let simpler consumers skip
header and macro processing.

**Adopt:** Separate phases, explicit source provenance, compile repeated work
offline, and share a finite domain vocabulary between code and tools. Carry
raw spelling as a source range and decoded string bytes under a real owner.
Retain file/path/digest identity after parser scratch is released. Add exact
compiled dictionaries with artifact-local indices.

**Adapt:** Produce validated typed domain records when shipping, rather than
replaying generic tokens to interpret gameplay data at load time. Generated
field/constant metadata solves today's code/data drift without making JSON a
C++ preprocessor input. A future language import/constant feature needs bounded
expansion, explicit resolution and dependencies, cycles, and origin chains.

**Reject:** Public inheritance from an STL linked token list, per-token mutable
C strings, unchecked integer/real conversion, unsupported exponent notation,
default case-insensitive token-file merging, and broad macro substitution.
The chapter's load-time multiplier is historical context, not a Ludus estimate.
Its assertions for token access do not replace checked access to external data.

**Result:** [Shared schema vocabulary](parsing.md#shared-schema-vocabulary),
[provenance](parsing.md#provenance-and-useful-diagnostics), and
[cooked data](parsing.md#cooked-data-and-checked-binary-reading) now have concrete
contracts beyond the initial pipeline diagram.

## Kelly: Lex/Yacc for custom data files

The chapter separates lexer token recognition from grammar productions and
semantic actions. It shows a weapon/ammunition authoring format, text-export
packing/unpacking, and the build flow from grammar sources to generated code
to a tools executable. It explicitly discusses the maintenance benefits and
code-size/execution tradeoffs of generated code. Its samples communicate
through globals and write domain tables during grammar actions.

**Adopt:** Keep a grammar specification and representative examples reviewable.
Generated lexers/parsers are legitimate when they make a growing grammar easier
to maintain. Preserve inspectable textual exports and a tool that decodes binary
records for investigation. Keep lexical recognition separate from domain
validation and publication.

**Adapt:** A modern generated backend needs explicit per-job context,
reentrancy, reproducible generation, conflict review, bounded stack growth,
fallible memory, and cleanup on error/recovery/cancellation. Semantic actions
build a private candidate; they do not mutate live gameplay tables. A generated
C interface may fit Ludus, but its exact output must pass the exception and
allocator audit. The current [Bison manual](https://www.gnu.org/software/bison/manual/bison.html)
confirms reentrant-parser and stack-limit mechanisms; the Gem alone does not
prove that any current generator configuration meets Ludus's rules.

**Retain/defer:** Handwritten recursive descent and Pratt expressions remain
the small-language default. Reconsider a generator for grammar conflicts,
recovery complexity, or duplicated grammar implementations. Do not convert
existing JSON schemas into DSLs merely to use a generator. The chapter's
general speed/code-size discussion is not a benchmark of current generators.

**Reject:** Global `yyin`/semantic state, unchecked text copying, `atoi`-style
conversion, direct diagnostic printing, and uncommitted partially built data.

**Result:** [Custom-language selection](parsing.md#small-custom-languages) now
includes generator acceptance and build-host/SDK boundaries.

## Price: XML and typed binary data

The chapter introduces XDS, a typed tagged binary format, and a toolkit that
derives definitions/schema metadata from C/C++ declarations, converts XML to
binary, and offers a common loading library. Its production guidance uses
editable text early, mixed representations while tuning, then the binary path
for integration/acceptance and release. It also describes removing text/write
support from a shipping reader to reduce its footprint.

**Adopt:** Keep a human-editable authoring representation, typed runtime
representation, and common domain meaning. One vocabulary can serve schema
metadata and decoder dispatch. Once a domain ships cooked content, test that
exact representation before release and make runtime parser/writer inclusion
an explicit dependency decision.

**Adapt:** Ludus currently uses strict JSON; there is no demonstrated reason
to replace it with XML or XDS. Start with a private readable field table and
explicit decoder. Introduce small deterministic metadata generation only when
multiple consumers need it. Keep stable schema field IDs explicit and disk
encoding separate from compiler ABI. A source edit cannot implicitly rewrite
an unsupported schema version.

**Reject:** Automatic capture of arbitrary global C++ types/variables,
ABI-shaped payloads, untyped ownership callbacks, and changing network types
without an explicit compatibility contract. A binary meta-format alone does
not solve versioning, validation, memory lifetime, or protocol compatibility.

**Result:** The architecture adds shared vocabulary, shipping-path acceptance,
and optional authoring-code removal. It keeps the existing JSON and caller-owned
candidate publication contracts.

## Olsen: preprocess and reuse buffers

The chapter emphasizes moving preparation before loading, contiguous data,
indices/handles where pointers are unsuitable, and reusing an appropriately
sized read buffer. Its example directly saves and reloads native object bytes;
it also warns about pointers/vtables and historical hardware read granularity.

**Adopt:** Resolve expensive work offline when it materially helps, keep
coherent records together, and reuse bounded scratch across sequential loads.
I/O granularity and writable padding are properties of an acquisition/backend
contract, not permission for the parser to overrun a counted byte view.

**Reject:** `fwrite(this, sizeof(...))`-style object dumps, assumptions about
padding/endianness, direct reads into live objects, unchecked short reads,
and historical raw allocation/deallocation examples. C++ object lifetime and
cross-platform layout need explicit portable encoding. Index-based references
still need bounds and ownership validation.

**Result:** The existing checked binary design is retained and strengthened:
packing is an optimization of typed records, not native object reconstruction.
No portable zero-copy claim follows from this chapter.

## Hughes: packed assets and inspection

The chapter moves resource preparation and dependency work to tools, groups
coherent data into blocks, and stores relationships as relative offsets. It
discusses alignment, byte swapping, named bindings, retained metadata, and
offline introspection. Its example patches offsets into pointers after loading;
the text explicitly identifies limitations of the illustrative writer.

**Adopt:** Contiguous typed payloads, an explicit section/dictionary directory,
offline dependency closure, and an inspector that exposes versions, records,
dependencies, footprint, and source origins. Retained debug metadata makes
compiled content understandable without shipping the original text everywhere.

**Adapt:** Keep offsets encoded and resolve them through checked blob-owned
views. Native and browser readers use the same fixed-width representation.
Reference resolution and old/new revision retirement belong to Content;
Parsing only produces/reads validated records. Include metadata in peak memory
accounting even when it is optional.

**Reject:** Patching read-only blobs, pointer-to-integer casts, serialized
vtables, native C++ layouts, assumed pointer widths, or a general relocation
framework for every parser. Asset compression and aligned mapped-record
formats need separate measurements and contracts.

**Result:** [Cooked data](parsing.md#cooked-data-and-checked-binary-reading) now
specifies dictionary/dependency/provenance sections and offline inspection.

## Reinalter: compile-time vocabulary discriminators

The chapter precomputes hashes for literal names, discusses overload/array
decay pitfalls, and explicitly treats collision detection as a production
requirement. Its examples illustrate repeated lookup work rather than grammar
recognition or schema validation.

**Adopt conditionally:** If a larger fixed vocabulary warrants measured faster
dispatch, precompute literal discriminators once and retain exact spellings.
Use the proposed Strings literal-descriptor contract rather than inventing a
second parser hashing service. Test adversarial collisions and runtime/literal
parity; exact byte equality remains authoritative.

**Retain/reject:** Small schemas use clear exact comparisons. Do not require
hashing for every token, use unchecked hashes as public/persistent field IDs,
or copy the historical recursive template mechanism. Compile-time work and
generated tables remain part of the build-time budget.

**Result:** Vocabulary dispatch has an explicit optimization path and invariant;
the baseline simple dispatch does not become more complex by default.

## Boer: useful spelling suggestions

The chapter proposes approximate matching after exact string-ID lookup fails.
It favors small code and minimal temporary allocation for error reporting,
and explicitly does not present its heuristic as an optimal general algorithm.
Its examples suggest a likely intended name while leaving exact lookup intact.

**Adopt:** Bounded editor/cooker suggestions for unknown schema fields and
unresolved resource names, drawn from an appropriate frozen domain vocabulary.
Retain exact failure and source origin; cap candidates, name length, scratch,
and total work. Deterministic ties make diagnostics stable in tests and CI.

**Adapt/reject:** Choose a measured scorer for Ludus's identifier grammars.
Do not automatically correct names, make fuzzy matching establish identity,
run similarity during successful gameplay lookup, or adopt the sample's
floating thresholds and bytewise case behavior as a Unicode policy.

**Result:** [Diagnostics](parsing.md#provenance-and-useful-diagnostics) add a
small optional usability feature while keeping core parsing deterministic.

## Current primary-source cross-check

These sources were consulted on 2026-10-04 to separate historical ideas from
current capabilities. They inform proposed choices; they do not establish
measured Ludus gains:

- [RFC 8259](https://www.rfc-editor.org/rfc/rfc8259): JSON syntax, string
  comparison/encoding, number interoperability, and parser limits. Ludus's
  duplicate rejection and domain numeric policy are explicit stricter rules.
- [yyjson API](https://ibireme.github.io/yyjson/doc/doxygen/html/api.html):
  configurable allocators, pool-backed documents, and parsing options. For
  actual compatibility use the bundled [0.10.0 header](../../third_party/yyjson/yyjson.h)
  and [hash-verified build](../../third_party/yyjson/CMakeLists.txt); upstream
  latest documentation does not override the repository pin.
- [simdjson On-Demand](https://simdjson.org/api/4.6.4/md_doc_2basics.html):
  lazy validation, traversal/lifetime rules, padding, and explicit errors.
  Equivalent full validation must be part of any performance comparison.
- [Tree-sitter](https://tree-sitter.github.io/tree-sitter/) and its
  [error/missing-node documentation](https://tree-sitter.github.io/tree-sitter/using-parsers/queries/1-syntax.html):
  incremental concrete syntax and recovery are editing capabilities, not
  proof of valid runtime content or bounded allocation.
- [re2c manual](https://re2c.org/manual/manual_c.html): generated lexers have
  explicit input bounds/padding strategies that require a verified adapter.
- [fast_float](https://github.com/fastfloat/fast_float): a private conversion
  candidate with explicit errors and documented allocation/exception behavior.
  Grammar, target range/underflow policy, pinning, and ADR compliance still apply.
- [Bison manual](https://www.gnu.org/software/bison/manual/bison.html): pure
  per-parser state and stack bounds exist; actual generated skeletons require
  an exception/allocation/cleanup audit.

No new dependency, policy exception, cooked format, or parser-generator
installation follows automatically from the literature review. Implementation
begins with the measured compatibility baseline and existing yyjson consumer.
