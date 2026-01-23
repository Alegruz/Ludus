#pragma once

#include <Ludus/Engine/Core/Math/Integration.h>

namespace ludus::core
{
    inline IntegratorState1D IntegrateImplicitEuler(
        const IntegratorState1D& state,
        float angularFrequency,
        float damping,
        float deltaTime) noexcept
    {
        const float step = deltaTime;
        const float omega = angularFrequency;
        const float lambda = damping;
        const float omegaStep = omega * step;
        const float denom = 1.0f + lambda * step + (omegaStep * omegaStep);

        IntegratorState1D result = state;
        result.Velocity = (state.Velocity - (omega * omega) * state.Position * step) / denom;
        result.Position = state.Position + result.Velocity * step;
        return result;
    }

    template<Arithmetic T>
    [[nodiscard]] IntegratorState<T> IntegrateImplicitEuler(
        const IntegratorState<T>& state,
        T angularFrequency,
        T damping,
        T deltaTime) noexcept
    {
        const T step = deltaTime;
        const T omega = angularFrequency;
        const T lambda = damping;
        const T omegaStep = omega * step;
        const T denom = static_cast<T>(1) + lambda * step + (omegaStep * omegaStep);

        IntegratorState<T> result = state;
        result.Velocity = (state.Velocity - (omega * omega) * state.Position * step) / denom;
        result.Position = state.Position + result.Velocity * step;
        return result;
    }

    inline IntegratorState1D IntegrateImplicitEuler(
        const IntegratorState1D& state,
        const LinearSpring1D& spring,
        float deltaTime) noexcept
    {
        return IntegrateImplicitEuler(state, spring.AngularFrequency, spring.Damping, deltaTime);
    }

    template<Arithmetic T>
    [[nodiscard]] IntegratorState<T> IntegrateImplicitEuler(
        const IntegratorState<T>& state,
        const LinearSpring<T>& spring,
        T deltaTime) noexcept
    {
        return IntegrateImplicitEuler(state, spring.AngularFrequency, spring.Damping, deltaTime);
    }
} // namespace ludus::core
