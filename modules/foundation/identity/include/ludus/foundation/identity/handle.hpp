#pragma once

// Thanks to Scott Bilas, "A Generic Handle-Based Resource Manager", Game
// Programming Gems 1, section 1.6, pp. 68-79, for typed indexed handles, and
// Jason Gregory, "Object References and World Queries", Game Engine Architecture,
// third edition, section 16.5.3, pp. 1082-1085, for incarnation validation.
// Design inspiration only: owner tokens and nonwrapping generations are Ludus
// choices; no sample code is copied. Review:
// docs/architecture/object-handles-uuid-reference-review.md.
#include <ludus/foundation/base/compiler.h>
#include <ludus/foundation/base/types.h>

namespace ludus::foundation::identity
{
/// @brief Nonowning reference to one runtime incarnation in one owning registry.
///
/// `Tag` is a compile-time domain marker: handles of different tags are distinct
/// types with no conversion, so an entity handle cannot be passed where a texture
/// handle is expected. The owner field distinguishes two registries of the same
/// domain at run time. Neither check replaces the other.
///
/// All-zero is the canonical null value. Owner zero or generation zero is never
/// issued; slot zero is a valid first slot. Equality compares the complete value,
/// including the owner. Neither equality nor IsNull() says anything about liveness:
/// only the issuing registry can validate a handle. A handle does not own its
/// object, pin its memory, or contain a pointer, UUID or manager lookup.
///
/// The value is trivially copyable, 16 bytes, and safe to copy across threads; the
/// registry that resolves it defines its own synchronization. Do not persist it:
/// owner tokens are process-scoped (see TryIssueLifetimeId()).
/// @tparam Tag Domain marker type; it is never instantiated.
template <typename Tag>
struct Handle final
{
    /// Lifetime token of the issuing registry; zero only in the null handle.
    uint64 Owner = 0;
    /// Registry slot index; zero is a valid slot.
    uint32 Slot = 0;
    /// Incarnation of the slot; zero only in the null handle.
    uint32 Generation = 0;

    /// Compare all fields exactly. Equal handles name the same incarnation, not a live one.
    [[nodiscard]] constexpr bool operator==(const Handle&) const noexcept = default;

    /// Report whether this is the canonical all-zero null value.
    /// A nonnull handle may still be malformed, stale or owned by another registry.
    [[nodiscard]] LUDUS_INLINE constexpr bool IsNull() const noexcept
    {
        return Owner == 0 && Slot == 0 && Generation == 0;
    }
};

/// First generation a registry assigns to a slot. Generation zero is never issued.
inline constexpr uint32 FIRST_GENERATION = 1;
/// Last usable generation of a slot. Releasing a slot at this generation retires it.
inline constexpr uint32 TERMINAL_GENERATION = ~uint32{0};

/// Result of advancing a released slot's generation.
enum class GenerationStatus : uint8
{
    /// The generation was incremented; the slot may be reused with the new value.
    Advanced,
    /// The slot was released at TERMINAL_GENERATION. It must be retired permanently
    /// for its owner; the generation is unchanged and never wraps.
    Retired,
    /// The input was zero, which no registry issues; the generation is unchanged.
    InvalidGeneration,
};

/// @brief Advance the generation of a slot that is being released.
///
/// Registries call this when a slot's current incarnation ends, before the slot
/// can be reused. A slot whose generation reaches TERMINAL_GENERATION is retired
/// instead of wrapping, so a stale handle can never name a later incarnation of
/// the same owner. Retired slots still count against the registry's capacity.
/// @param inoutGeneration The released incarnation's generation; incremented only
/// when the result is GenerationStatus::Advanced.
/// @return Advanced, Retired, or InvalidGeneration for a zero input.
[[nodiscard]] LUDUS_INLINE constexpr GenerationStatus AdvanceGeneration(uint32& inoutGeneration) noexcept
{
    if (inoutGeneration == 0)
    {
        return GenerationStatus::InvalidGeneration;
    }
    if (inoutGeneration == TERMINAL_GENERATION)
    {
        return GenerationStatus::Retired;
    }
    ++inoutGeneration;
    return GenerationStatus::Advanced;
}
} // namespace ludus::foundation::identity
