#pragma once

#include <ludus/foundation/base/types.h>

namespace ludus::foundation
{
// Stable, immutable allocator identity. The domain and context must outlive all
// allocations. Callbacks are thread safe, non-reentrant into their caller, and
// return nullptr on allocation failure. Free receives the original byte count
// and power-of-two alignment. No global new replacement or tracking is implied.
class AllocationDomain final
{
public:
    using AllocateFunction = void* (*)(void*, usize, usize) noexcept;
    using FreeFunction = void (*)(void*, void*, usize, usize) noexcept;

    constexpr AllocationDomain(void* context, AllocateFunction allocate, FreeFunction free) noexcept
        : mContext(context), mAllocate(allocate), mFree(free)
    {
    }
    AllocationDomain(const AllocationDomain&) = delete;
    AllocationDomain& operator=(const AllocationDomain&) = delete;
    AllocationDomain(AllocationDomain&&) = delete;
    AllocationDomain& operator=(AllocationDomain&&) = delete;

    [[nodiscard]] void* TryAllocate(usize bytes, usize alignment) const noexcept;
    void Free(void* pointer, usize bytes, usize alignment) const noexcept;

private:
    void* mContext;
    AllocateFunction mAllocate;
    FreeFunction mFree;
};

// Process-lifetime system backend, including static/TLS teardown.
[[nodiscard]] const AllocationDomain& GetSystemAllocationDomain() noexcept;
} // namespace ludus::foundation
