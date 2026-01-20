#pragma once

#include <Ludus/Engine/Core/Math/Bit.h>

#include <Ludus/Engine/Core/Assert.h>

#include <bit>
#include <limits>
#include <type_traits>

namespace ludus::core
{
    template<std::integral T>
    LUDUS_INLINE constexpr T GetNextPowerOfTwo(const T value) noexcept
    {
        if (value <= 1)
        {
            return 1;
        }
        
        if constexpr (std::signed_integral<T>)
        { 
            LUDUS_ASSERT_MSG(value > 0, "GetNextPowerOfTwo expects a non-negative value.");
            if (value <= 0)
            {
                return 1;
            }
        }

        using UnsignedT = std::make_unsigned_t<T>;
        const auto unsignedValue = static_cast<UnsignedT>(value);
        const auto maxPowerOfTwo = std::bit_floor(std::numeric_limits<UnsignedT>::max());

        LUDUS_ASSERT_MSG(unsignedValue <= maxPowerOfTwo, "GetNextPowerOfTwo overflow.");
        if (unsignedValue > maxPowerOfTwo)
        { 
            return 0;
        }
        return static_cast<T>(std::bit_ceil(unsignedValue));
    }

    template<std::integral T>
    LUDUS_INLINE constexpr bool IsPowerOfTwo(const T value) noexcept
    {
        if constexpr (std::signed_integral<T>)
        {
            if (value <= 0)
            {
                return false;
            }
        }

        using UnsignedT = std::make_unsigned_t<T>;
        const auto unsignedValue = static_cast<UnsignedT>(value);
        return std::has_single_bit(unsignedValue);
    }
} // namespace ludus::core
