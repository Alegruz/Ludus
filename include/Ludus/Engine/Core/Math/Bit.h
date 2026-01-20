#pragma once

#include <concepts>

namespace ludus::core
{
    template<std::integral T>
    constexpr T GetNextPowerOfTwo(const T value) noexcept;
    
    template<std::integral T>
    constexpr bool IsPowerOfTwo(const T value) noexcept;
} // namespace ludus::core
