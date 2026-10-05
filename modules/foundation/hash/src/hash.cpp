#include <ludus/foundation/hash/hash.hpp>

#include <bit>

// Thanks to Yann Collet and xxHash contributors, "xxHash specification",
// https://github.com/Cyan4973/xxHash/blob/v0.8.3/doc/xxhash_spec.md.
// Use the unmodified BSD-2-Clause implementation privately; see third_party/
// xxhash/LICENSE and docs/architecture/strings.md for algorithm policy.
#define XXH_STATIC_LINKING_ONLY
#define XXH_IMPLEMENTATION
#define XXH_NAMESPACE LUDUS_PRIVATE_
#include <xxhash.h>

namespace ludus::foundation
{
uint64 TableHash64(std::span<const uint8> bytes) noexcept
{
    return XXH3_64bits(bytes.data(), bytes.size());
}

Fingerprint128 StableFingerprint128(std::span<const uint8> bytes) noexcept
{
    const auto value = XXH3_128bits(bytes.data(), bytes.size());
    return {value.low64, value.high64};
}

// Thanks to Jean-Philippe Aumasson and Daniel J. Bernstein, "SipHash: a fast
// short-input PRF", INDOCRYPT 2012, section 2, https://eprint.iacr.org/2012/351.
// Implement the specified 2 compression / 4 finalization rounds with explicit
// little-endian byte loads; no unaligned loads or native representation hashing.
namespace
{
void Round(uint64& v0, uint64& v1, uint64& v2, uint64& v3) noexcept
{
    v0 += v1;
    v1 = std::rotl(v1, 13);
    v1 ^= v0;
    v0 = std::rotl(v0, 32);
    v2 += v3;
    v3 = std::rotl(v3, 16);
    v3 ^= v2;
    v0 += v3;
    v3 = std::rotl(v3, 21);
    v3 ^= v0;
    v2 += v1;
    v1 = std::rotl(v1, 17);
    v1 ^= v2;
    v2 = std::rotl(v2, 32);
}
} // namespace

uint64 KeyedTableHash64(std::span<const uint8> bytes, SipHashKey key) noexcept
{
    uint64 v0 = 0x736f6d6570736575ULL ^ key.Low;
    uint64 v1 = 0x646f72616e646f6dULL ^ key.High;
    uint64 v2 = 0x6c7967656e657261ULL ^ key.Low;
    uint64 v3 = 0x7465646279746573ULL ^ key.High;
    usize offset = 0;
    while (bytes.size() - offset >= 8)
    {
        uint64 word = 0;
        for (usize i = 0; i < 8; ++i)
        {
            word |= static_cast<uint64>(bytes[offset + i]) << (8 * i);
        }
        v3 ^= word;
        Round(v0, v1, v2, v3);
        Round(v0, v1, v2, v3);
        v0 ^= word;
        offset += 8;
    }
    uint64 tail = static_cast<uint64>(bytes.size() & 255) << 56;
    for (usize i = 0; i < bytes.size() - offset; ++i)
    {
        tail |= static_cast<uint64>(bytes[offset + i]) << (8 * i);
    }
    v3 ^= tail;
    Round(v0, v1, v2, v3);
    Round(v0, v1, v2, v3);
    v0 ^= tail;
    v2 ^= 255;
    for (uint32 i = 0; i < 4; ++i)
    {
        Round(v0, v1, v2, v3);
    }
    return v0 ^ v1 ^ v2 ^ v3;
}
} // namespace ludus::foundation
