# Reflection and serialization: Game Development Gems review

The selected chapters were consulted while designing the architecture. The
local `references/game-dev-gems-toc.md` reading index selects candidates, rather
than proving their contents. Bibliographic details below use the chapters' printed
titles, authors, sections, and printed pages. The reference PDFs are the user's
local reading packet and are not redistributed in the repository. Every new
implementation here is independently written; no chapter source code is copied.

## Material contributions to this implementation

| Author and source | Adopted idea | Departure and concrete use |
| --- | --- | --- |
| Frederic My, **Using Custom RTTI Properties to Stream and Edit Objects**, *Game Programming Gems 4*, §1.12, pp.111–124 | Shared per-type descriptions, stream/edit/read-only policies, matching schema meaning, canonical units | Immutable explicit-ID tables and generated typed access replace raw offsets and name-based persistence. Validation remains in shipping. Historical defaults belong to explicit schema versions. `schema.hpp`, owner plans, and JSON use this model. |
| Charles Cafrelli, **A Property Class for Generic C++ Member Access**, *Game Programming Gems 2*, §1.7, pp.46–50 | Generic tools consume descriptions of native members; external and native names can differ | Source key/member/label are independent. The ABI adapter copies owned records; it retains no per-instance member address. |
| Allen Pouratian, **Platform-Independent, Function-Binding Code Generator**, *Game Programming Gems 3*, §1.4, pp.38–43 | Emit ordinary source and let the target compiler handle native types/ABI | Closed JSON input and C++ member-pointer/type checks replace source parsing and historical RPC mechanisms. No offset, pointer-derived ID, host layout, or native executable is used during generation. |
| Jason Beardsley, **Template-Based Object Serialization**, *Game Programming Gems 3*, §5.5, pp.534–545 | Explicit scalar representation and owner extension points | Exact integer JSON, finite floats, bounded parsing, copied candidates, and shipping validation. No recursive raw-pointer serialization, unbounded stream growth, or debug-only tags. |
| Matthew Campbell and Curtiss Murphy, **Exposing Actor Properties Using Nonintrusive Proxies**, *Game Programming Gems 6*, §4.5, pp.383–392 | A property projection can adapt existing/third-party native objects | The optional GameApi adapter keeps Editor types out of native records and returns statuses. The chapter's exception recommendation on p.387 is incompatible with Ludus. Heap-allocated virtual per-field proxies and immediate resource setters are unnecessary here. |

Reference comments near each materially affected implementation identify the
chapter and explain its departure, as required by AGENTS.md. This document
supplements those comments.

## Contributions reserved for later slices

| Author and source | Useful design constraint | What is deliberately deferred or rejected |
| --- | --- | --- |
| Scott Wakeling, **Dynamic Type Information**, *Game Programming Gems 2*, §1.6, pp.38–45 | Selective metadata can serve tools without reflecting everything | No compulsory reflective superclass, cast service, global factory, or virtual serializer. Add a registry only with a real dynamic consumer and module leases. |
| James Boer, **A Flexible Text Parsing System**, *Game Programming Gems 2*, §1.17, pp.112–117 | Readable authoring and compiled forms can be separate; diagnostics matter | Keep the current strict JSON parser. No case-folded property keys, preprocessor language, or token-stream format introduced without a resource consumer. |
| Lasse Staff Jensen, **A Generic Tweaker**, *Game Programming Gems 2*, §1.18, pp.118–126 | Opt-in controls, ranges, categories, and custom projections | Explicit IDs/statuses replace addresses as identities and debug-only checks. Categories and custom controls wait for the inspector to need them. |
| Martin Brownlow, **Save Me Now!**, *Game Programming Gems 3*, §1.7, pp.59–63 | Explicit save state, ID-based links, post-load reconstruction of derived state | Keep owner-specific checkpoints. Do not serialize raw native blocks or offsets; graph loading needs private candidates and two-pass resolution. |
| Joris Mans, **Serializing C++ Objects into a Database Using Introspection**, *Game Programming Gems 7*, §7.2, pp.517–534 | Identity maps, transaction boundaries, type-checked references, and embedded-object reference concerns | No database dependency or automatic persistence of native class names. Interior-pointer support and stale-object caches require a concrete owner model. |
| Martin Linklater, **Dataports**, *Game Programming Gems 7*, §7.3, pp.535–540 | Named dynamic bindings need type and lifetime discipline | Global writable dataports and function addresses as IDs are unsuitable. Cached bindings must eventually include module/object generations. |
| Nicolas Guillemot, **Static Reflection in C++ Using Tuples**, *Game Engine Gems 3*, ch.15, pp.207–217 | Static field traversal and external naming are separable from object behavior | Retain ordinary structs rather than convert native storage to tuples. Heavy traversal templates do not enter public SDK headers. Typed generated member access is the small C++23 implementation. |

## Modern primary-source cross-check

The following primary documents informed the architecture's evolution and scope
choices. They are design references, not dependencies or performance evidence.

