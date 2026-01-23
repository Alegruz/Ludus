#pragma once

#include <Ludus/Engine/Core/Math/Trigonometry.h>

#include <cmath>
#include <numbers>

namespace ludus::core
{
    template<std::floating_point T>
    LUDUS_INLINE constexpr T Pi() noexcept
    {
        return std::numbers::pi_v<T>;
    }

    template<std::floating_point T>
    LUDUS_INLINE constexpr T TwoPi() noexcept
    {
        return static_cast<T>(2) * Pi<T>();
    }

    template<std::floating_point T>
    LUDUS_INLINE constexpr T HalfPi() noexcept
    {
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers) - 0.5 represents π/2 (well-known mathematical constant)
        return static_cast<T>(0.5) * Pi<T>();
    }

    template<std::floating_point T>
    LUDUS_INLINE T WrapRadiansPi(T radians) noexcept
    {
        return std::remainder(radians, TwoPi<T>());
    }

    template<std::floating_point T>
    LUDUS_INLINE T WrapRadiansTwoPi(T radians) noexcept
    {
        const T twoPi = TwoPi<T>();
        T result = std::fmod(radians, twoPi);
        if (result < static_cast<T>(0))
        {
            result += twoPi;
        }
        return result;
    }

    template<std::floating_point T>
    LUDUS_INLINE T Sin(T radians) noexcept
    {
        return std::sin(radians);
    }

    template<std::floating_point T>
    LUDUS_INLINE T Cos(T radians) noexcept
    {
        return std::cos(radians);
    }

    template<std::floating_point T>
    LUDUS_INLINE SinCosPair<T> SinCos(T radians) noexcept
    {
        return {std::sin(radians), std::cos(radians)};
    }

    template<std::floating_point T>
    LUDUS_INLINE T FastSin(T radians) noexcept
    {
        return FastSinCos(radians).Sin;
    }

    template<std::floating_point T>
    LUDUS_INLINE T FastCos(T radians) noexcept
    {
        return FastSinCos(radians).Cos;
    }

    template<std::floating_point T>
    LUDUS_INLINE SinCosPair<T> FastSinCos(T radians) noexcept
    {
        T x = WrapRadiansPi(radians);
        T cosSign = static_cast<T>(1);

        if (x > HalfPi<T>())
        {
            x = Pi<T>() - x;
            cosSign = static_cast<T>(-1);
        }
        else if (x < -HalfPi<T>())
        {
            x = -Pi<T>() - x;
            cosSign = static_cast<T>(-1);
        }

        const T x2 = x * x;
        const T sinPoly = (((((static_cast<T>(1.0 / 362880.0) * x2 + static_cast<T>(-1.0 / 5040.0)) * x2
                              + static_cast<T>(1.0 / 120.0))
                             * x2
                             + static_cast<T>(-1.0 / 6.0))
                            * x2
                            + static_cast<T>(1))
                           * x);
        const T cosPoly = ((((static_cast<T>(1.0 / 40320.0) * x2 + static_cast<T>(-1.0 / 720.0)) * x2
                             + static_cast<T>(1.0 / 24.0))
                            * x2
                            + static_cast<T>(-1.0 / 2.0))
                           * x2
                           + static_cast<T>(1));

        return {sinPoly, cosPoly * cosSign};
    }
} // namespace ludus::core
