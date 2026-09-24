#include <ludus/foundation/profiling/clock.hpp>

#include <chrono>

namespace ludus::foundation::profiling
{

uint64 NowTicks() noexcept
{
    // steady_clock is monotonic by standard guarantee. We store integer
    // nanoseconds so durations are exact subtractions downstream (§5, C7).
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

uint64 TicksPerNanosecondDenominator() noexcept
{
    // steady_clock backend: ticks are already nanoseconds.
    return 1;
}

} // namespace ludus::foundation::profiling
