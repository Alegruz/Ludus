#pragma once

#include <Ludus/Engine/Core/Math/Interpolation.h>

#include <cmath>
#include <limits>

namespace ludus::core
{
    template<std::floating_point T>
    LUDUS_INLINE constexpr T Clamp(const T value, const T minValue, const T maxValue) noexcept
    {
        if (minValue > maxValue)
        {
            return value;
        }

        return (value < minValue) ? minValue : (value > maxValue) ? maxValue : value;
    }

    template<std::floating_point T>
    LUDUS_INLINE constexpr T Clamp01(const T value) noexcept
    {
        return Clamp(value, static_cast<T>(0), static_cast<T>(1));
    }

    template<std::floating_point T>
    LUDUS_INLINE constexpr T Lerp(const T from, const T to, const T t) noexcept
    {
        return from + (to - from) * t;
    }

    template<std::floating_point T>
    LUDUS_INLINE constexpr T LerpClamped(const T from, const T to, const T t) noexcept
    {
        return Lerp(from, to, Clamp01(t));
    }

    template<std::floating_point T>
    LUDUS_INLINE constexpr T InverseLerp(const T from, const T to, const T value) noexcept
    {
        const T denom = (to - from);
        if (std::abs(denom) <= std::numeric_limits<T>::epsilon())
        {
            return static_cast<T>(0);
        }
        return (value - from) / denom;
    }

    template<std::floating_point T>
    LUDUS_INLINE constexpr T MoveTowards(const T current, const T target, const T maxDelta) noexcept
    {
        const T delta = target - current;
        if (std::abs(delta) <= maxDelta)
        {
            return target;
        }
        return current + (delta > static_cast<T>(0) ? maxDelta : -maxDelta);
    }

    template<std::floating_point T>
    LUDUS_INLINE constexpr T SmoothStep(const T edge0, const T edge1, const T value) noexcept
    {
        const T t = Clamp01(InverseLerp(edge0, edge1, value));
        return t * t * (static_cast<T>(3) - static_cast<T>(2) * t);
    }

    template<std::floating_point T>
    LUDUS_INLINE constexpr T SmootherStep(const T edge0, const T edge1, const T value) noexcept
    {
        const T t = Clamp01(InverseLerp(edge0, edge1, value));
        return t * t * t * (t * (t * static_cast<T>(6) - static_cast<T>(15)) + static_cast<T>(10));
    }

    template<std::floating_point T>
    LUDUS_INLINE T ExpDecay(const T current, const T target, const T deltaTime, const T halfLife) noexcept
    {
        if (halfLife <= static_cast<T>(0))
        {
            return target;
        }

        if (deltaTime <= static_cast<T>(0))
        {
            return current;
        }

        const T lambda = std::log(static_cast<T>(2)) / halfLife;
        const T decay = std::exp(-lambda * deltaTime);
        return target + (current - target) * decay;
    }

    template<std::floating_point T>
    LUDUS_INLINE T ExpDecayRate(const T current, const T target, const T deltaTime, const T rate) noexcept
    {
        if (rate <= static_cast<T>(0))
        {
            return target;
        }

        if (deltaTime <= static_cast<T>(0))
        {
            return current;
        }

        const T decay = std::exp(-rate * deltaTime);
        return target + (current - target) * decay;
    }

    template<std::integral T>
    LUDUS_INLINE constexpr T EaseOutShift(const T current, const T target, const int shift) noexcept
    {
        if (shift <= 0)
        {
            return target;
        }

        if (shift >= static_cast<int>(sizeof(T) * 8))
        {
            return target;
        }

        const auto factor = (static_cast<T>(1) << shift) - static_cast<T>(1);
        return static_cast<T>((current * factor + target) >> shift);
    }
} // namespace ludus::core
