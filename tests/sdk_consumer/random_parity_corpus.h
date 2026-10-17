#pragma once

#include <ludus/foundation/base/core.h>
#include <ludus/foundation/containers/array.hpp>
#include <ludus/foundation/containers/borrowed_view.hpp>
#include <ludus/foundation/containers/static_array.hpp>
#include <ludus/foundation/math/addressed_random.hpp>

#include "random_parity_layout.h"

#include <cstring>

// Case corpus and comparison for FoundationMath's shader port of addressed
// random v1 (modules/foundation/math/shaders/math.slang); see
// docs/architecture/randomness.md#gpu-addressed-sampling. Shared by the native
// renderer test (gpu_random_tests.cpp) and the WebGPU browser fixture
// (random_parity.cpp) so every backend checks the same cases. Every expected
// value comes from the CPU reference or a pinned known answer, never from a GPU.
namespace ludus::tests::random_parity
{
namespace math = ludus::foundation::math;
using foundation::Array;
using foundation::BorrowedView;
using foundation::float32;
using foundation::StaticArray;
using foundation::uint16;
using foundation::uint32;
using foundation::uint64;
using foundation::usize;

/// Mismatches recorded in detail by CompareResults; the total count is unbounded.
inline constexpr usize MAX_REPORTED_MISMATCHES = 16;

/// One kernel input: (kind, key low, key high, bound) then a kind-specific operand.
struct ParityCase final
{
    math::RandomBlock Header;
    math::RandomBlock Operand;
};
static_assert(sizeof(ParityCase) == LUDUS_RANDOM_PARITY_WORDS_PER_CASE * sizeof(math::RandomBlock));

/// One kernel case with the result the GPU must reproduce; named fields keep the
/// operand and expected words from being exchanged.
struct ParityEntry final
{
    uint32 Kind = LUDUS_RANDOM_PARITY_CASE_PADDING;
    math::RandomKey Key;
    uint32 Bound = 0;
    math::RandomBlock Operand;
    math::RandomBlock Expected;
};

/// Kernel inputs and the expected result of each, in dispatch order.
class ParityCorpus final
{
public:
    void Add(const ParityEntry& entry) noexcept
    {
        const ParityCase item
        {
            .Header = {{entry.Kind,
                        static_cast<uint32>(entry.Key.Value),
                        static_cast<uint32>(entry.Key.Value >> 32u),
                        entry.Bound}},
            .Operand = entry.Operand,
        };
        if (mCases.TryAdd(item) == false || mExpected.TryAdd(entry.Expected) == false)
        {
            mHasFailedAllocation = true;
        }
    }
    void AddSample(math::RandomKey key, uint64 scope, uint32 event, uint32 dimension, uint32 bound) noexcept
    {
        // An out-of-range dimension must alias dimension % 65536, as CPU uint16
        // narrowing does, and never a retry attempt; bounded sampling rejects it.
        const bool isInRange = dimension <= math::RandomMaxDimension;
        const math::RandomAddress address{scope, event, static_cast<uint16>(dimension)};
        const uint32 raw = math::SampleUInt32(key, address);
        const float32 sample = math::SampleFloat01(key, address);
        uint32 sampleBits = 0;
        std::memcpy(&sampleBits, &sample, sizeof(sampleBits));
        uint32 bounded = 0;
        uint32 flags = isInRange == true ? LUDUS_RANDOM_PARITY_IS_IN_RANGE : 0u;
        if (isInRange == true && math::TrySampleBounded(key, address, bound, bounded) == math::MathStatus::Success)
        {
            flags |= LUDUS_RANDOM_PARITY_IS_SAMPLED;
        }
        else
        {
            bounded = 0;
        }
        Add(
        {
            .Kind = LUDUS_RANDOM_PARITY_CASE_SAMPLE,
            .Key = key,
            .Bound = bound,
            .Operand = {{static_cast<uint32>(scope), static_cast<uint32>(scope >> 32u), event, dimension}},
            .Expected = {{raw, sampleBits, bounded, flags}},
        });
    }
    void AddMultiply(uint32 a, uint32 b) noexcept
    {
        const uint64 product = static_cast<uint64>(a) * b;
        Add(
        {
            .Kind = LUDUS_RANDOM_PARITY_CASE_MULTIPLY,
            .Key = {},
            .Bound = 0,
            .Operand = {{a, b, 0, 0}},
            .Expected = {{static_cast<uint32>(product), static_cast<uint32>(product >> 32u), 0, 0}},
        });
    }
    void PadToGroup() noexcept
    {
        while (mCases.GetSize() % LUDUS_RANDOM_PARITY_GROUP_SIZE != 0 && mHasFailedAllocation == false)
        {
            Add({ .Kind = LUDUS_RANDOM_PARITY_CASE_PADDING, .Key = {}, .Bound = 0, .Operand = {}, .Expected = {} });
        }
    }
    [[nodiscard]] bool HasFailedAllocation() const noexcept
    {
        return mHasFailedAllocation;
    }
    [[nodiscard]] const Array<ParityCase>& GetCases() const noexcept
    {
        return mCases;
    }
    [[nodiscard]] const Array<math::RandomBlock>& GetExpected() const noexcept
    {
        return mExpected;
    }
    /// Kernel input size; the result buffer holds one RandomBlock per case.
    [[nodiscard]] usize GetCaseBytes() const noexcept
    {
        return mCases.GetSize() * sizeof(ParityCase);
    }
    [[nodiscard]] usize GetResultBytes() const noexcept
    {
        return mCases.GetSize() * sizeof(math::RandomBlock);
    }
    /// Compute workgroups that cover every case; PadToGroup makes the division exact.
    [[nodiscard]] uint32 GetGroupCount() const noexcept
    {
        return static_cast<uint32>(mCases.GetSize() / LUDUS_RANDOM_PARITY_GROUP_SIZE);
    }

private:
    Array<ParityCase> mCases;
    Array<math::RandomBlock> mExpected;
    bool mHasFailedAllocation = false;
};

/// Builds the full corpus: Random123 and Ludus v1 known answers, layout
/// constants, forced retry-loop boundaries, endpoint floats, live CPU parity for
/// raw/float/bounded samples and MultiplyWide limb sweeps, padded to whole
/// workgroups.
/// @return False when allocation fails or the CPU reference disagrees with a
///         pinned key derivation; the corpus is then unusable.
[[nodiscard]] inline bool TryBuildCorpus(ParityCorpus& outCorpus) noexcept
{
    // Random123 tests/kat_vectors, revision 9545ff6413f258be2f04c1d319d99aaef7521150, the same
    // pinned zero, all-ones and mixed vectors as FoundationMath's addressed_random_tests.cpp.
    // https://github.com/DEShawResearch/random123/blob/9545ff6413f258be2f04c1d319d99aaef7521150/tests/kat_vectors
    outCorpus.Add(
    {
        .Kind = LUDUS_RANDOM_PARITY_CASE_PHILOX,
        .Key = {0},
        .Bound = 0,
        .Operand = {{0, 0, 0, 0}},
        .Expected = {{0x6627e8d5u, 0xe169c58du, 0xbc57ac4cu, 0x9b00dbd8u}},
    });
    outCorpus.Add(
    {
        .Kind = LUDUS_RANDOM_PARITY_CASE_PHILOX,
        .Key = {0xffffffffffffffffULL},
        .Bound = 0,
        .Operand = {{0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu}},
        .Expected = {{0x408f276du, 0x41c83b0eu, 0xa20bc7c6u, 0x6d5451fdu}},
    });
    outCorpus.Add(
    {
        .Kind = LUDUS_RANDOM_PARITY_CASE_PHILOX,
        .Key = {0x299f31d0a4093822ULL},
        .Bound = 0,
        .Operand = {{0x243f6a88u, 0x85a308d3u, 0x13198a2eu, 0x03707344u}},
        .Expected = {{0xd16cfe09u, 0x94fdccebu, 0x5001e420u, 0x24126ea1u}},
    });

    outCorpus.Add(
    {
        .Kind = LUDUS_RANDOM_PARITY_CASE_CONSTANTS,
        .Key = {},
        .Bound = 0,
        .Operand = {},
        .Expected = {{math::RandomAddressLayoutVersion,
                      math::RandomBoundedMappingVersion,
                      math::RandomMaxDimension,
                      math::RandomBoundedAttemptCount}},
    });

    // Retry loop boundaries through the production loop with injected draws.
    // Bound 0x80000001 has threshold 0x7fffffff, so a zero draw always rejects.
    constexpr uint32 HALF_REJECTING_BOUND = 0x80000001u;
    outCorpus.Add(
    {
        .Kind = LUDUS_RANDOM_PARITY_CASE_FORCED_EXHAUSTION,
        .Key = {},
        .Bound = HALF_REJECTING_BOUND,
        .Operand = {},
        .Expected = {{0, 0, 0, 0}},
    });
    outCorpus.Add(
    {
        .Kind = LUDUS_RANDOM_PARITY_CASE_FORCED_EXHAUSTION,
        .Key = {},
        .Bound = 0,
        .Operand = {},
        .Expected = {{0, 0, 0, 0}},
    });
    outCorpus.Add(
    {
        .Kind = LUDUS_RANDOM_PARITY_CASE_FORCED_LAST_ATTEMPT,
        .Key = {},
        .Bound = HALF_REJECTING_BOUND,
        .Operand = {},
        .Expected = {{0, LUDUS_RANDOM_PARITY_IS_SAMPLED, 0, 0}},
    });
    outCorpus.Add(
    {
        .Kind = LUDUS_RANDOM_PARITY_CASE_FORCED_CONSTANT,
        .Key = {},
        .Bound = 1,
        .Operand = {{0xffffffffu, 0, 0, 0}},
        .Expected = {{0, LUDUS_RANDOM_PARITY_IS_SAMPLED, 0, 0}},
    });

    // Endpoint floats pinned independently of the CPU mapping code.
    constexpr usize ENDPOINT_FLOAT_COUNT = 5;
    const StaticArray<uint32, ENDPOINT_FLOAT_COUNT> floatBits = {0u, 255u, 256u, 0x80000000u, 0xffffffffu};
    const StaticArray<float32, ENDPOINT_FLOAT_COUNT> floatValues = {0.0f, 0.0f, 0x1p-24f, 0.5f, 1.0f - 0x1p-24f};
    for (usize i = 0; i < ENDPOINT_FLOAT_COUNT; ++i)
    {
        uint32 expectedBits = 0;
        std::memcpy(&expectedBits, &floatValues[i], sizeof(expectedBits));
        outCorpus.Add(
        {
            .Kind = LUDUS_RANDOM_PARITY_CASE_FLOAT,
            .Key = {},
            .Bound = 0,
            .Operand = {{floatBits[i], 0, 0, 0}},
            .Expected = {{expectedBits, 0, 0, 0}},
        });
    }

    // Ludus v1 golden answers from addressed_random_tests.cpp, pinned so the GPU
    // is checked against frozen values as well as the live CPU reference.
    math::RandomKey key;
    if (math::TryMakeRandomKey(42, 7, key) != math::MathStatus::Success || key.Value != 0xccf635ee9e9e2fa4ULL)
    {
        return false;
    }
    const uint32 sampled = LUDUS_RANDOM_PARITY_IS_SAMPLED | LUDUS_RANDOM_PARITY_IS_IN_RANGE;
    const float32 golden = 0.5788143277168274f;
    uint32 goldenBits = 0;
    std::memcpy(&goldenBits, &golden, sizeof(goldenBits));
    outCorpus.Add(
    {
        .Kind = LUDUS_RANDOM_PARITY_CASE_SAMPLE,
        .Key = key,
        .Bound = 1000,
        .Operand = {{0x89abcdefu, 0x01234567u, 99, 1234}},
        .Expected = {{0x942d2d40u, goldenBits, 578, sampled}},
    });
    // Attempts 0 and 1 reject at this address; attempt 2 is accepted.
    math::RandomBlock forcedRejection = {{0xa3a6ca03u, 0, 0x3c0cb600u, sampled}};
    const float32 forcedSample = math::SampleFloat01(key, {});
    std::memcpy(&forcedRejection.Values[1], &forcedSample, sizeof(uint32));
    outCorpus.Add(
    {
        .Kind = LUDUS_RANDOM_PARITY_CASE_SAMPLE,
        .Key = key,
        .Bound = HALF_REJECTING_BOUND,
        .Operand = {},
        .Expected = forcedRejection,
    });
    // The aligned block holding dimensions 1232-1235 (TrySampleBlock's golden answer).
    outCorpus.Add(
    {
        .Kind = LUDUS_RANDOM_PARITY_CASE_PHILOX,
        .Key = key,
        .Bound = 0,
        .Operand = {{0x89abcdefu, 0x01234567u, 99, 1232u >> 2u}},
        .Expected = {{0x991146b3u, 0x1aa17390u, 0x942d2d40u, 0x487e565du}},
    });

    // Live CPU parity across key words, both scope words, event extremes,
    // lane boundaries 0-3/4-7, the top of the dimension range, out-of-range
    // dimensions and bounds that force rejection (0x80000001), exceed 16 bits
    // or are invalid (0).
    math::RandomKey derived;
    if (math::TryMakeRandomKey(0xffffffffffffffffULL, 0xffffffffu, derived) != math::MathStatus::Success)
    {
        return false;
    }
    const math::RandomKey keys[] = {{0}, {0xffffffffffffffffULL}, key, derived, {0x0123456789abcdefULL}};
    const uint64 scopes[] = {0, 1, 0xffffffffULL, 0x100000000ULL, 0x0123456789abcdefULL, 0xffffffffffffffffULL};
    const uint32 events[] = {0, 1, 99, 0x80000000u, 0xffffffffu};
    const uint32 dimensions[] =
        {0, 1, 2, 3, 4, 5, 6, 7, 8, 1232, 1235, 65531, 65532, 65535, 0x10000, 0x10000 + 1234, 0x20003, 0xffffffffu};
    const uint32 bounds[] =
        {1, 2, 3, 1000, 65535, 65536, 65537, 0x7fffffffu, 0x80000000u, HALF_REJECTING_BOUND, 0xffffffffu, 0};
    for (const math::RandomKey& sampleKey : keys)
    {
        for (const uint64 scope : scopes)
        {
            for (const uint32 event : events)
            {
                for (const uint32 dimension : dimensions)
                {
                    for (const uint32 bound : bounds)
                    {
                        outCorpus.AddSample(sampleKey, scope, event, dimension, bound);
                    }
                }
            }
        }
    }

    // MultiplyWide: every pair of 16-bit limb edge values, then exhaustive sweeps
    // of one limb against an all-ones operand where every carry is maximal.
    const uint32 edges[] = {0u,          1u,          2u,          0x7fffu,     0x8000u,     0xfffeu,
                            0xffffu,     0x10000u,    0x10001u,    0x1ffffu,    0x7fffffffu, 0x80000000u,
                            0x80000001u, 0xfffeffffu, 0xffff0000u, 0xffff0001u, 0xfffffffeu, 0xffffffffu,
                            0xd2511f53u, 0xcd9e8d57u, 0x9e3779b9u, 0xbb67ae85u};
    for (const uint32 a : edges)
    {
        for (const uint32 b : edges)
        {
            outCorpus.AddMultiply(a, b);
        }
    }
    constexpr uint32 LIMB_VALUES = 0x10000u;
    for (uint32 limb = 0; limb < LIMB_VALUES; ++limb)
    {
        outCorpus.AddMultiply(0xffffffffu, 0xffff0000u | limb);
        outCorpus.AddMultiply((limb << 16u) | 0xffffu, 0xffffffffu);
    }
    outCorpus.PadToGroup();
    return outCorpus.HasFailedAllocation() == false;
}

/// One result word that differs from the CPU reference or a pinned answer.
struct ParityMismatch final
{
    usize Index = 0;
    uint32 Kind = 0;
    usize Lane = 0;
    uint32 Actual = 0;
    uint32 Expected = 0;
};

/// Outcome of CompareResults: every mismatching word is counted, the first
/// MAX_REPORTED_MISMATCHES are recorded.
struct ParityComparison final
{
    usize MismatchCount = 0;
    StaticArray<ParityMismatch, MAX_REPORTED_MISMATCHES> Reported{};
};

/// Compares GPU results with the corpus expectations word by word.
/// @param actual One RandomBlock per corpus case, in dispatch order; storage
///        stays owned by the caller.
/// @return False when actual is invalid or its extent differs from the corpus;
///         outComparison is then left unchanged.
[[nodiscard]] inline bool TryCompareResults(const ParityCorpus& corpus,
                                            BorrowedView<const math::RandomBlock> actual,
                                            ParityComparison& outComparison) noexcept
{
    const Array<ParityCase>& cases = corpus.GetCases();
    const Array<math::RandomBlock>& expected = corpus.GetExpected();
    if (actual.IsValid() == false || actual.GetSize() != cases.GetSize() || expected.GetSize() != cases.GetSize())
    {
        return false;
    }
    ParityComparison comparison;
    constexpr usize LANES_PER_RESULT = 4;
    for (usize index = 0; index < cases.GetSize(); ++index)
    {
        for (usize lane = 0; lane < LANES_PER_RESULT; ++lane)
        {
            if (actual[index].Values[lane] == expected[index].Values[lane])
            {
                continue;
            }
            if (comparison.MismatchCount < MAX_REPORTED_MISMATCHES)
            {
                comparison.Reported[comparison.MismatchCount] =
                {
                    .Index = index,
                    .Kind = cases[index].Header.Values[0],
                    .Lane = lane,
                    .Actual = actual[index].Values[lane],
                    .Expected = expected[index].Values[lane],
                };
            }
            ++comparison.MismatchCount;
        }
    }
    outComparison = comparison;
    return true;
}
} // namespace ludus::tests::random_parity
