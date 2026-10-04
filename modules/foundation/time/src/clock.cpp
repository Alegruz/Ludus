#include <ludus/foundation/time/time.hpp>

#include <ludus/foundation/base/checked_integer.hpp>

#include <chrono>

// Thanks to the ISO C++ committee (WG21), "Class steady_clock", [time.clock.steady],
// https://eel.is/c++draft/time.clock.steady: the steady/non-decreasing contract
// supports interval timing. Preserve Ludus's existing backend and epoch.

namespace ludus::foundation::time
{
uint64 NowTicks() noexcept
{
    static_assert(std::chrono::steady_clock::is_steady);
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    // A pre-zero epoch maps to zero instead of wrapping to a large unsigned
    // timestamp. The supported Linux clock has a non-negative epoch.
    uint64 ticks = 0;
    (void)core::TryIntegerCast(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count(), ticks);
    return ticks;
}
} // namespace ludus::foundation::time