- ISO C++ WG21, [P2996R13, Reflection for C++26](https://www9.open-std.org/JTC1/SC22/WG21/docs/papers/2025/p2996r13.html): compiler reflection is a future implementation mechanism. Ludus's pinned C++23/native and separate web toolchains cannot assume it; persistence IDs, versions, defaults, ownership, and migrations would still be necessary.
- Google, [Protocol Buffers proto3 guide](https://protobuf.dev/programming-guides/proto3/) and [field presence](https://protobuf.dev/programming-guides/field_presence/): stable field identities and reserved retired numbers are separate from names, and absence has semantic meaning. The v1 baseline freezes wire defaults instead of interpreting absent fields from today's native constructors.
- Google, [FlatBuffers schema evolution](https://flatbuffers.dev/evolution/) and [C++ support](https://flatbuffers.dev/languages/cpp/): compatibility and validation must be specified separately from fast native access. The first slice uses a bounded authored codec; a future cooked format needs verification and measured benefit.
- Michele Caini, [EnTT meta documentation](https://skypjack.github.io/entt/md_docs_2md_2meta.html): broad runtime meta facilities are useful for some consumers, but Ludus presently needs field descriptions and owner-controlled transactions. This review makes no throughput comparison with EnTT or other libraries.

The resulting implementation is small because it takes these constraints
seriously: normal C++ storage, readable generated code, explicit identity,
bounded staging, one shared scalar validator, and a copied projection into the
already existing Editor ABI. The larger registry and persistence services remain
separate, explicit future work.

## Follow-up conference and journal reading

Evaluate these resources after implementing and validating the
[initial architecture](reflection-serialization.md#next-production-slices).
This reading queue was selected from primary venue records, abstracts and
available source material. The potential applications below are questions for
that later technical review; implementation decisions and performance claims
require evidence from Ludus's own consumers.

Start with Kennedy, ESCHER and Walker, which address compatibility and authoring
directly. Then use PADS and the codec/parser papers to evaluate extensions and
optimizations. Preserve the existing Gems attribution when applying a new source.

| Priority and source | Question to investigate in Ludus |
| --- | --- |
| First: Stephen Kennedy, **Robustification Through Introspection and Analysis Tools (Avoiding Developer Taxes)**, GDC 2012, Havok. [Venue record](https://gdcvault.com/play/1016002/Robustification-Through-Introspection-and-Analysis). | Can comparisons of schema snapshots turn the baseline check into actionable compatibility reports and migration requirements? Retain explicit IDs and the closed manifest generator; evaluate the metadata-comparison idea independently of the talk's modified Clang frontend. |
| First: Marco Piccioni, Manuel Oriol and Bertrand Meyer, **Class Schema Evolution for Persistent Object-Oriented Software: Model, Empirical Study, and Automated Support**, *IEEE Transactions on Software Engineering* 39(2), February 2013. [DOI](https://doi.org/10.1109/TSE.2011.123), [author manuscript](https://arxiv.org/abs/1103.0711). | How should immutable version history, explicit transformation functions and post-conversion semantic invariants support our durable saves? Study the ESCHER model while adapting its exception-based failures to statuses and private candidates. Automatic structural changes cannot infer the owner's intended semantics. |
| First: Jeremy Walker, **Reflection for Tools Development**, GDC Canada 2010, Ubisoft Vancouver. [Venue record](https://gdcvault.com/play/1013168/Reflection-for-Tools). | How can shared descriptions support reusable, game-specific authoring workflows as strings, containers and custom projections reach the inspector? Evaluate those workflows through our copied GameApi records and owner-controlled transactions. |
| Next: Kathleen Fisher and Robert Gruber, **PADS: A Domain-Specific Language for Processing Ad Hoc Data**, PLDI 2005, pp.295–304. [DOI](https://doi.org/10.1145/1065010.1065046), [authors' project and paper links](https://kathleenfisher.org/research/pads/). | Which declarative description and generated diagnostic-metadata techniques help extend the closed schema manifest to bounded strings/containers and useful error paths? Any language extension needs a real consumer, explicit limits and review of its generated code. |
| Next: Daniel Lemire and Francisco Geiman Thiesen, **Reflection-based JSON in C++ at Gigabytes per Second**, CppCon 2025. [Session account](https://devblogs.microsoft.com/cppblog/cppcon2025-trip-report/), [C++26 Reflection for JSON Serialization — A Practical Journey, slides](https://simdjson.github.io/simdjson_talks/cppcon2025/cppcon_2025_slides.html). | Would specialized generated codecs reduce repeated traversal or intermediate scalar snapshots under the same validation and evolution semantics? Compare against our C++23 implementation; investigate standard reflection separately when both supported toolchains can use it. |
| Next: Geoff Langdale and Daniel Lemire, **Parsing Gigabytes of JSON per Second**, *The VLDB Journal* 28, 2019, pp.941–960. [DOI and publisher record](https://doi.org/10.1007/s00778-019-00578-5), [author manuscript](https://arxiv.org/abs/1902.08318). | Which parsing and SIMD techniques improve the actual bounded authored documents? Measure native and Wasm paths with complete validation, exact integer handling, workspace limits and allocation-failure behavior. |
| Focused follow-up: Daniel Lemire, **Number Parsing at a Gigabyte per Second**, *Software: Practice and Experience* 51(8), 2021, pp.1700–1727. [DOI](https://doi.org/10.1002/spe.2984), [author manuscript](https://arxiv.org/abs/2101.11408). | Can decimal-to-floating-point conversion improve while retaining the scalar codec's width, bounds, finite-value and fixture contracts? Review rounding correctness as well as throughput. |
| Conditional networking follow-up: Deepthi Raghavan et al., **Cornflakes: Zero-Copy Serialization for Microsecond-Scale Networking**, SOSP 2023. [DOI](https://doi.org/10.1145/3600006.3613137), [author paper](https://www.microsoft.com/en-us/research/wp-content/uploads/2023/09/cornflakes-sosp23.pdf). | If network serialization becomes a measured bottleneck, when do copies outperform scatter/gather bookkeeping? The paper's datacenter NIC workloads need separate evaluation before applying its techniques to game networking, durable saves or browser targets. |

Record accepted ideas, significant departures and rejected alternatives in this
review. Put concise attribution beside materially affected code and keep the
detailed contracts in the [architecture owner](reflection-serialization.md).
The later improvement phase uses the initial implementation as its comparison
baseline and retains its compatibility fixtures and failure tests.
