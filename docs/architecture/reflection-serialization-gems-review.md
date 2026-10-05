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
