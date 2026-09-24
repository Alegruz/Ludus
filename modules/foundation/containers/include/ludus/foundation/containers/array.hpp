#pragma once

// Ludus::Array<ElementType> — a dynamically sized, owning, heap-backed,
// contiguous array. This is the engine's default growable sequence container.
//
// Naming (see docs/architecture/containers.md): the dynamic owning array is
// "Array" (not "Vector") because "Vector" is heavily overloaded in a
// graphics/math engine (Vec2/Vec3/Vec4, direction, velocity, ...). Reserving
// "Vector" for mathematics and calling the growable sequence "Array" removes
// that collision; the fixed-size sibling is Ludus::StaticArray<T, N>. Member
// functions follow the Ludus verb-oriented convention (GetSize/IsEmpty/GetData/
// EnsureCapacity/Add/RemoveLast/...), not STL spellings.
//
// Representation: { ElementType* mData; usize mSize; usize mCapacity; }.
// Invariants (checked by LUDUS_ASSERT in Debug/Development, free in Release):
//   * mSize <= mCapacity
//   * (mData == nullptr) == (mCapacity == 0)
//   * elements [0, mSize) are live objects; [mSize, mCapacity) is raw storage.
// A default-constructed / moved-from Array is exactly {nullptr, 0, 0}.
//
// Error model (Ludus is -fno-exceptions, see AGENTS.md):
//   * Preconditions (index bounds, non-empty GetLast/RemoveLast) -> LUDUS_ASSERT.
//   * Allocation failure / capacity overflow:
//       - infallible API (Add, EnsureCapacity, Resize, ...) -> LUDUS_FATAL.
//       - fallible API (TryEnsureCapacity/TryAdd/TryResize/TryAddRange) -> returns
//         false and leaves the container UNCHANGED (strong guarantee).
//
// Growth: geometric 2x (newCap = oldCap * 2) with a small minimum first
// capacity, overflow-checked. See docs/architecture/containers.md sections 15/32
// (the factor was settled at 2x by benchmark; the constant lives in
// array_support.cpp ComputeGrowthCapacity).
//
// The heavy/cold paths (allocation, growth math, OOM/overflow handling) live
// out-of-line in array_support.cpp so this header stays cheap to parse and the
// common inline paths stay small. Element lifetime lives in the
// detail/contiguous_storage.hpp helpers.

#include <ludus/foundation/containers/detail/contiguous_storage.hpp>
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
// array_support.cpp in terms of the sized/aligned nothrow operator new/delete.
// This is the single insertion point for a future Ludus engine allocator: no
// call site changes when it is re-pointed. Returns nullptr on failure (never
// throws, never terminates here).
[[nodiscard]] void*
AllocateBytes(usize bytes, // NOLINT(bugprone-easily-swappable-parameters): allocation seam signature
              usize alignment) noexcept;
void FreeBytes(void* ptr,
               usize bytes, // NOLINT(bugprone-easily-swappable-parameters): allocation seam signature
               usize alignment) noexcept;

// Cold, non-template helpers (defined in array_support.cpp):
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
// size. The exception-free replacement for std::length_error.
[[nodiscard]] LUDUS_INLINE constexpr usize MaxElementCount(usize elementSize) noexcept
{
    // Reserve headroom below SIZE_MAX; PTRDIFF_MAX-style ceiling on element count.
    constexpr usize kPtrdiffMax = static_cast<usize>(-1) >> 1;
    return elementSize == 0 ? static_cast<usize>(-1) : kPtrdiffMax / elementSize;
}
} // namespace detail

template <typename ElementType>
class Array
{
    static_assert(!std::is_const_v<ElementType>, "Array<const T> is not supported");
    static_assert(!std::is_reference_v<ElementType>, "Array<T&> is not supported");
    // Reallocation of a non-relocatable type uses move-construction; a throwing
    // move would break the strong guarantee. Under -fno-exceptions moves cannot
    // throw, but require noexcept-movability (or copyability) to document intent.
    static_assert(IsTriviallyRelocatable<ElementType> || std::is_nothrow_move_constructible_v<ElementType> ||
                      std::is_copy_constructible_v<ElementType>,
                  "Array<T> requires T to be trivially relocatable, nothrow-move-constructible, or copy-constructible");

public:
    using ValueType = ElementType;
    using SizeType = usize;
    using Iterator = ElementType*;
    using ConstIterator = const ElementType*;

    // --- Construction / destruction ----------------------------------------
    constexpr Array() noexcept = default;

    explicit Array(usize count)
    {
        if (count > 0)
        {
            GrowExact(count);
            internal_ValueConstructTail(count);
            mSize = count;
        }
    }

    Array(usize count, const ElementType& value)
    {
        if (count > 0)
        {
            GrowExact(count);
            internal_FillTail(count, value);
            mSize = count;
        }
    }

