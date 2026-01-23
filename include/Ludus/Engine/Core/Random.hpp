#pragma once

#include <Ludus/Engine/Core/Random.h>

namespace ludus::core
{
    // PCG32 constants
    inline constexpr uint64_t PCG_MULTIPLIER = 6364136223846793005ULL;
    inline constexpr uint64_t PCG_INCREMENT_SEED = 0x9E3779B97F4A7C15ULL;
    inline constexpr uint64_t SPLITMIX64_MULTIPLIER_1 = 0xBF58476D1CE4E5B9ULL;
    inline constexpr uint64_t SPLITMIX64_MULTIPLIER_2 = 0x94D049BB133111EBULL;
    inline constexpr uint32_t BIT_WIDTH_32 = 31u;
    inline constexpr uint32_t BIT_WIDTH_30 = 30u;
    inline constexpr uint32_t BIT_WIDTH_27 = 27u;
    inline constexpr uint32_t BIT_WIDTH_31 = 31u;

    // Constexpr function implementations
    constexpr Random Random::Create(uint64_t seed) noexcept
    {
        Random rng;
        rng.Reseed(seed);
        return rng;
    }

    constexpr void Random::Reseed(uint64_t seed) noexcept
    {
        const uint64_t stream = splitMix64(seed ^ PCG_INCREMENT_SEED);
        Reseed(seed, stream);
    }

    constexpr void Random::Reseed(uint64_t seed, uint64_t stream) noexcept  // NOLINT(bugprone-easily-swappable-parameters) - intentional API design
    {
        mState = 0u;
        mInc = (stream << 1u) | 1u;
        static_cast<void>(NextU32());
        mState += seed;
        static_cast<void>(NextU32());
    }

    constexpr uint32_t Random::NextU32() noexcept
    {
        const uint64_t oldState = mState;
        mState = oldState * PCG_MULTIPLIER + mInc;
        const uint32_t xorshifted = static_cast<uint32_t>(((oldState >> 18u) ^ oldState) >> 27u);
        const uint32_t rot = static_cast<uint32_t>(oldState >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((0u - rot) & BIT_WIDTH_32));
    }

    constexpr uint64_t Random::NextU64() noexcept
    {
        const uint64_t high = static_cast<uint64_t>(NextU32()) << 32u;
        const uint64_t low = static_cast<uint64_t>(NextU32());
        return high | low;
    }

    constexpr float Random::NextFloat01() noexcept
    {
        // NOLINTNEXTLINE(readability-magic-numbers) - 2^32 scale for float normalization
        constexpr float K_INV = 1.0f / 4294967296.0f;
        return static_cast<float>(NextU32()) * K_INV;
    }

    constexpr double Random::NextDouble01() noexcept
    {
        // NOLINTNEXTLINE(readability-magic-numbers) - 2^64 scale for double normalization
        constexpr double K_INV = 1.0 / 18446744073709551616.0;
        return static_cast<double>(NextU64()) * K_INV;
    }

    constexpr uint32_t Random::RangeU32(uint32_t minInclusive, uint32_t maxInclusive) noexcept
    {
        if (minInclusive > maxInclusive)
        {
            const uint32_t tmp = minInclusive;
            minInclusive = maxInclusive;
            maxInclusive = tmp;
        }

        const uint32_t range = maxInclusive - minInclusive + 1u;

        const uint32_t threshold = (0u - range) % range;
        uint32_t value = 0u;
        do
        {
            value = NextU32();
        } while (value < threshold);

        return minInclusive + (value % range);
    }

    constexpr int32_t Random::RangeI32(int32_t minInclusive, int32_t maxInclusive) noexcept
    {
        if (minInclusive > maxInclusive)
        {
            const int32_t tmp = minInclusive;
            minInclusive = maxInclusive;
            maxInclusive = tmp;
        }

        const uint32_t range = static_cast<uint32_t>(static_cast<int64_t>(maxInclusive) - minInclusive + 1);
        const uint32_t value = RangeU32(0u, range - 1u);
        return static_cast<int32_t>(static_cast<int64_t>(minInclusive) + value);
    }

    constexpr uint64_t Random::splitMix64(uint64_t value) noexcept
    {
        value += PCG_INCREMENT_SEED;
        value = (value ^ (value >> BIT_WIDTH_30)) * SPLITMIX64_MULTIPLIER_1;
        value = (value ^ (value >> BIT_WIDTH_27)) * SPLITMIX64_MULTIPLIER_2;
        return value ^ (value >> BIT_WIDTH_31);
    }
}   // namespace ludus::core
