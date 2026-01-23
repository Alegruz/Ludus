#pragma once

#include <Ludus/Engine/Core/Math/Vector.h>

#include <concepts>

namespace ludus::core
{
    struct IntegratorState1D final
    {
        float Position;
        float Velocity;
    };

    template<Arithmetic T>
    struct IntegratorState final
    {
        Vector3<T> Position;
        Vector3<T> Velocity;
    };

    struct LinearSpring1D final
    {
        float AngularFrequency;
        float Damping;
    };

    template<Arithmetic T>
    struct LinearSpring final
    {
        T AngularFrequency;
        T Damping;
    };

    [[nodiscard]] IntegratorState1D IntegrateImplicitEuler(
        const IntegratorState1D& state,
        float angularFrequency,
        float damping,
        float deltaTime) noexcept;

    template<Arithmetic T>
    [[nodiscard]] IntegratorState<T> IntegrateImplicitEuler(
        const IntegratorState<T>& state,
        T angularFrequency,
        T damping,
        T deltaTime) noexcept;

    [[nodiscard]] IntegratorState1D IntegrateImplicitEuler(
        const IntegratorState1D& state,
        const LinearSpring1D& spring,
        float deltaTime) noexcept;

    template<Arithmetic T>
    [[nodiscard]] IntegratorState<T> IntegrateImplicitEuler(
        const IntegratorState<T>& state,
        const LinearSpring<T>& spring,
        T deltaTime) noexcept;
} // namespace ludus::core
