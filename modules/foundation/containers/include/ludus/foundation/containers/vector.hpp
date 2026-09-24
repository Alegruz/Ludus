#pragma once

// Ludus::Vector<ElementType> — a dynamically sized, owning, heap-backed,
// contiguous array. Broad analogue of std::vector.
//
// Representation: { ElementType* mData; usize mSize; usize mCapacity; }.
// Invariants (checked by LUDUS_ASSERT in Debug/Development, free in Release):
//   * mSize <= mCapacity
//   * (mData == nullptr) == (mCapacity == 0)
//   * elements [0, mSize) are live objects; [mSize, mCapacity) is raw storage.
// A default-constructed / moved-from Vector is exactly {nullptr, 0, 0}.
//
// Error model (Ludus is -fno-exceptions, see AGENTS.md):
//   * Preconditions (index bounds, non-empty Back/PopBack) -> LUDUS_ASSERT.
//   * Allocation failure / capacity overflow:
//       - infallible API (PushBack, Reserve, Resize, ...) -> LUDUS_FATAL.
//       - fallible API (TryReserve/TryPushBack/TryResize/TryAppend) -> returns
//         false and leaves the container UNCHANGED (strong guarantee).
//
// Growth: geometric 2x (newCap = oldCap * 2) with a small minimum first
// capacity, overflow-checked. See docs/architecture/containers.md sections 15/32
// (the factor was settled at 2x by benchmark; the constant lives in
// vector_support.cpp ComputeGrowthCapacity).
//
// The heavy/cold paths (allocation, growth math, OOM/overflow handling) live
// out-of-line in vector_support.cpp so this header stays cheap to parse and the
// common inline paths stay small. Element lifetime lives in the private
// internal/contiguous_storage.hpp helpers.

#include <ludus/foundation/containers/internal/contiguous_storage.hpp>
#include <ludus/foundation/containers/relocation.hpp>

#include <ludus/foundation/base/assert.hpp>
#include <ludus/foundation/base/compiler.hpp>
#include <ludus/foundation/base/defines.h>
#include <ludus/foundation/base/types.h>

#include <span>
#include <type_traits>
#include <utility>

namespace ludus::foundation::core
{
namespace detail
{
// Raw, alignment-aware, nothrow allocation seam. Implemented once in
// vector_support.cpp in terms of the sized/aligned nothrow operator new/delete.
// This is the single insertion point for a future Ludus engine allocator: no
// call site changes when it is re-pointed. Returns nullptr on failure (never
// throws, never terminates here).
[[nodiscard]] void*
AllocateBytes(usize bytes, // NOLINT(bugprone-easily-swappable-parameters): allocation seam signature
              usize alignment) noexcept;
void FreeBytes(void* ptr,
               usize bytes, // NOLINT(bugprone-easily-swappable-parameters): allocation seam signature
               usize alignment) noexcept;

// Cold, non-template helpers (defined in vector_support.cpp):
//   * ComputeGrowthCapacity: 2x geometric growth with minimum + overflow check.
//   * OnAllocationFailure / OnCapacityOverflow: LUDUS_FATAL sinks for the
//     infallible API, kept out-of-line and cold so the hot path carries no
//     diagnostic string weight.
[[nodiscard]] usize
ComputeGrowthCapacity(usize currentCapacity, // NOLINT(bugprone-easily-swappable-parameters): growth-policy inputs
                      usize requested,
                      usize elementSize) noexcept;
[[noreturn]] LUDUS_COLD void OnAllocationFailure() noexcept;
[[noreturn]] LUDUS_COLD void OnCapacityOverflow() noexcept;

// Largest element count representable without size overflow, for a given element
// size. Mirrors std::vector's max_size intent; the exception-free replacement
// for length_error.
[[nodiscard]] LUDUS_INLINE constexpr usize MaxElementCount(usize elementSize) noexcept
{
    // Reserve headroom below SIZE_MAX; PTRDIFF_MAX-style ceiling on element count.
    constexpr usize kPtrdiffMax = static_cast<usize>(-1) >> 1;
    return elementSize == 0 ? static_cast<usize>(-1) : kPtrdiffMax / elementSize;
}
} // namespace detail

template <typename ElementType>
class Vector
{
    static_assert(!std::is_const_v<ElementType>, "Vector<const T> is not supported");
    static_assert(!std::is_reference_v<ElementType>, "Vector<T&> is not supported");
    // Reallocation of a non-relocatable type uses move-construction; a throwing
    // move would break the strong guarantee. Under -fno-exceptions moves cannot
    // throw, but require noexcept-movability (or copyability) to document intent.
    static_assert(
        IsTriviallyRelocatable<ElementType> || std::is_nothrow_move_constructible_v<ElementType> ||
            std::is_copy_constructible_v<ElementType>,
        "Vector<T> requires T to be trivially relocatable, nothrow-move-constructible, or copy-constructible");

public:
    using ValueType = ElementType;
    using SizeType = usize;
    using Iterator = ElementType*;
    using ConstIterator = const ElementType*;

