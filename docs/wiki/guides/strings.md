---
description: Choose string ownership, validate text and bind names through scoped tables.
---

# Own text and bind names

Use `Ludus::FoundationStrings` for counted byte storage and scoped name tables.
The implementation is available to native and browser SDK consumers. Include
its public headers explicitly; Base's `core.h` does not include strings.
Functions report failure through `StringStatus` and do not use exceptions.
In an existing SDK game target, load the installed package and link the target:

```cmake
find_package(Ludus CONFIG REQUIRED)
target_link_libraries(my_game PRIVATE Ludus::FoundationStrings)
```

## Choose the owner

| Type | Use and ownership contract |
| --- | --- |
| `std::string_view` | Synchronous reads; borrowed bytes, copying retains no owner |
| `StaticString<N>` | Fixed byte limit; inline value copy, capacity excludes the terminator |
| `String` | Unique mutable bytes; move-only, fallible deep copy through `CloneTo` |
| `StringBuilder` | Reusable append buffer; reserve, clear and transfer into `String` |
| `SharedString` | Retained immutable bytes; copies share one allocation without allocating |
| `NameId` | Repeated identifier; token/index belonging to one `StringTable` |
| `StringIndex` | Index meaningful only with its particular table/dictionary owner |
| `Utf8View` | Borrowed UTF-8 validation proof; keep the source alive and unchanged |

`String`, `StaticString<N>` and `SharedString` preserve embedded zero bytes and
store an extra trailing zero outside their counted length. Sizes are **bytes**,
not characters or display widths. `String` currently keeps 23 bytes inline;
that threshold and its layout are implementation choices, not a stable ABI.

A borrowed view becomes unsafe when its owner dies or its storage changes.
`GetView()` on temporary owners is deleted, but ordinary views cannot detect
all lifetime mistakes. For queued work, retain a `SharedString` or keep the
owning table/snapshot alive until the job finishes.

## Build a label without partial publication

This complete function builds privately and replaces `output` only after every
append succeeds. It also works when `name` borrows the old output.

```cpp
#include <ludus/foundation/strings/string.hpp>

#include <string_view>

[[nodiscard]] ludus::foundation::StringStatus BuildLabel(std::string_view name,
                                                         ludus::foundation::String& output) noexcept
{
    using namespace ludus::foundation;
    StringBuilder builder(output.GetDomain());
    StringStatus status = builder.TryAppend("Player: ");
    if (status != StringStatus::Ok)
    {
        return status;
    }
    status = builder.TryAppend(name);
    if (status != StringStatus::Ok)
    {
        return status;
    }
    output = builder.TakeString();
    return StringStatus::Ok;
}
```

Use `TryEnsureCapacity` for known workloads or `TryAppendMany` for a batch of
views. `Clear()` retains a builder's capacity for reuse. Counted append operations
support self-borrowed slices. Fixed-capacity overflow returns `CapacityExceeded`
without truncation. Allocation failure returns `OutOfMemory`; owners and checked
operation outputs retain their previous logical values on failure.

`String` defaults to the process-lifetime system allocation domain. A custom
`AllocationDomain` and its callback context must outlive all allocations made
through it. Byte assignment keeps the destination domain; moves carry the
source domain. `CloneTo(domain, output)` makes allocation and deep-copy failure
explicit, while `CopyToString(shared, output)` makes an editable copy.

## Retain immutable bytes

This function verifies that copying a nonempty shared value retains the same
bytes. `CreateShared` allocates the original block; the subsequent copy does not.

```cpp
#include <ludus/foundation/memory/allocation_domain.hpp>
#include <ludus/foundation/strings/shared_string.hpp>

// Thanks to The Qt Company, "Implicit Sharing", Qt 6 documentation:
// https://doc.qt.io/qt-6/implicit-sharing.html
// Ludus shares immutable bytes; it does not implement mutation/detachment.
[[nodiscard]] bool VerifySharedCopies() noexcept
{
    using namespace ludus::foundation;
    SharedString original;
    if (CreateShared("Player", GetSystemAllocationDomain(), original) != StringStatus::Ok)
    {
        return false;
    }
    const SharedString retained = original;
    return retained.GetData() == original.GetData() && retained == original;
}
```

Shared bytes are immutable: editing requires an explicit unique copy. Separate
handles may be copied/destroyed on different threads after safe publication.
Concurrent access to the **same handle** still needs caller synchronization.
An empty shared value requires no allocation. This API has no copy-on-write
mutation, weak references or shared slice owner.

