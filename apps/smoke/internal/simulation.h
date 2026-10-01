#pragma once

#include <ludus/foundation/base/types.h>

#include <ludus/platform/browser/window.h>

namespace ludus::smoke
{
struct Simulation final
{
    ludus::foundation::float64 X = 0;
    ludus::foundation::float64 Y = 0;
    ludus::foundation::float64 Phase = 0;
};
// Visible time only, at most 100ms per tick. Input snapshots are authoritative.
void Advance(Simulation&, const platform::browser::WindowState&, ludus::foundation::float64 delta) noexcept;
} // namespace ludus::smoke
