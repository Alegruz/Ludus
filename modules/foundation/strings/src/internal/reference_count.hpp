#pragma once

#include <ludus/foundation/base/types.h>

#include <atomic>

namespace ludus::foundation::detail
{
// A saturated count pins storage forever instead of wrapping into premature
// destruction. Reduced-width instantiation is an internal test seam only.
template <typename Count>
[[nodiscard]] bool RetainReference(std::atomic<Count>& references) noexcept
{
    constexpr Count kPinned = static_cast<Count>(-1);
    Count old = references.load(std::memory_order_relaxed);
    while (old != kPinned)
    {
        const Count next = static_cast<Count>(old + 1);
        if (references.compare_exchange_weak(old, next, std::memory_order_relaxed))
        {
            return next == kPinned;
        }
    }
    return false;
}
template <typename Count>
[[nodiscard]] bool ReleaseReference(std::atomic<Count>& references) noexcept
{
    constexpr Count kPinned = static_cast<Count>(-1);
    Count old = references.load(std::memory_order_relaxed);
    while (old != kPinned)
    {
        // CAS, rather than fetch_sub, cannot decrement a concurrently pinned
        // count after a retain wins the race at the saturation boundary.
        if (references.compare_exchange_weak(old, static_cast<Count>(old - 1), std::memory_order_release))
        {
            if (old == 1)
            {
                std::atomic_thread_fence(std::memory_order_acquire);
                return true;
            }
            return false;
        }
    }
    return false;
}
} // namespace ludus::foundation::detail
