#pragma once

#include <ludus/foundation/base/types.h>

#include <span>
#include <string_view>

namespace ludus::foundation
{
struct Fingerprint128 final
{
    uint64 Low{};
    uint64 High{};
    friend constexpr bool operator==(Fingerprint128, Fingerprint128) noexcept = default;
};

struct SipHashKey final
{
    uint64 Low{};
    uint64 High{};
};

// Thanks to Eastlake, Hansen, Noll and Vo, "The FNV Non-Cryptographic Hash
// Algorithm", RFC 9923, sections 2/5 (https://www.rfc-editor.org/rfc/rfc9923).
// Adopt FNV-1a-64 for literal descriptors only, never collision-free identity.
// Thanks to Stefan Reinalter, "Compile-Time String Hashing in C++", Game Engine
// Gems 3, ch.14, pp.197-205: precompute finite literals; use a C++23 loop rather
// than recursive 32-bit templates. See docs/architecture/strings-gems-review.md.
[[nodiscard]] constexpr uint64 SymbolFingerprint64(std::string_view bytes) noexcept
{
    uint64 result = 14695981039346656037ULL;
    for (char byte : bytes)
    {
        result ^= static_cast<uint8>(byte);
        result *= 1099511628211ULL;
    }
    return result;
}

// Non-cryptographic XXH3, pinned 0.8.3; no public vendor headers or hash cache.
[[nodiscard]] uint64 TableHash64(std::span<const uint8> bytes) noexcept;
// XXH3-128 seed zero/default secret. Persistent format v1: Low then High,
// each little endian, over caller-defined canonical bytes. Not an identity.
[[nodiscard]] Fingerprint128 StableFingerprint128(std::span<const uint8> bytes) noexcept;
// SipHash-2-4. Caller supplies a platform-generated secret key for hostile input;
// this module performs no entropy acquisition and provides no fixed fallback.
[[nodiscard]] uint64 KeyedTableHash64(std::span<const uint8> bytes, SipHashKey key) noexcept;
} // namespace ludus::foundation
