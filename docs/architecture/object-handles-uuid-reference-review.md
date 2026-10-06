# Object handle and UUID reference review

The [final architecture](object-handles-uuid.md) uses owner-scoped generational
handles for runtime incarnations and UUIDs for durable identity. This review
records the initial design, the relevant readings discovered through
[game-dev-gems-toc.md](../../references/game-dev-gems-toc.md), and the changes
that followed. Reviewed on 2026-10-04.

The index covers engine books as well as Gems volumes. Chapter titles were used
for discovery; the findings below follow reading the specified passages. All
summaries are original paraphrases. Proposed concurrency, persistence, and
performance contracts are Ludus engineering judgments, not guarantees established
by the historical samples.

## Design before the article review

The baseline was written into the architecture document before opening the
discovered chapters. It selected:

1. Typed owner/slot/generation handles, retaining the entity layout
   `uint64 / uint32 / uint32`, never-reused process owner tokens, and nonwrapping
   generations with exhausted-slot retirement.
2. Owner-scoped slot metadata and free lists, sharing small primitives while
   keeping storage and lifetime policy in each subsystem.
3. Existing sparse/dense ECS storage and phase-local pointer borrowing with
   structural reader barriers.
4. Optional eight-byte local handles only within a proved owner boundary after
   measurement; no arbitrary bit budgets or C++ bitfields.
5. UUIDv4 durable identity from injected platform entropy, bounded codecs, and
   UUIDv7 only for an actual ordered-index requirement.
6. Distinct asset, source-object, instance, save, runtime, and network identity
   roles, including instance scope for reusable source objects.
7. Explicit cold-path durable resolution, validated handle caches, unresolved
   references retained through streaming, and no implicit loading.
8. Rename/move preservation, transactional graph duplication and remapping,
   and duplicate-identity rejection on import.
9. Fallible preallocated mutation, separate identity and resource retirement,
   and operation-revision checks for delayed work.
10. Readable named values, bounded diagnostics, source mappings, invariant tests,
    and representative benchmarks before making performance claims.

The readings did not overturn this structure. They made its asset residency,
cache, issuance, and authority rules more explicit, and provided concrete
historical mechanisms to exclude.

## Discovery and verification

Searched the complete TOC for handles, identifiers, UUIDs, weak references,
resources, persistence, and serialization. The four Gems articles below were
the most direct matches. Used embedded PDF bookmarks to confirm the Gems 3 and
4 chapter ranges. The Gems 1 index is a retained catalog, and Gems 6 omits chapter
page links, so their article numbering and pagination were checked against the
local scanned chapter pages.

Read all four complete articles with OCR, including samples, caveats, and
conclusions. Visually inspected their opening pages, Bilas's final listing page,
and Kim's final page to confirm titles, authors, and pagination. Read Gregory's
reference discussion using PDF text extraction. PDF positions below are one-based
file pages and are separate from printed book pages. Companion CDs and linked
historical sample repositories were not inspected.

Local PDF and TOC links require the developer's `references/` directory. The exact
author, book, article, and printed-page citations remain usable without that
directory; the published design also links to current public primary references.

## A Generic Handle Based Resource Manager