    Array(const Array& other)
    {
        if (other.mSize > 0)
        {
            GrowExact(other.mSize);
            internal_CopyTail(other.mData, other.mSize);
            mSize = other.mSize;
        }
    }

    Array(Array&& other) noexcept : mData(other.mData), mSize(other.mSize), mCapacity(other.mCapacity)
    {
        other.mData = nullptr;
        other.mSize = 0;
        other.mCapacity = 0;
    }

    Array& operator=(const Array& other)
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

    Array& operator=(Array&& other) noexcept
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

    ~Array()
    {
        ClearElements();
        FreeStorage();
    }

    // --- Size / capacity ----------------------------------------------------
    [[nodiscard]] usize GetSize() const noexcept
    {
        return mSize;
    }
    [[nodiscard]] usize GetCapacity() const noexcept
    {
        return mCapacity;
    }
    [[nodiscard]] bool IsEmpty() const noexcept
    {
        return mSize == 0;
    }
    [[nodiscard]] ElementType* GetData() noexcept
    {
        return mData;
    }
    [[nodiscard]] const ElementType* GetData() const noexcept
    {
        return mData;
    }
    [[nodiscard]] static constexpr usize GetMaxSize() noexcept
    {
        return detail::MaxElementCount(kElementSize);
    }

    // Guarantee GetCapacity() >= newCapacity without changing GetSize(). If the
    // current capacity already satisfies the request, does nothing (never
    // shrinks). This is the Ludus-native spelling of "reserve": the name states
    // the postcondition. Infallible (fatal on OOM/overflow).
    void EnsureCapacity(usize newCapacity)
    {
        if (!TryEnsureCapacity(newCapacity))
        {
            detail::OnAllocationFailure();
        }
    }

