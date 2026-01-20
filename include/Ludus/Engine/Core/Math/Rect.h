#pragma once

#include <Ludus/Engine/Core/Common.h>

#include <concepts>
#include <cstdint>

namespace ludus::core
{    
    template<Arithmetic T>
    struct Rect final
    {
        T X;
        T Y;
        T Width;
        T Height;

        constexpr Rect() noexcept;
        constexpr Rect(const T x, const T y, const T width, const T height) noexcept;

        [[nodiscard]] constexpr T GetLeft() const noexcept;
        [[nodiscard]] constexpr T GetRight() const noexcept;
        [[nodiscard]] constexpr T GetTop() const noexcept;
        [[nodiscard]] constexpr T GetBottom() const noexcept;

        [[nodiscard]] constexpr bool Contains(T px, T py) const noexcept;
        [[nodiscard]] constexpr bool Intersects(const Rect& other) const noexcept;
        
        constexpr bool operator==(const Rect& other) const noexcept;
        constexpr bool operator!=(const Rect& other) const noexcept;
    };

    // Common type aliases
    using RectI = Rect<int32_t>;
    using RectU = Rect<uint32_t>;
    using RectF = Rect<float>;
} // namespace ludus::core
