#pragma once

#include <Ludus/Engine/Core/Common.h>

#include <concepts>

namespace ludus::core
{
    template<std::floating_point T>
    [[nodiscard]] constexpr T Clamp(const T value, const T minValue, const T maxValue) noexcept;

    template<std::floating_point T>
    [[nodiscard]] constexpr T Clamp01(const T value) noexcept;

    template<std::floating_point T>
    [[nodiscard]] constexpr T Lerp(const T from, const T to, const T t) noexcept;

    template<std::floating_point T>
    [[nodiscard]] constexpr T LerpClamped(const T from, const T to, const T t) noexcept;

    template<std::floating_point T>
    [[nodiscard]] constexpr T InverseLerp(const T from, const T to, const T value) noexcept;

    template<std::floating_point T>
    [[nodiscard]] constexpr T MoveTowards(const T current, const T target, const T maxDelta) noexcept;

    template<std::floating_point T>
    [[nodiscard]] constexpr T SmoothStep(const T edge0, const T edge1, const T value) noexcept;

    template<std::floating_point T>
    [[nodiscard]] constexpr T SmootherStep(const T edge0, const T edge1, const T value) noexcept;

    template<std::floating_point T>
    [[nodiscard]] T ExpDecay(const T current, const T target, const T deltaTime, const T halfLife) noexcept;

    template<std::floating_point T>
    [[nodiscard]] T ExpDecayRate(const T current, const T target, const T deltaTime, const T rate) noexcept;

    template<std::integral T>
    [[nodiscard]] constexpr T EaseOutShift(const T current, const T target, const int shift) noexcept;
} // namespace ludus::core
