---
description: Select lookup hashes, literal fingerprints and explicit untrusted-input policies.
---

# Hash bytes without confusing identity

Use `Ludus::FoundationHash` for the shipped hash primitives. Include
`ludus/foundation/hash/hash.hpp` explicitly. These counted-input functions are
allocation-free and `noexcept`; the xxHash backend is private and does not enter
consumer headers. For owning bytes or binding names, start with the
[strings guide](strings.md). In an existing SDK target, load the installed package and link
`FoundationHash` explicitly when calling its API directly:

```cmake
find_package(Ludus CONFIG REQUIRED)
target_link_libraries(my_game PRIVATE Ludus::FoundationHash)
```

## Choose by purpose

| Purpose | API | Contract |
| --- | --- | --- |
| Trusted in-memory table lookup | `TableHash64` | XXH3-64; noncryptographic, collision candidates still require exact equality |
| Finite literal metadata | `SymbolFingerprint64` | `constexpr` FNV-1a-64 over counted bytes |
| Reproducible noncryptographic fingerprint | `StableFingerprint128` | XXH3-128, seed zero/default secret; caller defines canonical input bytes |
| Hash-table lookup for hostile input | `KeyedTableHash64` | SipHash-2-4 with a caller-supplied secret key |
| Runtime name identity | `NameId` from a scoped table | Table token plus entry index, not a hash |
| Content integrity or authentication | The owning subsystem's digest/security protocol | Do not replace Content's SHA-256 or an authentication scheme with these fingerprints |

Hashing does not normalize Unicode, lowercase names or apply filesystem rules.
Embedded zero bytes are included when the input view/span includes them. Two
hashes matching never establishes exact string equality. The intern table
retains full spellings and checks bytes even when hashes collide.

## Fingerprint an explicit byte recipe

This complete function illustrates the shipped span API and field ordering.
The three bytes are an illustrative recipe, not a serialized C++ object.

```cpp
#include <ludus/foundation/base/types.h>

#include <ludus/foundation/hash/hash.hpp>

#include <span>

[[nodiscard]] ludus::foundation::Fingerprint128 FingerprintRecipe() noexcept
{
    using namespace ludus::foundation;
    const uint8 bytes[] = {1, 7, 42};
    return StableFingerprint128(std::span<const uint8>(bytes));
}
```

For real records, specify field order, fixed widths, byte order, string lengths
and schema version. Frame variable-length fields so different records cannot
produce the same concatenated bytes. Do not hash native structs, pointers,
padding or locale-dependent formatting. Structural schema helpers are not
implemented; the caller owns canonicalization.

The stable fingerprint contract uses pinned xxHash 0.8.3 and reports `Low` and
`High`. When encoding the fingerprint itself, the documented v1 representation
is `Low` then `High`, each little endian. Write fields explicitly with checked
byte codecs; do not dump the struct. Preserve the algorithm/input recipe/version
when persisting fingerprints. This does not define a cooked dictionary format
or an authenticated dictionary protocol.

## Choose the trust policy

`StringTableConfig` defaults to `StringHashPolicy::Trusted`, using XXH3 lookup.
For externally controlled spellings, select `Untrusted`, provide a secret
`SipHashKey` acquired from platform entropy, and set `HasSecretKey` only after
successful acquisition. Missing key declaration makes table creation fail with
`InvalidArgument`; there is no fixed or deterministic secret fallback.

`HasSecretKey` is a caller declaration, not an entropy-quality check. Never use
an example constant, fingerprint, predictable RNG seed or public identifier as
a production secret. Hashing provides no entropy acquisition API. Keep keys
private and out of logs. SipHash protects the lookup policy against chosen-input
hash attacks; it does not authenticate content or limit admitted input volume.

Keep spelling, entry and allocated-byte budgets enabled in both modes. A keyed
hash does not prevent exhaustion by many distinct valid names. Table growth
budgets include peak allocations, not just the eventual payload size.

## Bind literals instead of repeatedly hashing names

`NameLiteral("Player")` computes a 64-bit literal fingerprint and retains its
counted spelling. Bind `literal.Spelling` through `TryIntern` during setup, then
cache the resulting `NameId` for repeated work. The descriptor's FNV fingerprint
is not the table's XXH3/SipHash lookup hash and cannot be passed as an ID.

Existing logging category and profiling identifiers use their own FNV-1a-32
contracts. Adding this module did not change those values or Content digests.
Migration needs an explicit compatibility decision and tests.

## Sources and limits

Thanks to **Yann Collet / xxHash**, *xxHash* v0.8.3, for the private XXH3 backend;
its unmodified vendor source and BSD-2-Clause license are retained. Thanks to
**Jean-Philippe Aumasson and Daniel J. Bernstein**, *SipHash: a fast short-input
PRF*, for the keyed algorithm. Thanks to **Eastlake, Hansen, Noll and Vo**,
*The FNV Non-Cryptographic Hash Algorithm*, RFC 9923, §§2/5, for FNV-1a.
The implementation comments identify these sources and adaptations.

- [Public hash API and source credits](https://github.com/Alegruz/Ludus/blob/main/modules/foundation/hash/include/ludus/foundation/hash/hash.hpp)
- [xxHash v0.8.3 source and license](https://github.com/Alegruz/Ludus/tree/main/third_party/xxhash)
- [Private implementation and SipHash reference](https://github.com/Alegruz/Ludus/blob/main/modules/foundation/hash/src/hash.cpp)
- [Architecture and consulted chapter review](https://github.com/Alegruz/Ludus/blob/main/docs/architecture/strings-gems-review.md)

An opt-in `ludus_strings_benchmark` exists when configured with
`LUDUS_BUILD_STRING_BENCHMARKS=ON`. Its initial results are a baseline, not proof
of engine-frame gains or a universal best algorithm. Representative consumer
measurements must guide layout, hashing and optional optimizations. SIMD UTF-8,
Bloom filters and spelling suggestions remain separate future work.
