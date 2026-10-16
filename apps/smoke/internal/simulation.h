#pragma once

#include <ludus/foundation/base/types.h>

#include <ludus/foundation/time/timers.hpp>

#include <ludus/platform/browser/window.h>

namespace ludus::smoke
{
struct Simulation final
{
    ludus::foundation::float64 X = 0;
    ludus::foundation::float64 Y = 0;
    ludus::foundation::float64 Phase = 0;
};
// Largest accepted animation interval; longer callbacks report the excess as discarded.
inline constexpr foundation::time::Duration MAXIMUM_FRAME_DELTA{foundation::time::NANOSECONDS_PER_SECOND / 10};
// Sample the one callback timestamp. Hidden frames drop the baseline, so the first
// visible callback is a zero-elapsed baseline and hidden time is never replayed.
[[nodiscard]] foundation::time::FrameSample
SampleFrame(foundation::time::FrameClock& inoutClock, foundation::time::Timestamp now, bool isVisible) noexcept;
// Visible accepted time only; the FrameClock is the sole clamp. Input snapshots are authoritative.
void Advance(Simulation& inoutSimulation,
             const platform::browser::WindowState& input,
             const foundation::time::FrameSample& frame) noexcept;
} // namespace ludus::smoke
