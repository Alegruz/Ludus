#include <ludus/foundation/profiling/clock.hpp>

#include <emscripten.h>

namespace ludus::foundation::profiling
{

uint64 NowTicks() noexcept
{
    // performance.now() milliseconds expressed as nanoseconds. Browser privacy
    // settings may reduce precision; the unit does not promise ns resolution.
    return static_cast<uint64>(emscripten_get_now() * 1'000'000.0);
}

uint64 TicksPerNanosecondDenominator() noexcept
{
    return 1;
}

} // namespace ludus::foundation::profiling
