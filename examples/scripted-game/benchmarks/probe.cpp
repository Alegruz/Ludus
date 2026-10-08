// Measurement executable only: the actual FoundationMemory fallible boundary.
// This is linked into a separate counting executable, never the timed player,
// SDK or game module. No failure injection or extra allocation headers.
#include "probe.h"

#include <cstdlib>
#include <new>

namespace
{
ludus::s7::Allocations totals;
bool counting = false;
} // namespace
namespace ludus::s7
{
void BeginAllocations() noexcept
{
    totals = {};
    counting = true;
}
Allocations EndAllocations() noexcept
{
    counting = false;
    return totals;
}
} // namespace ludus::s7
void* operator new(ludus::foundation::usize bytes, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    if (counting)
    {
        ++totals.Requests;
        totals.RequestedBytes += bytes;
    }
    const auto align = static_cast<ludus::foundation::usize>(alignment);
    if (bytes == 0)
    {
        bytes = 1;
    }
    if (bytes > ~ludus::foundation::usize{0} - (align - 1))
    {
        return nullptr;
    }
    return std::aligned_alloc(align, (bytes + align - 1) & ~(align - 1));
}
void operator delete(void* block, std::align_val_t) noexcept
{
    std::free(block);
}
