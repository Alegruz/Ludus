#pragma once

#include <Ludus/Engine/Core/Math/Bit.h>

namespace ludus::core
{
    template<std::integral T>
    LUDUS_INLINE constexpr T GetNextPowerOfTwo(const T value) noexcept
    {
        if(value == 0)
        {
            return 1;
        }
        
        if(value == 1)
        {
            return 1;
        }

        T v = value - 1;
        
        // Set all bits after the highest set bit
        v |= v >> 1;
        v |= v >> 2;
        v |= v >> 4;
        v |= v >> 8;
        v |= v >> 16;
        
        if constexpr (sizeof(T) >= 8)
        {
            v |= v >> 32;
        }
        
        return v + 1;
    }

    template<std::integral T>
    LUDUS_INLINE constexpr bool IsPowerOfTwo(const T value) noexcept
    {
        return (value > 0) && ((value & (value - 1)) == 0);
    }
} // namespace ludus::core