    // --- Construction / destruction ----------------------------------------
    constexpr Vector() noexcept = default;

    explicit Vector(usize count)
    {
        if (count > 0)
        {
            GrowExact(count);
            internal_ValueConstructTail(count);
            mSize = count;
        }
    }

    Vector(usize count, const ElementType& value)
    {
        if (count > 0)
        {
            GrowExact(count);
            internal_FillTail(count, value);
            mSize = count;
        }
    }

    Vector(const Vector& other)
    {
        if (other.mSize > 0)
        {
            GrowExact(other.mSize);
            internal_CopyTail(other.mData, other.mSize);
            mSize = other.mSize;
        }
    }

    Vector(Vector&& other) noexcept : mData(other.mData), mSize(other.mSize), mCapacity(other.mCapacity)
    {
        other.mData = nullptr;
        other.mSize = 0;
        other.mCapacity = 0;
    }

    Vector& operator=(const Vector& other)
    {
        if (this == &other)
        {
            return *this;
        }
        // Reuse existing capacity when possible; otherwise reallocate.
        ClearElements();
        if (other.mSize > mCapacity)
        {
            FreeStorage();
            GrowExact(other.mSize);
        }
        internal_CopyTail(other.mData, other.mSize);
        mSize = other.mSize;
        return *this;
    }

    Vector& operator=(Vector&& other) noexcept
    {
        if (this == &other)
        {
            return *this;
        }
        ClearElements();
        FreeStorage();
        mData = other.mData;
        mSize = other.mSize;
        mCapacity = other.mCapacity;
        other.mData = nullptr;
        other.mSize = 0;
        other.mCapacity = 0;
        return *this;
    }

    ~Vector()
    {
        ClearElements();
        FreeStorage();
    }

    // --- Capacity -----------------------------------------------------------
    [[nodiscard]] usize Size() const noexcept
    {
        return mSize;
    }
    [[nodiscard]] usize Capacity() const noexcept
    {
        return mCapacity;
    }
    [[nodiscard]] bool Empty() const noexcept
    {
        return mSize == 0;
    }
    [[nodiscard]] ElementType* Data() noexcept
    {
        return mData;
    }
    [[nodiscard]] const ElementType* Data() const noexcept
    {
        return mData;
    }
    [[nodiscard]] static constexpr usize MaxSize() noexcept
    {
        return detail::MaxElementCount(kElementSize);
    }

    void Reserve(usize newCapacity)
    {
        if (!TryReserve(newCapacity))
        {
            detail::OnAllocationFailure();
        }
    }

    [[nodiscard]] bool TryReserve(usize newCapacity)
    {
        if (newCapacity <= mCapacity)
        {
            return true;
        }
        if (newCapacity > MaxSize())
        {
            return false;
        }
        return ReallocateTo(newCapacity);
    }

    void Resize(usize newSize)
    {
        if (!TryResizeImpl(newSize, nullptr))
        {
            detail::OnAllocationFailure();
        }
    }

    void Resize(usize newSize, const ElementType& value)
    {
        if (!TryResizeImpl(newSize, &value))
        {
            detail::OnAllocationFailure();
        }
    }

