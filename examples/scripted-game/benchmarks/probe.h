#pragma once

#include <ludus/foundation/base/types.h>

namespace ludus::s7
{
struct Allocations
{
    foundation::uint64 Requests = 0;
    foundation::uint64 RequestedBytes = 0;
};
#if defined(LUDUS_S7_ALLOCATION_PROBE)
void BeginAllocations() noexcept;
[[nodiscard]] Allocations EndAllocations() noexcept;
#else
inline void BeginAllocations() noexcept {}
[[nodiscard]] inline Allocations EndAllocations() noexcept
{
    return {};
}
#endif
} // namespace ludus::s7
