#include <Ludus/Engine/Core/Random.hpp>

#include <chrono>
#include <functional>
#include <random>
#include <thread>

namespace ludus::core
{
    Random Random::Create() noexcept
    {
        Random rng;
        rng.Reseed(generateSeed());
        return rng;
    }

    uint64_t Random::generateSeed() noexcept
    {
        uint64_t seed = 0U;

        const auto nowHigh = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        const auto nowSteady = std::chrono::steady_clock::now().time_since_epoch().count();
        seed ^= splitMix64(static_cast<uint64_t>(nowHigh));
        seed ^= splitMix64(static_cast<uint64_t>(nowSteady));

        const auto threadHash = std::hash<std::thread::id>{}(std::this_thread::get_id());
        seed ^= splitMix64(static_cast<uint64_t>(threadHash));

        seed ^= splitMix64(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&seed)));

        std::random_device rd;
        if (rd.entropy() > 0.0)
        {
            const uint64_t rdValue = (static_cast<uint64_t>(rd()) << 32U) ^ static_cast<uint64_t>(rd());
            seed ^= splitMix64(rdValue);
        }
        else
        {
            const uint64_t rdValue = (static_cast<uint64_t>(rd()) << 32U) ^ static_cast<uint64_t>(rd());
            seed ^= splitMix64(rdValue ^ static_cast<uint64_t>(nowHigh));
        }

        return seed;
    }
}   // namespace ludus::core
