# Ludus Strings Literature Review

**Status:** Source review informing the proposed [strings architecture](strings.md).
Architecture decisions are proposals; the review contains no Ludus performance
measurements and does not implement any string API.

The baseline architecture separated views, unique mutable owners, immutable
shared owners, and table-index identity. After establishing that design, the
review used [game-dev-gems-toc.md](../../references/game-dev-gems-toc.md) to
locate relevant chapters, read their available local contents, and revise the
architecture. The resulting design adds literal descriptors, compiled
dictionaries, offset-based packed storage, source metadata, and optional spelling
suggestions for diagnostics.
It retains exact equality and explicit lifetimes even where older examples
choose lighter checks.

## Evidence and scope

The repository contains the named PDFs under the ignored `references/` tree.
Links below identify the local copies; PDF page numbers are one-based physical
pages in those copies, not assumed to match the printed page numbers. The
review used text extraction and checked rendered pages for the compile-time
hashing chapter's identity and the parser chapter's TokenFile discussion.
The closest-string source was scanned and was read through OCR, with its chapter
and author also checked against the rendered contents page.
Bookmarks and table-of-contents titles located chapters; their technical claims
below come from the chapter text. No copyrighted source code is copied into
the proposed engine.

The review selected chapters directly relevant to string ownership, symbol
lookup, token dictionaries, and packed content. Spatial hashing, Zobrist game
state hashing, texture compression, and rendering articles address different
problems and supply no evidence for general string equality or ownership.
Memory-management articles already reviewed in the
[memory design](memory-management.md#4-literature-synthesis) remain the
allocation context; they do not justify adding a second string-specific heap.

## Changes resulting from the review

| Design question | Baseline | Revised architecture and source |
| --- | --- | --- |
| Literal names | Cache an interned ID after explicit setup | Add counted `consteval` descriptors and collision-checked closed schemas; Reinalter |
| Repeated content text | Scoped runtime intern table | Add cooker-generated deterministic dictionaries and typed indices; Boer |
| Dictionary storage | Stable runtime byte pages | Add packed offset/length storage owned by one content snapshot; Hughes |
| Source diagnostics | Resolve exact spelling through its owner | Retain source metadata and offer optional bounded spelling suggestions in tools; Boer and Hughes |
| Large dictionary misses | Exact table lookup | Benchmark an optional immutable Bloom prefilter; Fischer |
| Identifier collisions | Full bytes establish identity | Keep that rule in Release, even when examples use hashes as IDs; Reinalter and Gregory |
| Sharing and mutation | Immutable shared block and explicit edit copy | Retain the separation; no reviewed Gem establishes a better universal COW default |

The adopted details appear in [literal descriptors](strings.md#literal-descriptors),
[cooked dictionaries](strings.md#cooked-dictionaries-and-persistence), and
[debugging](strings.md#debugging-and-observation). These are Ludus adaptations,
not claims that the chapters specify the entire modern architecture.

## Compile time string hashing

**Source:** Stefan Reinalter, Chapter 14, “Compile-Time String Hashing in C++,”
[Game Engine Gems 3](../../references/Game%20Engine%20Gems%203.pdf#page=184).
Read PDF pages 184–192, printed pages 197–205.

The chapter's motivating examples hash asset or bone names repeatedly when
calling lookup functions. It shows compile-time FNV-1a-32 calculation with
`constexpr`, discusses array-to-pointer overload selection, and includes an
auxiliary hash wrapper whose constructor can check a string database for
collisions. Its implementation notes make collision detection a production
concern rather than something the hash width alone solves.

**Adopt:** Precompute finite literal metadata, retain the counted spelling, and
bind literal names once during explicit subsystem initialization. Generate
closed symbol schemas when the pipeline can validate their complete key set.
Make collision checking part of the cooker/registry contract, not a debug-only
assertion. This adds a concrete literal-binding contract to the baseline design.

**Adapt:** Use C++23 `consteval` and a simple loop, with unsigned-byte conversion
and runtime/compile-time parity tests. A 64-bit `SymbolFingerprint64` is a
candidate discriminator, while the runtime table may use a different lookup
hash. The descriptor does not pretend to be a runtime `NameId`: an index still
requires a specific table. Do not infer static pointer lifetime merely from
successful compile-time evaluation.

**Do not adopt:** The old recursive-template implementation, a universal FNV-32
lookup policy, implicit conversion from arbitrary runtime text to an identifier,
or unchecked hash-only equality for dynamic names. Those choices would obscure
allocation/failure, weaken collision behavior, or add unnecessary template work
on Ludus's current toolchain.

## Text parsing and compiled dictionaries

**Source:** James Boer, §1.17, “A Flexible Text Parsing System,”
[Game Programming Gems 2](../../references/Game%20Programming%20Gems%202.pdf#page=109).
Read PDF pages 109–114, printed pages 112–117.

The chapter separates token semantics, preserves source file/line diagnostics,
and describes TokenFile conversion from processed text tokens to binary form.
Character-based token values enter a lookup table while numerical values have
binary representation. The chapter also allows case-insensitive compilation
that merges different case spellings.

**Adopt:** Process human-readable authoring data in tools, emit a deduplicated
dictionary and typed binary records, and carry source spans in optional debug
metadata. A parser/editor holds one source owner and represents temporary tokens
with counted views or checked byte ranges. Escaped/decoded text needs separate
owned bytes; a decoded token is not necessarily a slice of the source file.
Dictionary indices belong to the compiled content snapshot rather than a
global immortal name service.

**Adapt:** Case sensitivity remains an explicit schema choice. Ludus's default
is exact counted-byte equality; neither generic string storage nor cooking
silently merges `Foo` and `foo`. Validate numeric ranges and all string/offset
bounds when the implementing parser or content reader is added. The strings
task does not replace existing JSON/content readers with this older parser.

**Do not adopt:** A linked STL token list, mutable raw C-string ownership,
unbounded preprocessing, or the chapter's suggested load-time multiplier as a
Ludus estimate. Its performance discussion is contextual; this design requires
new representative cooker/reader measurements.

## Packed assets and relative offsets

**Source:** Jason Hughes, Chapter 20, “Pointer Patching Assets,”
[Game Engine Gems 2](../../references/Game%20Engine%20Gems%202.pdf#page=361).
Read PDF pages 361–373, printed pages 345–357.

The chapter moves costly resource preparation into tools and packs coherent
data into contiguous blocks. Its writer tracks relationships and stores relative
offsets, its runtime patches pointer locations, and its discussion includes
alignment, byte swapping, metadata retention, symbolic binding, and offline
inspection. The sample explicitly has limitations including 32-bit pointers and
incomplete per-structure alignment support.

**Adopt:** Contiguous cooked string bytes, a compact directory, one owner per
snapshot, and offline inspection of dictionary footprint. This extends runtime
page storage with a concrete cooked representation, reduces per-string owner
overhead, and makes load-time work predictable.

**Adapt:** For Ludus strings, keep offsets as offsets. Resolve a checked index
to a view by combining the blob base with validated offset and length. Fixed-width
encoded fields work across native and browser data models. Parse those fields
without alignment assumptions. Make hot reload publish a new immutable blob
owner and retire old readers through the content subsystem's lifetime protocol.

**Do not adopt:** Raw dumping of native C++ object layouts, integer casts of
pointers, vtable patching, or patching a read-only mapped dictionary in place.
The portable offset/length format achieves the useful packing idea without
making the string subsystem a general pointer-fixup engine.

## Bloom filters for expensive misses

**Source:** Mark Fischer, §1.20, “Using Bloom Filters to Improve Computational
Performance,”
[Game Programming Gems 2](../../references/Game%20Programming%20Gems%202.pdf#page=130).
Read PDF pages 130–137, printed pages 133–140.

The chapter describes compact approximate membership, false-positive outcomes,
and the need to perform the authoritative test after a positive filter result.
Its conclusion recommends introducing the filter during optimization after
functionality is complete. Its historical implementation suggests an MD5-based
key and discusses additive filters.

**Adopt conditionally:** A Bloom prefilter is a candidate for a very large
immutable dictionary whose misses cause costly backing lookup. Build it from the
exact published dictionary and keep both in the same snapshot. A negative result
can skip lookup only when the filter fully covers that dictionary; a positive
result never establishes string identity.

**Defer:** The core intern table starts with exact lookup and no Bloom filter.
Measure miss ratio, dictionary size, additional hash work, filter build time,
memory, and end-to-end latency before adding one. Do not introduce the chapter's
MD5 choice, its numerical examples as engine budgets, or a concurrently mutated
filter without a new correctness/publication contract. This review supplies a
revisit criterion rather than another default structure to maintain.

## Closest string diagnostics

**Source:** James Boer, §1.6, “Closest-String Matching Algorithm,”
[Game Programming Gems 6](../../references/Game%20Programming%20Gems%206.pdf#page=74).
The scanned chapter begins at PDF page 74, printed page 69. Reviewed motivation,
requirements, matching rules, and usage are at PDF pages 74–76 and 81–82.

The chapter addresses human mistakes in string IDs and proposes showing a
likely spelling when exact lookup fails. Its requirements favor a small
similarity function and little temporary allocation for many small searches,
rather than assuming a large search corpus. The discussion includes sensitivity
to case mismatches.

Its discussion places the work on the error path and does not claim an optimal
general search algorithm. Ludus adopts that diagnostic scope rather than the
chapter's heuristic score, STL map interface, or floating-point thresholds.

**Adopt:** An optional editor/cooker diagnostic can inspect a frozen symbol
dictionary and propose a few candidates. This improves investigation of missing
resource or schema names while preserving the exact lookup result. Use bounded
work and caller-owned scratch; enumeration must retain a valid table snapshot.

**Do not adopt as identity:** Similarity does not define equality, trigger automatic
correction, or belong on normal gameplay lookups. The reviewed motivation does
not establish a universally best modern ranking algorithm, a Unicode policy,
or a Ludus performance result. Benchmark scoring separately for actual bounded
symbol grammars before selecting an implementation. The core string/table API
does not depend on fuzzy search.

## Supporting engine architecture reference

**Source:** Jason Gregory, §6.4, “Strings,”
[Game Engine Architecture 3rd Edition](../../references/Game%20Engine%20Architecture%203rd%20Edition.pdf#page=475).
Relevant reviewed portions cover string-class costs, unique identifiers,
interning, and localization in PDF pages 475–488, printed pages 456–469.
This is a supporting book section listed in the index, not a Gems article.

The section discusses string-owner cost models, caching interned names,
compile-time IDs, human-readable name resolution, UTF-8 use, and localized
string databases. It also describes hash-only ID practices and keeping spelling
in development-only memory in some engines.

**Retain:** Cache names once, make ownership/copy costs explicit, and keep
localized identity separate from resolved presentation text. Current primary
Unreal documentation independently supports distinguishing names, mutable
strings, and localized text.

**Refine:** Ludus uses a table-qualified entry index for dynamic identity and
retains exact bytes for collision comparison in every build flavor. A larger
hash does not prove absence of collisions. Debug-only spelling removal is
eligible only for a separately defined closed schema, not for a runtime table
that admits new input. User-facing localization will require plural/select
rules and locale-aware formatting above Strings, not an assumption that owning
UTF-8 bytes completes localization.

## Current primary sources

The proposal checked current authoritative sources on 2026-10-04. Their claims
support individual techniques; selecting those techniques for Ludus remains
an engineering judgment. Public library branches and Unicode report versions
must be pinned when implemented, rather than silently following latest URLs.

| Source | Specific use in this design |
| --- | --- |
| [Epic string handling](https://dev.epicgames.com/documentation/en-us/unreal-engine/string-handling-in-unreal-engine) | Separate identifiers, editable bytes, and localized text |
| [Qt implicit sharing](https://doc.qt.io/qt-6/implicit-sharing.html) | Understand shared blocks and mutation detachment; choose explicit immutable sharing for Ludus |
| [xxHash specification](https://github.com/Cyan4973/xxHash/blob/dev/doc/xxhash_spec.md) | Evaluate length-specific fast lookup/fingerprint algorithms behind a private boundary |
| [SipHash paper](https://eprint.iacr.org/2012/351) | Keyed short-input lookup for externally controlled keys |
| [FNV specification](https://datatracker.ietf.org/doc/html/rfc9923) | Define the small literal fingerprint algorithm and its exact constants |
| [Abseil Swiss tables](https://abseil.io/about/design/swisstables) | Candidate filtering before full equality; benchmark as a later lookup upgrade |
| [Unicode normalization](https://unicode.org/reports/tr15/) and [segmentation](https://unicode.org/reports/tr29/) | Keep normalization, byte equality, and user-visible boundaries distinct |
| [simdutf API](https://github.com/simdutf/simdutf#api) | Evaluate caller-allocated validation/transcoding for large imports |
| [Abseil Cord](https://github.com/abseil/abseil-cpp/blob/master/absl/strings/cord.h) | Keep chunked text structures optional for consumers that benefit from them |
| [C++ declaration rules](https://eel.is/c++draft/dcl.constexpr) | Distinguish immediate literal evaluation from potentially runtime constexpr calls |

## Conference and journal research backlog

Recorded 2026-10-06. Finish and validate the
[initial strings architecture](strings.md#research-after-the-initial-architecture)
first, record its revision and representative workload baseline, then use these
resources to investigate improvements. This queue does not add an implementation
dependency or replace the current ownership, failure, identity, or persistence
contracts.

The recommendations were screened from primary publication metadata, abstracts,
and conference session descriptions. Full papers/talks remain **Queued**;
this section makes no claim that they have been fully reviewed, their techniques
adopted, or their reported results reproduced in Ludus. The questions below are
our proposed experiments, not conclusions from the existing Gems review.

### Venues and topics

| Venue | Priority after baseline | Topic and Ludus relevance |
| --- | --- | --- |
| Information Systems and the International Symposium on Experimental Algorithms (SEA) | First | Static string dictionaries, front coding, tries, and measured storage/lookup tradeoffs; inform cooked dictionary alternatives |
| CppCon | First | C++ string representation, terminators, allocation behavior, and cache-conscious hash-table design; inform owner-layout and intern-table trials |
| Software: Practice and Experience and the International Symposium on String Processing and Information Retrieval (SPIRE) | First for text boundaries | UTF-8 validation and UTF-8/UTF-16 conversion; inform scalar/SIMD comparisons on short names and large imports |
| VLDB, Proceedings of the VLDB Endowment (PVLDB), and The VLDB Journal | After an uncompressed dictionary baseline | String compression, random access, footprint, and decoding cost; evaluate compression for large cooked dictionaries |
| SIGIR, the Workshop on Algorithm Engineering and Experiments (ALENEX), and ACM Journal of Experimental Algorithmics | Selective | Minimal perfect hashing, compact indexes, and reproducible algorithm comparisons; evaluate lookup for fixed symbol sets |

These are archive-search priorities, not an upcoming conference schedule.
Venue names identify where to search; the attributed primary sources below
provide concrete entry points. Search wider topic shortlists after the first
readings when a measured Ludus workload exposes a relevant problem.

### Initial reading queue

Begin with SR-01 through SR-03 after the baseline is validated; prioritize the
remaining readings according to measured dictionary and text-boundary costs.

| ID | Source and attribution | Full-review status | Ludus question and possible trial after review |
| --- | --- | --- | --- |
| SR-01 | Miguel A. Martínez-Prieto, Nieves R. Brisaboa, Rodrigo Cánovas, Francisco Claude, and Gonzalo Navarro, **Practical compressed string dictionaries**, *Information Systems* 56, pp. 73–108, 2016. [Publisher](https://www.sciencedirect.com/science/article/pii/S0306437915001672); [DOI](https://doi.org/10.1016/j.is.2015.08.008). Extends work presented at SEA 2011 | Queued; metadata/abstract screened | Which representation fits our exact lookup and index-to-spelling operations? Compare the initial offset/length blob against selected front-coded or trie alternatives on real symbol/path dictionaries; measure lookup, extraction, build time, and total footprint |
| SR-02 | Nicholas Ormrod, **The strange details of std::string at Facebook**, CppCon 2016, Facebook. [Official recording](https://www.youtube.com/watch?v=kPR8h4-qZdk) | Queued; session description screened | Which object-size, inline-capacity, terminator, and growth tradeoffs matter for Ludus? Compare the proposed 32/40/48-byte owner candidates on observed length/mutation distributions while preserving the move-only and fallible-copy contracts |
| SR-03 | Matt Kulukundis, **Designing a Fast, Efficient, Cache-friendly Hash Table, Step by Step**, CppCon 2017, Google. [Official recording](https://www.youtube.com/watch?v=ncHmEUmJZf4) | Queued; session description screened | Does control-byte/group probing improve our intern tables? Compare it with current linear probing across hits/misses, load factors, common prefixes, cold caches, growth peaks, and forced collisions; retain full-byte equality |
| SR-04 | John Keiser and Daniel Lemire, **Validating UTF-8 In Less Than One Instruction Per Byte**, *Software: Practice and Experience* 51, issue 5, 2021. [Author publication page and paper](https://lemire.me/en/publication/arxiv2010.03090/); [DOI](https://doi.org/10.1002/spe.2920) | Queued; metadata/abstract screened | When does SIMD beat the scalar validator on our inputs? Compare short engine names and large imports, retaining invalid-byte diagnostics and testing truncated/page-boundary tails |
| SR-05 | Daniel Lemire and Wojciech Muła, **Transcoding Billions of Unicode Characters per Second with SIMD Instructions**, *Software: Practice and Experience* 52, issue 2, 2022. [Author publication page and paper](https://lemire.me/en/publication/arxiv210910433/) | Queued; metadata/abstract screened | Which conversion path fits native text boundaries? Compare validation plus UTF-8/UTF-16 conversion, checked destination sizing, malformed-input behavior, caller-owned buffers, and dispatch overhead |
| SR-06 | Peter Boncz, Thomas Neumann, and Viktor Leis, **FSST: Fast Random Access String Compression**, *Proceedings of the VLDB Endowment* 13, issue 12, pp. 2649–2661, VLDB 2020. [Paper](https://vldb.org/pvldb/vol13/p2649-boncz.pdf); [DOI](https://doi.org/10.14778/3407790.3407851) | Queued; metadata/abstract screened | Can individual-string compression save enough cooked memory to justify decoding? Compare against the uncompressed deduplicated blob, including dictionary metadata, caller scratch, selective reads, and retained/peak memory. Decoded text needs an explicit owner and cannot inherit zero-copy borrowed-view guarantees |
| SR-07 | Giulio Ermanno Pibiri and Roberto Trani, **PTHash: Revisiting FCH Minimal Perfect Hashing**, SIGIR 2021. [Author paper](https://arxiv.org/abs/2104.10402); [DOI](https://doi.org/10.1145/3404835.3462849) | Queued; metadata/abstract screened | Is a generated index useful for a closed symbol set? Compare build time/peak memory, index size, and exact lookup against the frozen-table baseline. Validate membership by spelling, preserve deterministic persisted record indices through explicit remapping, and measure unknown-key queries |

### Review and trial record

For each full review, record the source/version, sections or talk timestamps
actually consulted, relevant assumptions, adopted/adapted/rejected ideas, and
an experiment tied to a concrete consumer. Preserve source attribution beside
any resulting implementation and link back here for the detailed review.

Use **Queued**, **Reading**, **Reviewed**, **Trial planned**, **Trial complete**,
**Adopted**, **Deferred**, or **Rejected** as explicit statuses. A screened
abstract is not a full review, and a paper's speedup is not a Ludus result.
Keep full-review findings here; keep API/system contracts in the
[architecture](strings.md) and public declarations.

Trial one change at a time against the recorded baseline using the
[measurement requirements](strings.md#measurements-that-choose-defaults).
Report correctness and failure-path results alongside latency distributions,
allocations, retained/peak bytes, and binary/header cost on supported profiles.
Record the decision even when a promising technique is deferred or rejected.

## Evidence limits

No local performance experiments compare the proposed owners, hash algorithms,
or table designs. Review findings are requirements and hypotheses to validate
through the [implementation gates](strings.md#validation-and-performance-gates).
The proposed Memory system and several string consumers remain future work;
the design does not claim they already exist. Unrelated uncommitted engine and
platform changes were inspected only where necessary to establish the current
boundary and were not modified.
