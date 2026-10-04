#pragma once

// Allocation-free, non-cryptographic random access for simulation events.
// With thanks to John K. Salmon, Mark A. Moraes, Ron O. Dror and David E. Shaw,
// "Parallel Random Numbers: As Easy as 1, 2, 3", SC11 (2011), §4.3 (Philox):
// https://www.thesalmons.org/john/random123/papers/random123sc11.pdf
// We adapt Philox4x32-10 for pure sampling by logical event identity.
// Scope/event/dimension packing and key derivation are Ludus contracts, not
// Random123 APIs. See docs/architecture/randomness.md for ownership and replay.
// Thanks also to Guy W. Lecky-Thompson, "Predictable Random Numbers", Game
// Programming Gems 1 §2.0, pp. 133-140, for independent regeneration; and Bruce
// Dawson, "Game Input Recording and Playback", Game Programming Gems 2 §1.16,
// pp. 105-111, for isolating simulation from presentation randomness. These are
// design inspirations; no chapter code is copied. Detailed review and local
// chapter references: docs/architecture/randomness-gems-review.md.

#include <ludus/foundation/base/core.h>

#include <ludus/foundation/math/status.hpp>

namespace ludus::foundation::math
{
using core::float32;
using core::uint16;
using core::uint32;
using core::uint64;

// Frozen identifiers to store in the owner's replay/content manifest. Changing
// any formula, constants, packing or mapping requires a new version/API.
inline constexpr uint32 RandomKeyDerivationVersion = 1;
inline constexpr uint32 RandomAddressLayoutVersion = 1;
inline constexpr uint32 RandomBoundedMappingVersion = 1;

// Plain replay data; Value may also restore a previously derived key. A key is
// not secret and is not a globally unique identifier.
struct RandomKey
{
    uint64 Value = 0;
};

struct RandomAddress
{
    uint64 Scope = 0;
    uint32 Event = 0;
    uint16 Dimension = 0;
};

struct RandomBlock
{
    uint32 Values[4] = {};
};

// Domain IDs are persistent, nonzero numeric IDs assigned by the owner. Within
// one root seed, different IDs derive distinct keys; statistical independence
// and collision freedom between different roots are not promised.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): documented root seed / domain ID order.
[[nodiscard]] MathStatus TryMakeRandomKey(uint64 rootSeed, uint32 domainId, RandomKey& out) noexcept;

// Use this checked boundary BEFORE narrowing external event/dimension values.
// Scope is a stable entity/chunk ID; Event is an explicit occurrence counter;
// Dimension is a stable semantic slot. Do not use pointers, thread IDs, job
// indices or a shared incrementing draw count. Outputs are unchanged on failure.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): documented scope / event / dimension address order.
[[nodiscard]] MathStatus
TryMakeRandomAddress(uint64 scope, uint64 event, uint64 dimension, RandomAddress& out) noexcept;

// Pure Philox4x32-10, attempt zero, selected lane. Repeating an address repeats
// the result. There is no mutable state, initialization, cache or allocation.
[[nodiscard]] uint32 SampleUInt32(RandomKey key, RandomAddress address) noexcept;

// High 24 bits times 2^-24: [0,1), exactly one raw sample, identical to PCG v1's
// float conversion. This does not make downstream FP simulation deterministic.
[[nodiscard]] float32 SampleFloat01(RandomKey key, RandomAddress address) noexcept;

// Four consecutive dimensions in one Philox evaluation. Dimension must be a
// multiple of four (max 65532); no silent rounding to a different address.
[[nodiscard]] MathStatus TrySampleBlock(RandomKey key, RandomAddress address, RandomBlock& out) noexcept;

// Precompute the division for repeated sampling with the same bound. Default
// bound is one; checked construction prevents an inconsistent threshold.
class PreparedBound32
{
public:
    [[nodiscard]] uint32 Bound() const noexcept
    {
        return mBound;
    }
    [[nodiscard]] uint32 Threshold() const noexcept
    {
        return mThreshold;
    }

private:
    friend MathStatus TryPrepareBound32(uint32 bound, PreparedBound32& out) noexcept;
    uint32 mBound = 1;
    uint32 mThreshold = 0;
};

[[nodiscard]] MathStatus TryPrepareBound32(uint32 bound, PreparedBound32& out) noexcept;

// Unbiased multiply-high/rejection mapping in [0,bound). With thanks to Daniel
// Lemire, "Fast Random Integer Generation in an Interval", ACM TOMACS 29(1),
// 2019, Algorithm 5:
// https://arxiv.org/abs/1805.10941
// Thanks to James McNeill, "Fast Base-2 Functions for Logarithms and Random
// Number Generation", Game Programming Gems 3 §2.1, pp. 157-159, for rejection
// sampling guidance. We adopt Lemire's multiply-high mapping in place of the
// chapter's bit-mask mapping; no chapter code is copied. See the Gems review.
// Each rejection retries ONLY this address. At most 65536 evaluations; budget
// exhaustion returns OutOfRange without modifying out. Bound=0 is invalid;
// bound=1 still evaluates attempt zero. Existing RandomStream v1 is unchanged.
[[nodiscard]] MathStatus TrySampleBounded(RandomKey key, RandomAddress address, uint32 bound, uint32& out) noexcept;
[[nodiscard]] MathStatus
TrySampleBounded(RandomKey key, RandomAddress address, PreparedBound32 bound, uint32& out) noexcept;
} // namespace ludus::foundation::math
