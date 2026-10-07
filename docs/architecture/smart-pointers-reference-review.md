# Smart pointer design and reference review

Reviewed on 2026-10-06 against repository HEAD `1e6bd1a`.
The [final architecture](smart-pointers.md) owns the implementation contract.

## Design before the Gems chapter review

The initial design selected these rules before reading the discovered chapters:

1. Repair the existing `UniquePtr<T, D>` rather than create another unique owner.
   Preserve old deleter state for destination destruction; transfer pointer and
   deleter together in moves and swaps. Validate the actual deleter.
2. Keep unique ownership in FoundationBase and domain-aware factories in
   FoundationMemory. Default deletion accepts matching ordinary `new`, not domain
   storage or native API objects with another destruction protocol.
3. Use references and nullable raw pointers for scoped borrows; use existing
   generational handles for world identity and resource leases for resident data.
4. Add a small `SharedPtr<T>` and `WeakPtr<T>` only for independently overlapping
   object lifetimes. Atomic counts are the single initial sharing mode.
5. Keep a two-word typed view and opaque control block. Preserve exact dynamic
   destruction, original allocation address, domain, size and alignment.
6. Allocate object and controller together by default; require explicit fallible
   factories, not public raw-pointer shared adoption. Add split allocation only
   for demonstrated large objects retained by long-lived weak observers.
7. Lock a weak reference atomically while the strong count is nonzero. Prevent
   resurrection, counter wrap, and control-block reclamation while weak users
   remain. Retain one implicit weak hold through final object destruction.
8. Synchronize publication and mutable pointer variables separately from counts.
   Final release runs on the releasing thread; thread-affine and delayed physical
   destruction stay in subsystem owners.
9. Use ownership trees and weak back edges; avoid automatic cycle collection,
   shared-from-this, general aliasing, local/atomic modes, and universal intrusive
   ownership until a concrete consumer justifies their complexity.
10. Ship correctness, debugger views and representative measurements before
    claiming performance superiority. Thin public templates erase count logic
    behind a `.cpp` boundary.

## Discovery and reading scope

Searched [game-dev-gems-toc.md](../../references/game-dev-gems-toc.md) for smart
pointers, weak references, reference counting, handles, resource management and
memory management. Selected the four directly relevant Gems articles below.
The TOC contains a retained catalog for Gems 1, and bookmark-derived chapter
links for Gems 3 and 4. Article numbers, authors and printed pagination were
checked against the actual local chapters.

Read all four complete chapters using macOS Vision OCR of rendered scans,
including listings, diagrams, caveats and conclusions. Visually checked each
opening page. Read the additional *Game Coding Complete* section with PDF text
extraction and verified authorship against its title page. Companion CDs and
historical external sample repositories were not inspected. The summaries below
are original paraphrases; no sample implementation was copied.

Local links require the ignored `references/` collection. Printed citations
remain usable without it. File-page numbers below are one-based PDF positions,
not printed page numbers. Modern C++ concurrency and allocation contracts are
Ludus design judgments informed by current primary documentation; the historical
samples do not prove race-free reclamation or current performance.

## Handle-Based Smart Pointers

