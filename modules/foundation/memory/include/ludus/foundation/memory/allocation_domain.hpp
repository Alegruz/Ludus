#pragma once

#include <ludus/foundation/base/types.h>

namespace ludus::foundation
{
/// @brief Stable, immutable identity for an allocation backend and its context.
/// @note The domain and context must outlive all allocations made through them.
/// Callbacks must be thread safe and non-reentrant into their caller. No global
/// allocation replacement or allocation tracking is implied.
class AllocationDomain final
{
public:
    /// Allocates bytes with the requested power-of-two alignment; returns nullptr on failure.
    using AllocateFunction = void* (*)(void*, usize, usize) noexcept;
    /// Frees storage using its original context, byte count and alignment.
    using FreeFunction = void (*)(void*, void*, usize, usize) noexcept;

    /// @brief Binds an immutable domain to caller-owned callback state.
    /// @param context Borrowed backend state; callbacks decide whether nullptr is valid.
    /// @param allocate Thread-safe callback that reports allocation failure with nullptr.
    /// @param free Thread-safe callback that releases allocations from this backend.
    /// @note Both callbacks are required for TryAllocate to accept a request.
    constexpr AllocationDomain(void* context, AllocateFunction allocate, FreeFunction free) noexcept
        : mContext(context), mAllocate(allocate), mFree(free)
    {
    }
    /// Domains cannot be copied; their identity must remain stable.
    AllocationDomain(const AllocationDomain&) = delete;
    /// Domains cannot be assigned; allocations retain the original identity.
    AllocationDomain& operator=(const AllocationDomain&) = delete;
    /// Domains cannot be moved while allocations may refer to their identity.
    AllocationDomain(AllocationDomain&&) = delete;
    /// Domains cannot be move-assigned.
    AllocationDomain& operator=(AllocationDomain&&) = delete;

    /// @brief Attempts an allocation through the bound backend.
    /// @param bytes Nonzero storage size in bytes.
    /// @param alignment Nonzero power-of-two storage alignment.
    /// @return Allocated storage, or nullptr for invalid input, missing callbacks,
    /// or backend allocation failure.
    /// @note The caller frees successful allocations through this same domain.
    [[nodiscard]] void* TryAllocate(usize bytes, usize alignment) const noexcept;
    /// @brief Releases an allocation through its original backend; nullptr is a no-op.
    /// @param pointer Storage previously allocated through this domain, or nullptr.
    /// @param bytes Original allocation size in bytes.
    /// @param alignment Original allocation alignment.
    /// @pre A non-null pointer requires the original live context and free callback.
    void Free(void* pointer, usize bytes, usize alignment) const noexcept;

private:
    void* mContext;
    AllocateFunction mAllocate;
    FreeFunction mFree;
};

/// @brief Returns the process-lifetime system allocation domain.
/// @return Stable system backend, available through static and thread-local teardown.
[[nodiscard]] const AllocationDomain& GetSystemAllocationDomain() noexcept;
} // namespace ludus::foundation