Scott Bilas, *Game Programming Gems 1*, article 1.6, printed pp. 68–79;
[local PDF pp. 66–77](../../references/Game%20Programming%20Gems%201.pdf#page=66).
[TOC entry](../../references/game-dev-gems-toc.md#L6702).

The article combines a typed handle, an indexed data store, validity numbers,
and a free list beneath a domain-specific manager. Tag types distinguish
resource domains. Its sample uses a wrapping validity field, can omit validation,
and suggests saving handles when reconstructing the same table organization.
It also proposes singleton-based convenience and pointer-like dereferencing.

**Design refinement:** Keep the shared mechanism small and let typed subsystem
facades own policy. Make unpublished reservation and rollback explicit, separate
from any pending incarnation visible to jobs or clients. Keep release validation
mandatory and durable restore independent of runtime table organization.

**Rejected mechanisms:** Wrapping 16-bit validity fields, native bitfield
serialization, assertion-only boundary checks, implicit singleton dereference,
and preserving runtime indices as save identity. The sample's container growth
and historical string-copy assumptions do not establish modern exception-free
allocation behavior or a Ludus performance result.

Applied in [registry validation](object-handles-uuid.md#registry-state-and-validation),
[resolution](object-handles-uuid.md#resolution-and-streaming), and
[module boundaries](object-handles-uuid.md#module-and-api-boundaries).

## Handle Based Smart Pointers

Brian Hawkins, *Game Programming Gems 3*, article 1.5, printed pp. 44–48;
[local PDF pp. 46–50](../../references/Game%20Programming%20Gems%203.pdf#page=46).
[TOC entry](../../references/game-dev-gems-toc.md#L6878).

The article identifies objects with a distinct lifetime owner and unpredictable
destruction as a useful handle case. Its wrapper stores both a handle and a raw
pointer, resolves validity through a manager, and uses the stored pointer for
cheap dereference. Debug dereference can add validation. It acknowledges counter
wrap and the possibility of eliminating it by limiting issuance.

**Design refinement:** Specify exactly what may be cached. Durable references
cache a full handle and binding provenance; they never cache a raw pointer beyond
a protected phase. `Find` makes the borrow visible and can be used once per phase.
Handle copy does not add ownership or extend the borrow. Independently removed
components require their own membership/lifetime checks if retained separately.

**Rejected mechanisms:** A wrapper that exposes pointer-like access while keeping
an unprotected raw address, debug-only dereference safety, global manager lookup,
and accepting rare wrap collisions. The article is useful evidence for separating
ownership from reference convenience; its implementation is not a concurrent
reclamation solution.

Applied in [pointer borrowing](object-handles-uuid.md#storage-and-pointer-borrowing)
and [runtime representation](object-handles-uuid.md#runtime-handle-representation).

## The Beauty of Weak References and Null Objects

Noel Llopis, *Game Programming Gems 4*, article 1.7, printed pp. 61–68;
[local PDF pp. 75–82](../../references/Game%20Programming%20Gems%204.pdf#page=75).
[TOC entry](../../references/game-dev-gems-toc.md#L6969).

The article introduces a stable intermediate record whose resource pointer can
change during relocation or eviction. It discusses optional reference counting
of those holders and per-resource fallback objects. Its caveats include callers
assuming dimensions or other data that a fallback lacks, and unintended writes
to a shared fallback. Its use of “weak” emphasizes indirection, which it explicitly
distinguishes from the usual ownership terminology.

**Design refinement:** Separate a typed logical asset handle from readiness and
from a lease of one resident payload version. Hot reload publishes a validated
replacement while existing readers finish using their original version. Immutable
presentation fallbacks have a named policy and visible diagnostics. Required
gameplay links and authoritative resources remain fallible.

**Rejected mechanisms:** Making all entity references owning, imposing an
individually allocated pointer holder for every resource, redirecting an in-flight borrow, and silently
substituting a null object for required game state. A stable holder alone does
not guarantee race-free replacement or GPU/audio retirement.

Applied in [logical assets](object-handles-uuid.md#logical-assets-and-resident-payloads)
and [physical retirement](object-handles-uuid.md#destruction-and-physical-retirement).

## Generating Globally Unique Identifiers for Game Objects

Yongha Kim, *Game Programming Gems 6*, article 7.3, printed pp. 623–628;
[local PDF pp. 576–581](../../references/Game%20Programming%20Gems%206.pdf#page=576).
[TOC entry](../../references/game-dev-gems-toc.md#L7131).

The article describes operational trouble from exhausting ID spaces and poor bit
allocation. Its proposed 64-bit distributed scheme combines centrally issued time
tags with factory-issued serial numbers. It discusses exhausted fields, advance
allocation of tags, and restricted authority for client-created temporary objects.
The format encodes time and object type and depends on its issuance infrastructure.

**Design refinement:** Make exhaustion and provenance observable, and test terminal
counters directly. Separate provisional client IDs from authoritative replication
bindings. Keep permissions independent of identity. Record names, kinds, revisions,
and issuance history as metadata rather than embedding changing semantics in UUID
bits. State that distributed server issuance is a separate product requirement.

**Rejected mechanisms:** Using this custom format as an RFC UUID, imposing an ID
server on offline authoring, arbitrary lifetime/type bit splits, and adopting
historical assertions about 128-bit affordability without measurements. UUIDv4
plus exact project conflict detection remains the default durable workflow;
nonwrapping scoped counters remain the runtime mechanism.

Applied in [UUID generation](object-handles-uuid.md#persistent-uuid-representation-and-generation),
[network identity](object-handles-uuid.md#persistence-and-networking), and
[observability](object-handles-uuid.md#debugging-and-observability).

## Object References and World Queries

Jason Gregory, *Game Engine Architecture*, third edition, section 16.5.1–16.5.3,
printed pp. 1079–1085;
[local PDF pp. 1098–1104](../../references/Game%20Engine%20Architecture%203rd%20Edition.pdf#page=1098).
[TOC entry](../../references/game-dev-gems-toc.md#L5507).
Read the pointer, smart-pointer, and handle discussion; the query section that
starts on the final page is outside the reviewed scope.

The discussion connects handle indirection with relocation and illustrates how
slot reuse can cause an old handle to name a new object. Its example checks an
object identifier in addition to a table index and explains that the sample is
incomplete. It also distinguishes ownership issues from the choice of reference.

**Design refinement:** Keep validity metadata accessible without dereferencing a
possibly destroyed object. Store owner, generation, and state in registry metadata,
while domain storage maps identity to a current location. Preserve the existing
ECS design rather than adding a global pointer table beside it.

**Rejected mechanisms:** Treating one global table, fixed object inheritance,
or the book's sample lookup as the required modern implementation. Owner scoping,
terminal retirement, and reader synchronization are explicit Ludus extensions.

Applied in [identity validation](object-handles-uuid.md#registry-state-and-validation)
and [storage](object-handles-uuid.md#storage-and-pointer-borrowing).

## Current primary references

[RFC 9562](https://www.rfc-editor.org/rfc/rfc9562.html), particularly sections 4,
5.4, 5.7, 6.8, and 6.9, supplies the standard UUID representation and generation
guidance. It supports the format facts in the architecture. Selection of v4,
strict schema acceptance, bounded collision retries, and migration policy are
Ludus decisions. No UUID algorithm guarantees absolute global uniqueness without
additional coordination.

[Flecs entity documentation](https://www.flecs.dev/flecs/EntitiesComponents.html)
shows recycling with a version counter and liveness rejection.
[EnTT registry documentation](https://github.com/skypjack/entt/wiki/Entity-Component-System)
also describes versioned identifiers, recycling, and registry validity checks.
These confirm that generational identity remains a current technique. They do
not establish Ludus's owner layout, counter-exhaustion policy, serialization
contract, or concurrency safety. No new ECS dependency is proposed.

## Resulting changes

| Area | Refinement to the baseline | Reading |
| --- | --- | --- |
| Creation | Separate private reservation rollback from cancellation of an externally visible pending incarnation. | Bilas |
| Reference caches | Permit handle caching and phase-local pointer reuse; explicitly exclude raw addresses inside long-lived wrappers. | Hawkins |
| Asset residency | Keep logical records stable while payload versions have readiness, leases, and independent retirement. | Llopis |
| Fallback behavior | Permit immutable presentation fallbacks with diagnostics; required gameplay references stay errors. | Llopis |
| Issuance and network authority | Test exhaustion, expose issuance history, and separate client prediction from authoritative bindings. | Kim |
| Storage metadata | Validate before touching payload; retain sparse/dense ECS storage and domain-owned location mapping. | Gregory |

The owner token, nonwrapping generations, UUID/runtime separation, and explicit
reader barriers were baseline choices and existing Ludus strengths. The reviewed
chapters reinforce them but do not supply a measured improvement. The completed
architecture's implementation and benchmark gates remain necessary before any
claim that it is faster or ready to ship.
