#pragma once

#include <Ludus/Engine/Core/Common.h>

#include <cstddef>
#include <cstdint>

namespace ludus::core
{
    class Random final
    {
    public:
        using result_type = uint32_t;

        static Random Create() noexcept;
        static constexpr Random Create(uint64_t seed) noexcept;

        explicit constexpr Random() = default;

        constexpr void Reseed(uint64_t seed) noexcept;
        constexpr void Reseed(uint64_t seed, uint64_t stream) noexcept;

        [[nodiscard]] constexpr uint32_t NextU32() noexcept;
        [[nodiscard]] constexpr uint64_t NextU64() noexcept;
        [[nodiscard]] constexpr float NextFloat01() noexcept;
        [[nodiscard]] constexpr double NextDouble01() noexcept;

        [[nodiscard]] constexpr uint32_t RangeU32(uint32_t minInclusive, uint32_t maxInclusive) noexcept;
        [[nodiscard]] constexpr int32_t RangeI32(int32_t minInclusive, int32_t maxInclusive) noexcept;

    private:
        static uint64_t generateSeed() noexcept;
        static constexpr uint64_t splitMix64(uint64_t value) noexcept;

        uint64_t mState = 0u;
        uint64_t mInc = 0u;
    };
}   // namespace ludus::core
