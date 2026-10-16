#pragma once

#include <ludus/foundation/base/types.h>

namespace ludus::foundation::identity
{
/// Result of issuing a registry lifetime token.
enum class LifetimeIdStatus : uint8
{
    /// A fresh token was written to the output.
    Success,
    /// The source reached its terminal value; it never wraps. The output is unchanged.
    IdentityExhausted,
};

/// Largest token the process source can issue. After it, issuance reports IdentityExhausted.
inline constexpr uint64 LAST_LIFETIME_ID = ~uint64{0} - 1;

/// @brief Issue a fresh nonzero owner token for one new registry lifetime.
///
/// Tokens are process-unique and monotonically increasing from 1. A registry
/// obtains one token when it is initialized and stores it as the Owner of every
/// handle it issues; clearing and rebuilding a registry obtains a new token rather
/// than resetting generations under the old one. Tokens identify a lifetime, not
/// an address, thread, name or content revision, and are never serialized.
///
/// Thread-safe and lock-free; uses a relaxed atomic counter, so a token establishes
/// uniqueness only. Publishing the registry that owns it needs separate
/// synchronization. No allocation.
/// @param outLifetimeId Receives the token on success; unchanged on exhaustion.
/// @return Success, or IdentityExhausted once LAST_LIFETIME_ID has been issued.
/// @warning The counter lives in this static library. Each executable or shared
/// module that links it privately receives its own counter, which is not
/// process-wide issuance. Reloadable gameplay modules must obtain tokens from the
/// host rather than calling this function from their own copy.
[[nodiscard]] LifetimeIdStatus TryIssueLifetimeId(uint64& outLifetimeId) noexcept;
} // namespace ludus::foundation::identity
