#pragma once

// Ludus::StaticArray<ElementType, Count> — a fixed-size, owning, contiguous
// aggregate whose element count is a compile-time constant.
//
// Broad analogue of std::array: no indirection, no allocation. It is an
// aggregate (no user-declared constructors, no private data, no bases) so it
// supports aggregate initialization and constexpr, and is trivially copyable /
// trivially destructible exactly when ElementType is.
//
// Naming (see docs/architecture/containers.md): the type is StaticArray (not
// "Array") so it is immediately obvious at a call site that the size is fixed at
// compile time; the dynamically-sized owning array is Ludus::Array<T>. Member
// functions follow the Ludus verb-oriented convention (GetSize/IsEmpty/GetData/
// GetFirst/GetLast/Fill/Swap), not STL spellings. Lowercase begin/end/data/size
// aliases exist only for range-for / ranges / std-algorithm interop.
//
// Bounds checks in operator[] use LUDUS_ASSERT: rich in Debug/Development,
// compiled out (zero cost) in Profile/Release. There is no checked accessor that
// throws (Ludus is -fno-exceptions). StaticArray converts to std::span for
// pointer+count / algorithm / GPU-API interop.
//
// The storage member is public (mData) because an aggregate cannot hide it and
// remain brace-initializable; treat it as an implementation detail and use the
// accessors.

#include <ludus/foundation/base/core.h>

#include <compare>
#include <span>
#include <type_traits>
#include <utility>

namespace ludus::foundation::core
{
template <typename ElementType, usize Count>
struct StaticArray
{
    using ValueType = ElementType;
    using SizeType = usize;
    using Iterator = ElementType*;
    using ConstIterator = const ElementType*;

    // Public storage: an aggregate cannot hide this and remain brace-initializable.
    ElementType mData[Count];

    // --- Size / capacity (all compile-time constants) -----------------------
    [[nodiscard]] static constexpr usize GetSize() noexcept
    {
        return Count;
    }
    [[nodiscard]] static constexpr usize GetCapacity() noexcept
    {
        return Count;
    }
    [[nodiscard]] static constexpr bool IsEmpty() noexcept
    {
        return Count == 0;
    }

    // --- Data ---------------------------------------------------------------
    [[nodiscard]] constexpr ElementType* GetData() noexcept
    {
        return mData;
    }
    [[nodiscard]] constexpr const ElementType* GetData() const noexcept
    {
        return mData;
    }

    // --- Element access -----------------------------------------------------
    [[nodiscard]] constexpr ElementType& operator[](usize index) noexcept
    {
        LUDUS_ASSERT(index < Count);
        return mData[index];
    }
    [[nodiscard]] constexpr const ElementType& operator[](usize index) const noexcept
    {
        LUDUS_ASSERT(index < Count);
        return mData[index];
    }
    [[nodiscard]] constexpr ElementType& GetFirst() noexcept
    {
        LUDUS_ASSERT(Count > 0);
        return mData[0];
    }
    [[nodiscard]] constexpr const ElementType& GetFirst() const noexcept
    {
        LUDUS_ASSERT(Count > 0);
        return mData[0];
    }
    [[nodiscard]] constexpr ElementType& GetLast() noexcept
    {
        LUDUS_ASSERT(Count > 0);
        return mData[Count - 1];
    }
    [[nodiscard]] constexpr const ElementType& GetLast() const noexcept
    {
        LUDUS_ASSERT(Count > 0);
        return mData[Count - 1];
    }

    // --- Iterators (lowercase for range-for / ranges interop) ---------------
    [[nodiscard]] constexpr Iterator begin() noexcept
    {
        return mData;
    }
    [[nodiscard]] constexpr ConstIterator begin() const noexcept
    {
        return mData;
    }
    [[nodiscard]] constexpr ConstIterator cbegin() const noexcept
    {
        return mData;
    }
    [[nodiscard]] constexpr Iterator end() noexcept
    {
        return mData + Count;
    }
    [[nodiscard]] constexpr ConstIterator end() const noexcept
    {
        return mData + Count;
    }
    [[nodiscard]] constexpr ConstIterator cend() const noexcept
    {
        return mData + Count;
    }

    // Lowercase data()/size() so free std:: utilities and generic code work.
    [[nodiscard]] constexpr ElementType* data() noexcept
    {
        return mData;
    }
    [[nodiscard]] constexpr const ElementType* data() const noexcept
    {
        return mData;
    }
    [[nodiscard]] static constexpr usize size() noexcept
    {
        return Count;
    }

    // --- Bulk ops -----------------------------------------------------------
    constexpr void Fill(const ElementType& value)
    {
        for (usize i = 0; i < Count; ++i)
        {
            mData[i] = value;
        }
    }

