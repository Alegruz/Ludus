#pragma once

#include <Ludus/Engine/Core/Common.h>

#include <concepts>

namespace ludus::core
{
    template<std::floating_point T>
    struct SinCosPair final
    {
        T Sin;
        T Cos;
    };

    template<std::floating_point T>
    [[nodiscard]] constexpr T Pi() noexcept;

    template<std::floating_point T>
    [[nodiscard]] constexpr T TwoPi() noexcept;

    template<std::floating_point T>
    [[nodiscard]] constexpr T HalfPi() noexcept;

    template<std::floating_point T>
    [[nodiscard]] T WrapRadiansPi(T radians) noexcept;

    template<std::floating_point T>
    [[nodiscard]] T WrapRadiansTwoPi(T radians) noexcept;

    template<std::floating_point T>
    [[nodiscard]] T Sin(T radians) noexcept;

    template<std::floating_point T>
    [[nodiscard]] T Cos(T radians) noexcept;

    template<std::floating_point T>
    [[nodiscard]] SinCosPair<T> SinCos(T radians) noexcept;

    template<std::floating_point T>
    [[nodiscard]] T FastSin(T radians) noexcept;

    template<std::floating_point T>
    [[nodiscard]] T FastCos(T radians) noexcept;

    template<std::floating_point T>
    [[nodiscard]] SinCosPair<T> FastSinCos(T radians) noexcept;
} // namespace ludus::core
