#pragma once

#include <Ludus/Engine/Core/Random.h>

namespace ludus::core
{
    // Constexpr function implementations
    inline constexpr Random Random::Create(const uint64_t seed) noexcept
    {
        Random rng;
        rng.Reseed(seed);
        return rng;
    }

    inline constexpr void Random::Reseed(const uint64_t seed) noexcept
    {
        const uint64_t stream = splitMix64(seed ^ 0x9E3779B97F4A7C15ULL);
        Reseed(seed, stream);
    }

    inline constexpr void Random::Reseed(const uint64_t seed, const uint64_t stream) noexcept
    {
        mState = 0u;
        mInc = (stream << 1u) | 1u;
        [[maybe_unused]] uint32_t nextU32 = NextU32();
        mState += seed;
        nextU32 = NextU32();
    }

    inline constexpr uint32_t Random::NextU32() noexcept
    {
        const uint64_t oldState = mState;
        mState = oldState * 6364136223846793005ULL + mInc;
        const uint32_t xorshifted = static_cast<uint32_t>(((oldState >> 18u) ^ oldState) >> 27u);
        const uint32_t rot = static_cast<uint32_t>(oldState >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((0u - rot) & 31u));
    }

    inline constexpr uint64_t Random::NextU64() noexcept
    {
        const uint64_t high = static_cast<uint64_t>(NextU32()) << 32u;
        const uint64_t low = static_cast<uint64_t>(NextU32());
        return high | low;
    }

    inline constexpr float Random::NextFloat01() noexcept
    {
        constexpr float kInv = 1.0f / 4294967296.0f;
        return static_cast<float>(NextU32()) * kInv;
    }

    inline constexpr double Random::NextDouble01() noexcept
    {
        constexpr double kInv = 1.0 / 18446744073709551616.0;
        return static_cast<double>(NextU64()) * kInv;
    }

    inline constexpr uint32_t Random::RangeU32(uint32_t minInclusive, uint32_t maxInclusive) noexcept
    {
        if (minInclusive > maxInclusive)
        {
            const uint32_t tmp = minInclusive;
            minInclusive = maxInclusive;
            maxInclusive = tmp;
        }

        const uint32_t range = maxInclusive - minInclusive + 1u;
        if (range == 0u)
        {
            return NextU32();
        }

        const uint32_t threshold = (0u - range) % range;
        uint32_t value = 0u;
        do
        {
            value = NextU32();
        } while (value < threshold);

        return minInclusive + (value % range);
    }

    inline constexpr int32_t Random::RangeI32(int32_t minInclusive, int32_t maxInclusive) noexcept
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

    inline constexpr uint64_t Random::splitMix64(uint64_t value) noexcept
    {
        value += 0x9E3779B97F4A7C15ULL;
        value = (value ^ (value >> 30u)) * 0xBF58476D1CE4E5B9ULL;
        value = (value ^ (value >> 27u)) * 0x94D049BB133111EBULL;
        return value ^ (value >> 31u);
    }
}   // namespace ludus::core
