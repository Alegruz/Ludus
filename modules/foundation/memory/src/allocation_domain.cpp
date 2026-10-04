#include <ludus/foundation/memory/allocation_domain.hpp>

#include <new>

namespace ludus::foundation
{
namespace
{
void* SystemAllocate(void*, usize bytes, usize alignment) noexcept
{
    return ::operator new(bytes, std::align_val_t{alignment}, std::nothrow);
}
void SystemFree(void*, void* pointer, usize, usize alignment) noexcept
{
    ::operator delete(pointer, std::align_val_t{alignment});
}
constexpr AllocationDomain kSystemDomain{nullptr, SystemAllocate, SystemFree};
} // namespace

void* AllocationDomain::TryAllocate(usize bytes, usize alignment) const noexcept
{
    if (bytes == 0 || alignment == 0 || (alignment & (alignment - 1)) != 0 || mAllocate == nullptr || mFree == nullptr)
    {
        return nullptr;
    }
    return mAllocate(mContext, bytes, alignment);
}

void AllocationDomain::Free(void* pointer, usize bytes, usize alignment) const noexcept
{
    if (pointer != nullptr)
    {
        mFree(mContext, pointer, bytes, alignment);
    }
}

const AllocationDomain& GetSystemAllocationDomain() noexcept
{
    return kSystemDomain;
}
} // namespace ludus::foundation