    constexpr void Swap(StaticArray& other) noexcept(std::is_nothrow_swappable_v<ElementType>)
    {
        using std::swap;
        for (usize i = 0; i < Count; ++i)
        {
            swap(mData[i], other.mData[i]);
        }
    }

    // --- std::span interop --------------------------------------------------
    [[nodiscard]] constexpr operator std::span<ElementType>() noexcept
    {
        return std::span<ElementType>(mData, Count);
    }
    [[nodiscard]] constexpr operator std::span<const ElementType>() const noexcept
    {
        return std::span<const ElementType>(mData, Count);
    }
    [[nodiscard]] constexpr std::span<ElementType> AsSpan() noexcept
    {
        return std::span<ElementType>(mData, Count);
    }
    [[nodiscard]] constexpr std::span<const ElementType> AsSpan() const noexcept
    {
        return std::span<const ElementType>(mData, Count);
    }

    // --- Comparison (defaulted; value/key usable) ---------------------------
    [[nodiscard]] friend constexpr bool operator==(const StaticArray&, const StaticArray&) = default;
    [[nodiscard]] friend constexpr auto operator<=>(const StaticArray&, const StaticArray&) = default;
};

// Zero-size specialization. std::array<T,0> is allowed and data() returns a
// valid, non-dereferenceable pointer; we mirror that. Keeping it a distinct
// specialization avoids a zero-length array member (ill-formed / an extension).
template <typename ElementType>
struct StaticArray<ElementType, 0>
{
    using ValueType = ElementType;
    using SizeType = usize;
    using Iterator = ElementType*;
    using ConstIterator = const ElementType*;

    [[nodiscard]] static constexpr usize GetSize() noexcept
    {
        return 0;
    }
    [[nodiscard]] static constexpr usize GetCapacity() noexcept
    {
        return 0;
    }
    [[nodiscard]] static constexpr bool IsEmpty() noexcept
    {
        return true;
    }

    [[nodiscard]] constexpr ElementType* GetData() noexcept
    {
        return nullptr;
    }
    [[nodiscard]] constexpr const ElementType* GetData() const noexcept
    {
        return nullptr;
    }
    [[nodiscard]] constexpr ElementType* data() noexcept
    {
        return nullptr;
    }
    [[nodiscard]] constexpr const ElementType* data() const noexcept
    {
        return nullptr;
    }
    [[nodiscard]] static constexpr usize size() noexcept
    {
        return 0;
    }

    [[nodiscard]] constexpr Iterator begin() noexcept
    {
        return nullptr;
    }
    [[nodiscard]] constexpr ConstIterator begin() const noexcept
    {
        return nullptr;
    }
    [[nodiscard]] constexpr ConstIterator cbegin() const noexcept
    {
        return nullptr;
    }
    [[nodiscard]] constexpr Iterator end() noexcept
    {
        return nullptr;
    }
    [[nodiscard]] constexpr ConstIterator end() const noexcept
    {
        return nullptr;
    }
    [[nodiscard]] constexpr ConstIterator cend() const noexcept
    {
        return nullptr;
    }

    constexpr void Fill(const ElementType&) noexcept {}
    constexpr void Swap(StaticArray&) noexcept {}

    [[nodiscard]] constexpr operator std::span<ElementType>() noexcept
    {
        return std::span<ElementType>();
    }
    [[nodiscard]] constexpr operator std::span<const ElementType>() const noexcept
    {
        return std::span<const ElementType>();
    }
    [[nodiscard]] constexpr std::span<ElementType> AsSpan() noexcept
    {
        return std::span<ElementType>();
    }
    [[nodiscard]] constexpr std::span<const ElementType> AsSpan() const noexcept
    {
        return std::span<const ElementType>();
    }

    [[nodiscard]] friend constexpr bool operator==(const StaticArray&, const StaticArray&) noexcept
    {
        return true;
    }
    [[nodiscard]] friend constexpr std::strong_ordering operator<=>(const StaticArray&, const StaticArray&) noexcept
    {
        return std::strong_ordering::equal;
    }
};

// Non-member Swap for ADL.
template <typename ElementType, usize Count>
constexpr void Swap(StaticArray<ElementType, Count>& a,
                    StaticArray<ElementType, Count>& b) noexcept(noexcept(a.Swap(b)))
{
    a.Swap(b);
}

// Deduction guide: StaticArray{a, b, c} deduces StaticArray<CommonType, N>.
template <typename First, typename... Rest>
StaticArray(First, Rest...) -> StaticArray<First, 1 + sizeof...(Rest)>;
} // namespace ludus::foundation::core

namespace ludus::foundation
{
template <typename ElementType, core::usize Count>
using StaticArray = core::StaticArray<ElementType, Count>;
} // namespace ludus::foundation
