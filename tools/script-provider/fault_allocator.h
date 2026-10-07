#pragma once

#include <ludus/foundation/base/types.h>

namespace ludus::s6
{
// Acceptance executable only. Reset counts, then reject this aligned allocation
// and all later ones; zero counts without rejecting. Never linked into the SDK.
void FailAllocationsFrom(foundation::usize attempt) noexcept;
void StopAllocationFailure() noexcept;
[[nodiscard]] foundation::usize AllocationAttempts() noexcept;
} // namespace ludus::s6
