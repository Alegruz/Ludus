#include <ludus/foundation/time/time.hpp>

#include <emscripten.h>

// Thanks to the Emscripten authors, "emscripten.h", emscripten_get_now,
// https://emscripten.org/docs/api_reference/emscripten.h.html#c.emscripten_get_now.
// Preserve the existing pinned 4.0.23 clock/epoch; ns conversion cannot restore
// precision lost to browser clock policy. See the high-resolution-time review.

namespace ludus::foundation::time
{
uint64 NowTicks() noexcept
{
    // Pinned Emscripten uses performance.now() on the main browser thread.
    // Preserve its epoch for existing traces/breadcrumbs; privacy settings may
    // coarsen resolution. AudioWorklet and cross-worker domains are separate.
    const float64 nanoseconds = emscripten_get_now() * 1'000'000.0;
    // Float-to-integer conversion is only defined inside the destination range.
    // Guards also avoid undefined behavior if the host supplies NaN/infinity.
    if (!(nanoseconds >= 0.0 && nanoseconds < 18'446'744'073'709'551'616.0))
    {
        return 0;
    }
    return static_cast<uint64>(nanoseconds);
}
} // namespace ludus::foundation::time
