#pragma once

// Large-coordinate helper (design §10). Subtract a double origin from a double
// world position IN DOUBLE, then narrow to a local float Vector3 with an
// explicit per-component range. This preserves local detail already present in
// the doubles that direct float positions near, e.g., 1e9 m would lose. It
// cannot recover detail lost before the call, nor support arbitrarily large
// double worlds at fixed millimetre accuracy.
//
// FoundationMath owns no global origin, cell scheme or rebase event. The
// render/simulation owner keeps related transforms in one space and decides when
// to change origins; for multiple cameras, construct independent relative values.

#include <ludus/foundation/base/core.h>

#include <ludus/foundation/math/status.hpp>
#include <ludus/foundation/math/vector.hpp>

namespace ludus::foundation::math
{
using core::float32;
using core::float64;

// TryMakeRelative: out = narrow(position - origin). Requires finite inputs and a
// positive finite maxAbsComponent (local distance units). Each component of the
// double difference must be finite and within [-maxAbsComponent, maxAbsComponent]
// before casting; the narrowed result is re-checked for finiteness. There is no
// default range on purpose. Output unchanged on failure.
[[nodiscard]] MathStatus
TryMakeRelative(Vector3d position, Vector3d origin, float64 maxAbsComponent, Vector3& out) noexcept;
} // namespace ludus::foundation::math