    [[nodiscard]] bool TryResize(usize newSize)
    {
        return TryResizeImpl(newSize, nullptr);
    }

    [[nodiscard]] bool TryResize(usize newSize, const ElementType& value)
    {
        return TryResizeImpl(newSize, &value);
    }

    void ShrinkToFit()
    {
        if (mSize == mCapacity)
        {
            return;
        }
        if (mSize == 0)
        {
            FreeStorage();
            return;
        }
        // Best-effort: on OOM keep the current (larger) allocation rather than
        // failing — shrinking is an optimization, never a correctness need.
        (void)ReallocateTo(mSize);
    }

    void Clear() noexcept
    {
        ClearElements();
    }

    // --- Element access -----------------------------------------------------
    [[nodiscard]] ElementType& operator[](usize index) noexcept
    {
        LUDUS_ASSERT(index < mSize);
        return mData[index];
    }
    [[nodiscard]] const ElementType& operator[](usize index) const noexcept
    {
        LUDUS_ASSERT(index < mSize);
        return mData[index];
    }
    [[nodiscard]] ElementType& Front() noexcept
    {
        LUDUS_ASSERT(mSize > 0);
        return mData[0];
    }
    [[nodiscard]] const ElementType& Front() const noexcept
    {
        LUDUS_ASSERT(mSize > 0);
        return mData[0];
    }
    [[nodiscard]] ElementType& Back() noexcept
    {
        LUDUS_ASSERT(mSize > 0);
        return mData[mSize - 1];
    }
    [[nodiscard]] const ElementType& Back() const noexcept
    {
        LUDUS_ASSERT(mSize > 0);
        return mData[mSize - 1];
    }

    // --- Iterators ----------------------------------------------------------
    [[nodiscard]] Iterator begin() noexcept
    {
        return mData;
    }
    [[nodiscard]] ConstIterator begin() const noexcept
    {
        return mData;
    }
    [[nodiscard]] ConstIterator cbegin() const noexcept
    {
        return mData;
    }
    [[nodiscard]] Iterator end() noexcept
    {
        return mData + mSize;
    }
    [[nodiscard]] ConstIterator end() const noexcept
    {
        return mData + mSize;
    }
    [[nodiscard]] ConstIterator cend() const noexcept
    {
        return mData + mSize;
    }
    // Lowercase for range-for / ranges / std interop.
    [[nodiscard]] ElementType* data() noexcept
    {
        return mData;
    }
    [[nodiscard]] const ElementType* data() const noexcept
    {
        return mData;
    }
    [[nodiscard]] usize size() const noexcept
    {
        return mSize;
    }

    // --- Modifiers ----------------------------------------------------------
    void PushBack(const ElementType& value)
    {
        EmplaceBack(value);
    }
    void PushBack(ElementType&& value)
    {
        EmplaceBack(static_cast<ElementType&&>(value));
    }

    template <typename... Args>
    ElementType& EmplaceBack(Args&&... args)
    {
        if (mSize == mCapacity) [[unlikely]]
        {
            // Slow path: growth is required. Materialize the element from `args`
            // into a local BEFORE growing, because `args` may alias an existing
            // element (e.g. v.PushBack(v.Back())) and growth frees the old block.
            // The local lives only in this cold branch, so the hot path below is
            // not penalized (no per-iteration argument spill). std::vector makes
            // self-referential push_back/emplace_back well-defined.
            ElementType value(static_cast<Args&&>(args)...);
            GrowForOne();
            ElementType* slot = internal::ConstructAt(mData + mSize, static_cast<ElementType&&>(value));
            ++mSize;
            return *slot;
        }
        ElementType* slot = internal::ConstructAt(mData + mSize, static_cast<Args&&>(args)...);
        ++mSize;
        return *slot;
    }

    [[nodiscard]] bool TryPushBack(const ElementType& value)
    {
        return TryEmplaceBack(value) != nullptr;
    }
    [[nodiscard]] bool TryPushBack(ElementType&& value)
    {
        return TryEmplaceBack(static_cast<ElementType&&>(value)) != nullptr;
    }

