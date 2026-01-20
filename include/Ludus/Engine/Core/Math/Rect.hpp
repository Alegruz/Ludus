#pragma once

#include <Ludus/Engine/Core/Math/Rect.h>

namespace ludus::core
{
    template<Arithmetic T>
    LUDUS_INLINE constexpr Rect<T>::Rect() noexcept
        : X(0)
        , Y(0)
        , Width(0)
        , Height(0)
    {
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr Rect<T>::Rect(const T x, const T y, const T width, const T height) noexcept
        : X(x)
        , Y(y)
        , Width(width)
        , Height(height)
    {
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr T Rect<T>::GetLeft() const noexcept
    {
        return X;
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr T Rect<T>::GetRight() const noexcept
    {
        return X + Width;
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr T Rect<T>::GetTop() const noexcept
    {
        return Y;
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr T Rect<T>::GetBottom() const noexcept
    {
        return Y + Height;
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr bool Rect<T>::Contains(T px, T py) const noexcept
    {
        return px >= X && px < GetRight() && py >= Y && py < GetBottom();
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr bool Rect<T>::Intersects(const Rect& other) const noexcept
    {
        return GetLeft() < other.GetRight() &&
               GetRight() > other.GetLeft() &&
               GetTop() < other.GetBottom() &&
               GetBottom() > other.GetTop();
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr bool Rect<T>::operator==(const Rect& other) const noexcept
    {
        return X == other.X && Y == other.Y && Width == other.Width && Height == other.Height;
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr bool Rect<T>::operator!=(const Rect& other) const noexcept
    {
        return !(*this == other);
    }
} // namespace ludus::core
