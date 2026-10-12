#pragma once

#include <ludus/foundation/base/types.h>

namespace ludus::resources::internal
{
// Test backend only; owner-serialized. -1 disables, 0 fails next candidate array,
// positive values skip that many candidate arrays before failing exactly once.
void FailMeshArrayAllocationAfter(ludus::foundation::int32 count) noexcept;
} // namespace ludus::resources::internal