    // Fallible EnsureCapacity: returns false (container unchanged) on OOM/overflow.
    [[nodiscard]] bool TryEnsureCapacity(usize newCapacity)
    {
        if (newCapacity <= mCapacity)
        {
            return true;
        }
        if (newCapacity > GetMaxSize())
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

    // Release capacity down to the current size (best effort; never grows, never
    // fails observably — on OOM it keeps the larger allocation).
    void TrimCapacity()
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
    [[nodiscard]] ElementType& GetFirst() noexcept
    {
        LUDUS_ASSERT(mSize > 0);
        return mData[0];
    }
    [[nodiscard]] const ElementType& GetFirst() const noexcept
    {
        LUDUS_ASSERT(mSize > 0);
        return mData[0];
    }
    [[nodiscard]] ElementType& GetLast() noexcept
    {
        LUDUS_ASSERT(mSize > 0);
        return mData[mSize - 1];
    }
    [[nodiscard]] const ElementType& GetLast() const noexcept
    {
        LUDUS_ASSERT(mSize > 0);
        return mData[mSize - 1];
    }

    // --- Iterators (lowercase for range-for / ranges / std interop) ---------
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

    // --- Adding elements ----------------------------------------------------
    void Add(const ElementType& value)
    {
        AddInPlace(value);
    }
    void Add(ElementType&& value)
    {
        AddInPlace(static_cast<ElementType&&>(value));
    }

    // Construct a new element in place at the end from `args`. Returns a
    // reference to the new element.
    template <typename... Args>
    ElementType& AddInPlace(Args&&... args)
    {
        if (mSize == mCapacity) [[unlikely]]
        {
            // Slow path: growth is required. Materialize the element from `args`
            // into a local BEFORE growing, because `args` may alias an existing
            // element (e.g. array.Add(array.GetLast())) and growth frees the old
            // block. The local lives only in this cold branch, so the hot path
            // below is not penalized (no per-iteration argument spill).
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

    // Fallible add: returns false (container unchanged) on OOM/overflow.
    [[nodiscard]] bool TryAdd(const ElementType& value)
    {
        return TryAddInPlace(value) != nullptr;
    }
    [[nodiscard]] bool TryAdd(ElementType&& value)
    {
        return TryAddInPlace(static_cast<ElementType&&>(value)) != nullptr;
    }

    template <typename... Args>
    [[nodiscard]] ElementType* TryAddInPlace(Args&&... args)
    {
        if (mSize == mCapacity)
        {
            // Materialize from `args` before growth (self-reference safety, see
            // AddInPlace); the local lives only in this cold branch.
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

    // Append a contiguous range to the end. Single capacity check + one copy.
    void AddRange(std::span<const ElementType> items)
    {
        if (!TryAddRange(items))
        {
            detail::OnAllocationFailure();
        }
    }

    [[nodiscard]] bool TryAddRange(std::span<const ElementType> items)
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
            if (!TryEnsureCapacity(mSize + count))
            {
                return false;
            }
        }
        internal::UninitializedCopy(mData + mSize, items.data(), count);
        mSize += count;
        return true;
    }

    // --- Removing elements --------------------------------------------------
    void RemoveLast() noexcept
    {
        LUDUS_ASSERT(mSize > 0);
        --mSize;
        internal::DestroyRange(mData + mSize, mData + mSize + 1);
    }

    // Order-preserving remove at `index` (shifts the tail left). O(n).
    void RemoveAt(usize index) noexcept
    {
        LUDUS_ASSERT(index < mSize);
        internal::ShiftLeftByOne(mData + index, mData + mSize);
        --mSize;
        internal::DestroyRange(mData + mSize, mData + mSize + 1);
    }

    // Order-preserving remove of the index range [first, last). O(n).
    void RemoveRange(usize first, usize last) noexcept
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
        ElementType* stop = mData + mSize;
        for (; src != stop; ++src, ++dst)
        {
            *dst = static_cast<ElementType&&>(*src);
        }
        internal::DestroyRange(mData + (mSize - count), stop);
        mSize -= count;
    }

    // O(1) removal when order does not matter: overwrite the slot with the last
    // element, then drop the last. The engine-preferred removal; named so the
    // O(1) swap behaviour (and the resulting reorder) is explicit at call sites.
    void RemoveAtSwap(usize index) noexcept
    {
        LUDUS_ASSERT(index < mSize);
        const usize lastIndex = mSize - 1;
        if (index != lastIndex)
        {
            mData[index] = static_cast<ElementType&&>(mData[lastIndex]);
        }
        --mSize;
        internal::DestroyRange(mData + mSize, mData + mSize + 1);
    }

    // --- Inserting elements -------------------------------------------------
    ElementType& InsertAt(usize index, const ElementType& value)
    {
        return InsertAtInPlace(index, value);
    }
    ElementType& InsertAt(usize index, ElementType&& value)
    {
        return InsertAtInPlace(index, static_cast<ElementType&&>(value));
    }

    // Construct a new element in place at `index`, shifting the tail right. O(n).
    template <typename... Args>
    ElementType& InsertAtInPlace(usize index, Args&&... args)
    {
        LUDUS_ASSERT(index <= mSize);
        if (index == mSize)
        {
            return AddInPlace(static_cast<Args&&>(args)...);
        }
        // Materialize the new value into a temporary FIRST, before any shift or
        // reallocation. Essential when `args` aliases an element of this array
        // (e.g. array.InsertAt(i, array[j])): the shift/realloc would otherwise
        // move or free the referenced element before it is read.
        ElementType value(static_cast<Args&&>(args)...);
        if (mSize == mCapacity) [[unlikely]]
        {
            GrowForOne();
        }
        // Open a gap at `index`; the slot becomes a live moved-from object.
        internal::ShiftRightByOne(mData + index, mData + mSize);
        ++mSize;
        mData[index] = static_cast<ElementType&&>(value);
        return mData[index];
    }

    // --- Misc ---------------------------------------------------------------
    void Swap(Array& other) noexcept
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

    // Build an Array by copying a contiguous view (explicit factory; there is no
    // implicit range/initializer_list constructor by design — containers.md §12).
    [[nodiscard]] static Array FromRange(std::span<const ElementType> items)
    {
        Array result;
        result.EnsureCapacity(items.size());
        internal::UninitializedCopy(result.mData, items.data(), items.size());
        result.mSize = items.size();
        return result;
    }

    // --- Comparison ---------------------------------------------------------
    [[nodiscard]] friend bool operator==(const Array& a, const Array& b)
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
    // Size of one element. Centralizes the (legitimate) sizeof(ElementType),
    // which may be a pointer type for Array<T*>; expressed once here so the
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
    // type for Array<T*>).
    static void FreeStorageBytes(ElementType* dataPtr, usize capacity) noexcept
    {
        // NOLINTNEXTLINE(bugprone-multi-level-implicit-pointer-conversion)
        detail::FreeBytes(static_cast<void*>(dataPtr), ByteSize(capacity), alignof(ElementType));
    }

    // Destroy live elements, keep capacity.
    void ClearElements() noexcept
    {
        internal::DestroyRange(mData, mData + mSize);
        mSize = 0;
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

    // Allocate exactly `capacity` slots into an EMPTY array (mData==nullptr).
    // Infallible: fatal on failure. Used by constructors.
    void GrowExact(usize capacity)
    {
        LUDUS_ASSERT(mData == nullptr && mSize == 0 && mCapacity == 0);
        if (capacity > GetMaxSize())
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
    // Returns false on allocation failure, leaving the array UNCHANGED.
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
            if (!TryEnsureCapacity(newSize))
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

    // Tail constructors used only right after GrowExact on an empty array.
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

// Non-member Swap for ADL.
template <typename ElementType>
void Swap(Array<ElementType>& a, Array<ElementType>& b) noexcept
{
    a.Swap(b);
}
} // namespace ludus::foundation::core

namespace ludus::foundation
{
template <typename ElementType>
using Array = core::Array<ElementType>;
} // namespace ludus::foundation
