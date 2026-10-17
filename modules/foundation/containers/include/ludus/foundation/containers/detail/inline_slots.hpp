#pragma once

// Uninitialized inline element storage shared by the bounded Ludus containers
// (FixedArray, RingBuffer). Constructing the union constructs no element: the
// owning container begins and ends every element's lifetime explicitly through
// detail/contiguous_storage.hpp. Like contiguous_storage.hpp, this header ships
// with the SDK only because container templates instantiate it; it is not part
// of the supported API.

#include <ludus/foundation/base/core.h>

#include <type_traits>

namespace ludus::foundation::core::detail
{
/// @brief Internal: `Count` uninitialized, correctly aligned element slots.
///
/// Trivially destructible exactly when ElementType is. Not copyable; owners copy live elements individually.
template <typename ElementType, usize Count>
union InlineSlots
{
    /// Internal: leaves every slot uninitialized.
    InlineSlots() noexcept {}
    /// Internal: trivial destruction for trivially destructible elements.
    ~InlineSlots()
        requires std::is_trivially_destructible_v<ElementType>
    = default;
    /// Internal: destroys no element; the owner destroys its live elements first.
    ~InlineSlots() {}
    InlineSlots(const InlineSlots&) = delete;
    InlineSlots& operator=(const InlineSlots&) = delete;

    /// Internal: raw slots; only the owner's live elements hold objects.
    ElementType Elements[Count];
};

/// Internal: zero-capacity storage holds no slots.
template <typename ElementType>
union InlineSlots<ElementType, 0>
{
};
} // namespace ludus::foundation::core::detail