Brian Hawkins, *Game Programming Gems 3*, article 1.5, printed pp. 44-48;
[local PDF pp. 46-50](../../references/Game%20Programming%20Gems%203.pdf#page=46).
Discovered at the [TOC entry](../../references/game-dev-gems-toc.md#L6878).

Hawkins recommends handles where a designated owner can destroy an object at
an unpredictable time, while reference counting suits independently shared
ownership. His wrapper keeps both a handle and a raw address: validity checks
consult the manager, while ordinary dereference uses the cached pointer.
Additional dereference validation can be enabled for debugging. The chapter
acknowledges identity wrap and the option of preventing it.

**Refinement:** State the lifetime-owner distinction in the selection table.
Permit address caching only through a protected phase or exact-version lease.
Resolve once for a batch, so inner work avoids repeated lookups without making
an unchecked pointer a persistent reference. A successful validity test alone
does not establish reclamation protection.

**Exclude:** Global singleton lookup, permanent handle-plus-address wrappers,
debug-only dangling-reference checks, and probabilistic acceptance of wrap.
An owning smart pointer and a non-owning handle have different contracts even
when both offer convenient syntax.

Applied in [choosing a reference](smart-pointers.md#choosing-a-reference) and
[handles, reload and cycles](smart-pointers.md#handles-reload-and-cycles).

## A Generic Handle-Based Resource Manager

Scott Bilas, *Game Programming Gems 1*, article 1.6, printed pp. 68-79;
[local PDF pp. 66-77](../../references/Game%20Programming%20Gems%201.pdf#page=66).
Discovered at the [TOC entry](../../references/game-dev-gems-toc.md#L6702).

Bilas combines typed handle tags, indexed metadata, validity numbers and a free
list, leaving domain policy with an enclosing manager. The sample permits
wrapping validity numbers and suggests optional validation, singleton access,
and restoring handles alongside the same table organization. Its resource
lookup examples can create resources on demand.

**Refinement:** Reuse Ludus's existing scoped generational-handle mechanics
instead of creating a parallel registry in the pointer system. Make explicit
that handle copy neither retains an allocation nor guarantees readiness.
Validation and a borrow must share the owner's protected phase or synchronization.

**Exclude:** C++ bitfield persistence, wrapping identifiers, assertion-only
boundary validation and hidden resource loading through pointer syntax. Durable
identity remains separate from runtime slots. Preserve explicit construction and
rollback rather than adopting the historical vector-growth assumptions.

Applied in [choosing a reference](smart-pointers.md#choosing-a-reference) and
[handles, reload and cycles](smart-pointers.md#handles-reload-and-cycles).
The detailed registry algorithm remains in [object handles](object-handles-uuid.md).

## The Beauty of Weak References and Null Objects

Noel Llopis, *Game Programming Gems 4*, article 1.7, printed pp. 61-68;
[local PDF pp. 75-82](../../references/Game%20Programming%20Gems%204.pdf#page=75).
Discovered at the [TOC entry](../../references/game-dev-gems-toc.md#L6969).

Llopis places a stable holder between each reference and a replaceable resource
pointer. Holders may be reference counted and resources can be unloaded or
relocated without rewriting every reference. The chapter explicitly distinguishes
its indirection-based meaning of weak from non-owning references in memory
management. Null objects supply type-specific fallback behavior, with warnings
about assumed resource dimensions and accidental mutation of shared fallbacks.

**Refinement:** Reserve `WeakPtr` for observation of one fixed shared lifetime.
A logical resource handle survives eviction/reload; a lease pins one resident
version. Existing borrowers retain that version while new acquisitions select a
published replacement. Optional fallbacks are immutable, type-correct resource
policy with visible diagnostics. Weak promotion simply succeeds or fails.

**Exclude:** Retargeting an in-flight shared view, hiding missing required game
state behind fallback objects, and dynamically allocating one generic holder for
every entity. A stable address is not a concurrent publication protocol, and a
holder count does not prove physical GPU/audio retirement.

Applied in [shared object and controller](smart-pointers.md#shared-object-and-controller)
and [handles, reload and cycles](smart-pointers.md#handles-reload-and-cycles).

## Resource and Memory Management

James Boer, *Game Programming Gems 1*, article 1.7, printed pp. 80-87;
[local PDF pp. 78-85](../../references/Game%20Programming%20Gems%201.pdf#page=78).
Discovered at the [TOC entry](../../references/game-dev-gems-toc.md#L6703).

Boer retains resource metadata while payloads can be discarded and recreated.
Lock counts prevent eviction, and priority, recent access and size inform victim
selection. Ordinary returned pointers can become invalid on the next manager
call. The article adds memory reservation before creation to address the peak
caused by allocating first and repairing a budget afterward.

**Refinement:** Borrowed access and ownership require separate lifetime contracts.
Resource leases, not generic shared counts, govern eviction and exact-version
access. Reserve replacement peaks before preparation and keep old/retiring
storage charged until subsystem completion. Raw borrows cannot survive arbitrary
manager calls unless a phase or lease protects them.

**Exclude:** Synchronous reload through dereference, unconditional manager-destructor
cleanup while users may remain, and treating refcount zero as proof of device or
realtime completion. Eviction scores and loading orchestration remain resource
manager concerns rather than smart-pointer policies.

Applied in [publication, threads and physical retirement](smart-pointers.md#publication-threads-and-physical-retirement)
and [handles, reload and cycles](smart-pointers.md#handles-reload-and-cycles).

## Supporting ownership and modern concurrency references

Mike McShaffry and David Graham, *Game Coding Complete*, fourth edition,
chapter 3, "Smart Pointers and Naked Pointers," printed pp. 68-75;
[local PDF pp. 113-120](../../references/Game%20Coding%20Complete%20-%204th%20Edition.pdf#page=113).
Discovered at the [TOC entry](../../references/game-dev-gems-toc.md#L4920).
Read this complete section, ending before "Using Memory Correctly."
Its examples show manual retain/release mistakes, cyclic shared ownership and
duplicate controllers for one raw allocation. Adopt RAII, weak back edges and
factory-only initial shared construction. Its historical threading discussion
does not establish that `const` access synchronizes writers; use the stronger
publication and all-alias immutability contracts in the final architecture.

Thanks to the ISO C++ working draft authors for
[shared ownership](https://eel.is/c++draft/util.smartptr.shared) and
[atomic weak promotion](https://eel.is/c++draft/util.smartptr.weak.obs), and to
Greg Colvin, Beman Dawes, Peter Dimov and Glen Fernandes for
[Boost.SmartPtr](https://www.boost.org/doc/libs/latest/libs/smart_ptr/doc/html/smart_ptr.html).
Their ownership roles and separation of lifetime safety from payload
synchronization inform the controller contract. Ludus adds explicit failure and
domain lifetime, and retains a smaller API.

[Epic Games' smart pointer documentation](https://dev.epicgames.com/documentation/unreal-engine/smart-pointers-in-unreal-engine?lang=en-US)
provides a current engine example of unique/shared/weak roles and selectable
thread modes. Ludus starts with one atomic shared mode to reduce API combinations;
adding a local mode is a measured extension, not a claim that local counters
cannot be faster. No external library implementation was imported.

## Changes after the article review

| Initial choice | Final clarification | Evidence |
| --- | --- | --- |
| Scoped handles and raw borrows | Validity checks and borrow acquisition need one protected phase; cache addresses only inside it. | Hawkins and Bilas |
| Shared/weak owners for CPU lifetimes | `WeakPtr` observes a fixed incarnation and never redirects to replacement content. | Llopis |
| Resource leases outside pointer core | Exact resident version and dependency closure survive replacement; future acquisitions choose the new version. | Llopis plus existing Ludus resource contracts |
| Subsystem-owned destruction | Counts do not authorize physical retirement; bytes remain charged through device/audio completion. | Boer plus existing Ludus retirement contracts |
| Explicit failure | Optional presentation fallbacks stay immutable and type-correct; required data and expired weak references remain visibly fallible. | Llopis |
| Narrow initial shared API | Public raw shared adoption stays excluded to prevent duplicate ownership controllers. | McShaffry and Graham |

Other TOC entries on allocators, fragmentation and debug memory managers address
adjacent infrastructure, not the core lifetime/promotion algorithm. Their owners
are the existing memory and profiling designs. No allocator redesign was added
solely because a chapter title mentioned memory.