## Bind once, then freeze for reads

Create a table at an explicit initialization boundary, intern exact spellings,
and cache the returned IDs. This function publishes an ID only when freezing
succeeds; `output` must initially be an empty `FrozenStringTable`.

```cpp
#include <ludus/foundation/memory/allocation_domain.hpp>
#include <ludus/foundation/strings/string_table.hpp>

[[nodiscard]] ludus::foundation::StringStatus BindPlayer(ludus::foundation::FrozenStringTable& output,
                                                         ludus::foundation::NameId& outputId) noexcept
{
    using namespace ludus::foundation;
    StringTable table;
    StringStatus status = CreateTable({}, GetSystemAllocationDomain(), table);
    if (status != StringStatus::Ok)
    {
        return status;
    }
    // Thanks to Stefan Reinalter, "Compile-Time String Hashing in C++",
    // Game Engine Gems 3, ch.14, pp.197-205: precompute literal metadata.
    // Ludus retains spelling and binds an exact, table-qualified ID.
    // See docs/architecture/strings-gems-review.md.
    constexpr NameLiteral player("Player");
    NameId id;
    status = table.TryIntern(player.Spelling, id);
    if (status != StringStatus::Ok)
    {
        return status;
    }
    status = table.TryFreeze(output);
    if (status == StringStatus::Ok)
    {
        outputId = id;
    }
    return status;
}
```

`TryIntern` may allocate; duplicates return the existing ID, even at admission
capacity. `TryFind` never inserts or allocates and returns `NotFound` for a miss.
`TryResolve(id, view)` checks table ownership and borrows that table's bytes.
`NameId::IsValid()` checks representation only, not whether its owner still lives.

The default table allows 4096 bytes per spelling, 65536 entries including the
empty entry at index zero, and 8 MiB allocated including growth/rehash peak.
It requires UTF-8 and rejects embedded zeros. Choose explicit budgets and the
[hash trust policy](hashing.md#choose-the-trust-policy) for your input.

Mutable table operations synchronize internally. Freeze, move and destruction
require all concurrent operations to finish first. Successful freeze consumes
the mutable owner without allocation or changing IDs; frozen lookups do not
acquire its mutation mutex. Keep the frozen owner alive for every view and job.

A `NameLiteral` precomputes a fingerprint and retains spelling; it is not an
interned ID. Binding remains explicit, and its source storage must survive until
binding. Exact byte comparison handles collisions in every build flavor.

Runtime table tokens and IDs are process-local. Persist canonical spelling or
an existing stable resource identity; do not save table tokens, pointers or bare
indices as durable identity. Static SDK plugins bind names through the host's
table API so the token issuer exists once. Keep C++ owners and allocation-domain
pointers outside the existing plain game-module ABI.

## Validate at the boundary

`ValidateUtf8(bytes, view, errorOffset)` rejects malformed sequences and reports
the first invalid sequence's byte offset; failure preserves `view`. Success
sets the offset to zero. The returned view borrows the original bytes and does
not normalize, case-fold, segment graphemes or repair input.

U+0000 is valid UTF-8. At a C-string boundary, also call
`ValidateCString(bytes)` to reject embedded zeros. A counted literal containing
one must use an explicit length, for example `std::string_view("a\0b", 3)`.
The C-string check alone does not validate encoding or create owned storage;
pass a terminated owner's data when the receiving API requires termination.

When a checked call fails, propagate its status or show a bounded diagnostic.
Do not reinterpret failure as an empty successful value. UTF-16 conversion,
checked text slicing, normalization and SIMD validation are separate future APIs.

## Implementation and further reading

The file logger shares its base/active path allocation until rotation. The
[SDK example](https://github.com/Alegruz/Ludus/blob/main/tests/sdk_consumer/strings.cpp)
and native/browser tests exercise the public contracts. Cooked deterministic
dictionaries, content snapshot integration, format adapters, debugger printers
and an editor strings inspector remain follow-on work.

See the [implementation status](https://github.com/Alegruz/Ludus/blob/main/docs/architecture/strings.md#implementation-status),
[public headers](https://github.com/Alegruz/Ludus/tree/main/modules/foundation/strings/include/ludus/foundation/strings),
[hashing guide](hashing.md) and [reference reading path](../learn/references.md#6-own-text-and-identify-names).
Thanks to The Qt Company, *Implicit Sharing*, Qt 6 documentation, for cheap
shared-copy inspiration; Ludus uses immutable storage rather than detachment.
The architecture records that adaptation and the consulted Gems chapters.