    template <typename... Args>
    [[nodiscard]] ElementType* TryEmplaceBack(Args&&... args)
    {
        if (mSize == mCapacity)
        {
            // Materialize from `args` before growth (self-reference safety, see
            // EmplaceBack); the local lives only in this cold branch.
            ElementType value(static_cast<Args&&>(args)...);
            if (!TryGrowForOne())
            {
                return nullptr;
            }
            ElementType* slot = internal::ConstructAt(mData + mSize, static_cast<ElementType&&>(value));
            ++mSize;
            return slot;
        }
        ElementType* slot = internal::ConstructAt(mData + mSize, static_cast<Args&&>(args)...);
        ++mSize;
        return slot;
    }

    void PopBack() noexcept
    {
        LUDUS_ASSERT(mSize > 0);
        --mSize;
        internal::DestroyRange(mData + mSize, mData + mSize + 1);
    }

    // Bulk append from a contiguous view. Single capacity check + one relocation.
    void Append(std::span<const ElementType> items)
    {
        if (!TryAppend(items))
        {
            detail::OnAllocationFailure();
        }
    }

    [[nodiscard]] bool TryAppend(std::span<const ElementType> items)
    {
        const usize count = items.size();
        if (count == 0)
        {
            return true;
        }
        // Guard aliasing: appending a view into our own storage would be
        // invalidated by a reallocation. Callers must not do that; assert it.
        LUDUS_ASSERT(items.data() < mData || items.data() >= mData + mCapacity);
        if (mSize + count < mSize) // usize overflow
        {
            return false;
        }
        if (mSize + count > mCapacity)
        {
            if (!TryReserve(mSize + count))
            {
                return false;
            }
        }
        internal::UninitializedCopy(mData + mSize, items.data(), count);
        mSize += count;
        return true;
    }

    // Ordered insert at `index` (shifts tail right). O(n).
    ElementType& Insert(usize index, const ElementType& value)
    {
        return EmplaceAt(index, value);
    }
    ElementType& Insert(usize index, ElementType&& value)
    {
        return EmplaceAt(index, static_cast<ElementType&&>(value));
    }

    template <typename... Args>
    ElementType& EmplaceAt(usize index, Args&&... args)
    {
        LUDUS_ASSERT(index <= mSize);
        if (index == mSize)
        {
            return EmplaceBack(static_cast<Args&&>(args)...);
        }
        // Materialize the new value into a temporary FIRST, before any shift or
        // reallocation. This is essential for correctness when `args` aliases an
        // element of this vector (e.g. v.EmplaceAt(i, v[j])): the shift/realloc
        // would otherwise move or free the referenced element before it is read,
        // producing a wrong value or a use-after-free. std::vector makes the same
        // guarantee for self-referential insert/emplace.
        ElementType value(static_cast<Args&&>(args)...);
        if (mSize == mCapacity) [[unlikely]]
        {
            GrowForOne();
        }
        // Open a gap at `index`; the slot becomes a live moved-from object.
        internal::ShiftRightByOne(mData + index, mData + mSize);
        ++mSize;
        // Move the already-constructed value into the opened slot.
        mData[index] = static_cast<ElementType&&>(value);
        return mData[index];
    }

    // Ordered erase at `index` (shifts tail left). O(n).
    void Erase(usize index) noexcept
    {
        LUDUS_ASSERT(index < mSize);
        internal::ShiftLeftByOne(mData + index, mData + mSize);
        --mSize;
        internal::DestroyRange(mData + mSize, mData + mSize + 1);
    }

    // Erase a range [first, last) of indices. O(n).
    void EraseRange(usize first, usize last) noexcept
    {
        LUDUS_ASSERT(first <= last);
        LUDUS_ASSERT(last <= mSize);
        const usize count = last - first;
        if (count == 0)
        {
            return;
        }
        // Move the tail [last, mSize) down into [first, ...).
        ElementType* dst = mData + first;
        ElementType* src = mData + last;
        ElementType* end = mData + mSize;
        for (; src != end; ++src, ++dst)
        {
            *dst = static_cast<ElementType&&>(*src);
        }
        internal::DestroyRange(mData + (mSize - count), end);
        mSize -= count;
    }

