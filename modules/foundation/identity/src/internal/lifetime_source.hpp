#pragma once

// Private issuance step shared by the process lifetime source and its tests. Tests
// drive a local counter near the terminal value through this seam; it is not
// installed and production callers use TryIssueLifetimeId().
#include <ludus/foundation/base/types.h>

#include <ludus/foundation/identity/lifetime.hpp>

#include <atomic>

namespace ludus::foundation::identity::internal
{
/// Issue the counter's current value and advance it, unless LAST_LIFETIME_ID was already issued.
/// @param inoutNext Next token to issue; starts at 1. Relaxed ordering: uniqueness only.
/// @param outLifetimeId Receives the token on Success; unchanged on exhaustion.
[[nodiscard]] inline LifetimeIdStatus TryIssueLifetimeIdFrom(std::atomic<uint64>& inoutNext,
                                                             uint64& outLifetimeId) noexcept
{
    uint64 candidate = inoutNext.load(std::memory_order_relaxed);
    do
    {
        if (candidate == 0 || candidate > LAST_LIFETIME_ID)
        {
            return LifetimeIdStatus::IdentityExhausted;
        }
    } while (inoutNext.compare_exchange_weak(candidate, candidate + 1, std::memory_order_relaxed) == false);
    outLifetimeId = candidate;
    return LifetimeIdStatus::Success;
}
} // namespace ludus::foundation::identity::internal
