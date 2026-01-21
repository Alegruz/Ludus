#pragma once

#include <concepts>

namespace ludus::core
{
    template<std::integral T>
    constexpr T GetNextPowerOfTwo(T value) noexcept;
    
    template<std::integral T>
    constexpr bool IsPowerOfTwo(T value) noexcept;
} // namespace ludus::core