    // O(1) removal when order does not matter: overwrite with the last element,
    // then pop. The engine-preferred removal; named to prevent accidental O(n).
    void EraseUnordered(usize index) noexcept
    {
        LUDUS_ASSERT(index < mSize);
        const usize lastIdx = mSize - 1;
        if (index != lastIdx)
        {
            mData[index] = static_cast<ElementType&&>(mData[lastIdx]);
        }
        --mSize;
        internal::DestroyRange(mData + mSize, mData + mSize + 1);
    }

    void Swap(Vector& other) noexcept
    {
        ElementType* d = mData;
        usize s = mSize;
        usize c = mCapacity;
        mData = other.mData;
        mSize = other.mSize;
        mCapacity = other.mCapacity;
        other.mData = d;
        other.mSize = s;
        other.mCapacity = c;
    }

    // --- std::span interop --------------------------------------------------
    [[nodiscard]] operator std::span<ElementType>() noexcept
    {
        return std::span<ElementType>(mData, mSize);
    }
    [[nodiscard]] operator std::span<const ElementType>() const noexcept
    {
        return std::span<const ElementType>(mData, mSize);
    }
    [[nodiscard]] std::span<ElementType> AsSpan() noexcept
    {
        return std::span<ElementType>(mData, mSize);
    }
    [[nodiscard]] std::span<const ElementType> AsSpan() const noexcept
    {
        return std::span<const ElementType>(mData, mSize);
    }

    // Build a Vector from a contiguous view (explicit factory; no implicit
    // range/initializer_list ctor by design — see containers.md section 12).
    [[nodiscard]] static Vector From(std::span<const ElementType> items)
    {
        Vector v;
        v.Reserve(items.size());
        internal::UninitializedCopy(v.mData, items.data(), items.size());
        v.mSize = items.size();
        return v;
    }

    // --- Comparison ---------------------------------------------------------
    [[nodiscard]] friend bool operator==(const Vector& a, const Vector& b)
    {
        if (a.mSize != b.mSize)
        {
            return false;
        }
        for (usize i = 0; i < a.mSize; ++i)
        {
            if (!(a.mData[i] == b.mData[i]))
            {
                return false;
            }
        }
        return true;
    }

private:
    // Destroy live elements, keep capacity.
    void ClearElements() noexcept
    {
        internal::DestroyRange(mData, mData + mSize);
        mSize = 0;
    }

    // Size of one element. Centralizes the (legitimate) sizeof(ElementType),
    // which may be a pointer type for Vector<T*>; expressed once here so the
    // pointer-sizeof suppression lives in a single place.
    // NOLINTNEXTLINE(bugprone-sizeof-expression)
    static constexpr usize kElementSize = sizeof(ElementType);

    // Byte size of `count` elements.
    [[nodiscard]] static constexpr usize ByteSize(usize count) noexcept
    {
        return count * kElementSize;
    }

    // Allocate a raw block for `count` elements through the seam (nullptr on
    // failure). Returns typed storage; the void* cast lives here.
    [[nodiscard]] static ElementType* AllocateStorage(usize count) noexcept
    {
        return static_cast<ElementType*>(detail::AllocateBytes(ByteSize(count), alignof(ElementType)));
    }

    // Free a raw block of `capacity` elements through the seam. ElementType* ->
    // void* is a deliberate erase to the byte block (ElementType may be a pointer
    // type for Vector<T*>).
    static void FreeStorageBytes(ElementType* data, usize capacity) noexcept
    {
        // NOLINTNEXTLINE(bugprone-multi-level-implicit-pointer-conversion)
        detail::FreeBytes(static_cast<void*>(data), ByteSize(capacity), alignof(ElementType));
    }

