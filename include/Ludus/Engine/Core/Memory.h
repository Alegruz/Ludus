#pragma once

//
// Ludus Memory Allocation Interface
//
// All memory allocations in the engine go through this interface, which uses
// mimalloc as the underlying allocator. This allows for:
//
// - Centralized memory tracking and profiling
// - Easy allocator swapping (mimalloc <-> jemalloc <-> system malloc)
// - Memory pooling and custom heap management per subsystem
// - Debug instrumentation and leak detection
// - Statistics gathering across all allocations
//

#include <Ludus/Engine/Core/Common.h>

#if defined(LUDUS_WINDOWS)
    #include <cstdlib>  // For size_t
#else
    #include <cstdlib>
#endif

namespace ludus::memory
{
    /// @brief Allocate memory for a single object of type T
    /// @tparam T Type to allocate
    /// @param count Number of objects to allocate (default 1)
    /// @return Pointer to allocated memory, or nullptr on failure
    template<typename T>
    [[nodiscard]] LUDUS_INLINE T* Allocate(size_t count = 1) noexcept
    {
        return static_cast<T*>(malloc(count * sizeof(T)));
    }

    /// @brief Deallocate memory previously allocated with Allocate<T>
    /// @tparam T Type that was allocated
    /// @param ptr Pointer to deallocate (can be nullptr, which is safe)
    template<typename T>
    LUDUS_INLINE void Deallocate(T* ptr) noexcept
    {
        free(ptr);
    }

    /// @brief Reallocate memory to a new size
    /// @tparam T Type being reallocated
    /// @param ptr Existing pointer (can be nullptr)
    /// @param newCount New number of objects to allocate
    /// @return Pointer to reallocated memory, or nullptr on failure
    template<typename T>
    [[nodiscard]] LUDUS_INLINE T* Reallocate(T* ptr, size_t newCount) noexcept
    {
        return static_cast<T*>(realloc(ptr, newCount * sizeof(T)));
    }

#undef CopyMemory
    /// @brief Copy memory from source to destination
    /// Replaces std::memcpy for explicit memory management
    /// @param dest Destination pointer
    /// @param src Source pointer
    /// @param size Number of bytes to copy
    /// @return dest pointer
    LUDUS_INLINE void* CopyMemory(void* dest, const void* src, size_t size) noexcept
    {
        return memcpy(dest, src, size);
    }

    /// @brief Set memory to a value
    /// Replaces std::memset for explicit memory management
    /// @param ptr Pointer to memory
    /// @param value Value to set (0-255)
    /// @param size Number of bytes to set
    /// @return ptr pointer
    LUDUS_INLINE void* SetMemory(void* ptr, int value, size_t size) noexcept
    {
        return memset(ptr, value, size);
    }

#undef MoveMemory
    /// @brief Move memory (handles overlapping regions)
    /// Replaces std::memmove for explicit memory management
    /// @param dest Destination pointer
    /// @param src Source pointer
    /// @param size Number of bytes to move
    /// @return dest pointer
    LUDUS_INLINE void* MoveMemory(void* dest, const void* src, size_t size) noexcept
    {
        return memmove(dest, src, size);
    }

    /// @brief Compare memory regions
    /// @param ptr1 First pointer
    /// @param ptr2 Second pointer
    /// @param size Number of bytes to compare
    /// @return 0 if equal, <0 if ptr1 < ptr2, >0 if ptr1 > ptr2
    LUDUS_INLINE int CompareMemory(const void* ptr1, const void* ptr2, size_t size) noexcept
    {
        return memcmp(ptr1, ptr2, size);
    }

    /// @brief Zero out memory (secure erase)
    /// @param ptr Pointer to memory
    /// @param size Number of bytes to zero
    LUDUS_INLINE void ZeroOutMemory(void* ptr, size_t size) noexcept
    {
        memset(ptr, 0, size);
    }

#if defined(LUDUS_DEBUG) && defined(LUDUS_WINDOWS)
    /// @brief Get allocation statistics (Windows Debug only)
    /// Uses CRT heap debugging
    struct AllocationStats
    {
        size_t totalAllocated;
        size_t totalFreed;
        size_t currentBlocks;
    };

    /// @brief Query current allocation statistics
    /// @return Allocation statistics (Windows Debug builds only)
    AllocationStats GetAllocationStats() noexcept;
#endif

}   // namespace ludus::memory
