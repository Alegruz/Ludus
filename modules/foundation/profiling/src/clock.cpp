#include <ludus/foundation/profiling/clock.hpp>

#include <ludus/foundation/time/time.hpp>

namespace ludus::foundation::profiling
{
uint64 NowTicks() noexcept
{
    return time::NowTicks();
}

uint64 TicksPerNanosecondDenominator() noexcept
{
    // Compatibility API: exported timestamps remain integer nanoseconds.
    return 1;
}
} // namespace ludus::foundation::profiling