    // Free the storage block (elements must already be destroyed). Resets to
    // the canonical empty representation.
    void FreeStorage() noexcept
    {
        if (mData != nullptr)
        {
            FreeStorageBytes(mData, mCapacity);
            mData = nullptr;
            mCapacity = 0;
        }
    }

    // Allocate exactly `capacity` slots into an EMPTY vector (mData==nullptr).
    // Infallible: fatal on failure. Used by constructors.
    void GrowExact(usize capacity)
    {
        LUDUS_ASSERT(mData == nullptr && mSize == 0 && mCapacity == 0);
        if (capacity > MaxSize())
        {
            detail::OnCapacityOverflow();
        }
        ElementType* block = AllocateStorage(capacity);
        if (block == nullptr)
        {
            detail::OnAllocationFailure();
        }
        mData = block;
        mCapacity = capacity;
    }

    // Reallocate to exactly `newCapacity` (>= mSize), relocating live elements.
    // Returns false on allocation failure, leaving the vector UNCHANGED.
    [[nodiscard]] bool ReallocateTo(usize newCapacity)
    {
        LUDUS_ASSERT(newCapacity >= mSize);
        if (newCapacity == 0)
        {
            FreeStorage();
            return true;
        }
        ElementType* newData = AllocateStorage(newCapacity);
        if (newData == nullptr)
        {
            return false;
        }
        internal::UninitializedRelocate(newData, mData, mSize);
        // Relocate consumed the sources; free only the old raw block.
        if (mData != nullptr)
        {
            FreeStorageBytes(mData, mCapacity);
        }
        mData = newData;
        mCapacity = newCapacity;
        return true;
    }

    // Grow to hold at least one more element, infallible (fatal on OOM/overflow).
    LUDUS_NOINLINE void GrowForOne()
    {
        const usize newCap = detail::ComputeGrowthCapacity(mCapacity, mCapacity + 1, kElementSize);
        if (newCap == 0)
        {
            detail::OnCapacityOverflow();
        }
        if (!ReallocateTo(newCap))
        {
            detail::OnAllocationFailure();
        }
    }

    // Fallible growth for one more element.
    [[nodiscard]] bool TryGrowForOne()
    {
        const usize newCap = detail::ComputeGrowthCapacity(mCapacity, mCapacity + 1, kElementSize);
        if (newCap == 0)
        {
            return false;
        }
        return ReallocateTo(newCap);
    }

    // Resize implementation shared by Resize/TryResize. `fill` is nullptr for
    // value-initialization, or points to the value to copy for new elements.
    [[nodiscard]] bool TryResizeImpl(usize newSize, const ElementType* fill)
    {
        if (newSize < mSize)
        {
            internal::DestroyRange(mData + newSize, mData + mSize);
            mSize = newSize;
            return true;
        }
        if (newSize == mSize)
        {
            return true;
        }
        if (newSize > mCapacity)
        {
            if (!TryReserve(newSize))
            {
                return false;
            }
        }
        const usize added = newSize - mSize;
        if (fill == nullptr)
        {
            internal::UninitializedValueConstruct(mData + mSize, added);
        }
        else
        {
            internal::UninitializedFill(mData + mSize, added, *fill);
        }
        mSize = newSize;
        return true;
    }

    // Tail constructors used only right after GrowExact on an empty vector.
    void internal_ValueConstructTail(usize count)
    {
        internal::UninitializedValueConstruct(mData, count);
    }
    void internal_FillTail(usize count, const ElementType& value)
    {
        internal::UninitializedFill(mData, count, value);
    }
    void internal_CopyTail(const ElementType* src, usize count)
    {
        internal::UninitializedCopy(mData, src, count);
    }

    ElementType* mData = nullptr;
    usize mSize = 0;
    usize mCapacity = 0;
};

// Non-member swap for ADL / std interop.
template <typename ElementType>
void Swap(Vector<ElementType>& a, Vector<ElementType>& b) noexcept
{
    a.Swap(b);
}
} // namespace ludus::foundation::core

namespace ludus::foundation
{
template <typename ElementType>
using Vector = core::Vector<ElementType>;
} // namespace ludus::foundation
