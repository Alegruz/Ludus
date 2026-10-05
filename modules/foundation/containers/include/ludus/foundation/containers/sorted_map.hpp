#pragma once

#include <ludus/foundation/base/core.h>

#include <ludus/foundation/containers/array.hpp>

#include <span>
#include <type_traits>

namespace ludus::foundation::core
{
// Stateless ordering policy. Keys must provide a nonthrowing strict weak order.
struct KeyLess
{
    template <typename KeyType>
    [[nodiscard]] constexpr bool operator()(const KeyType& left, const KeyType& right) const
        noexcept(noexcept(left < right))
    {
        return left < right;
    }
};

// One contiguous, sorted allocation. O(log n) lookup, O(n) insertion/removal.
// Keys are exposed only through const entries; Find permits value mutation.
// Insert/remove invalidate borrows at/after the position, growth invalidates
// all borrows, failed/duplicate insertion invalidates none. Single-owner.
// See docs/architecture/container-systems.md for the full lifetime contract.
template <typename KeyType, typename ValueType, typename Compare = KeyLess>
class SortedMap
{
    static_assert(std::is_nothrow_copy_constructible_v<KeyType> && std::is_nothrow_move_constructible_v<KeyType> &&
                  std::is_nothrow_move_assignable_v<KeyType> && std::is_nothrow_destructible_v<KeyType>);
    static_assert(std::is_nothrow_move_constructible_v<ValueType> && std::is_nothrow_move_assignable_v<ValueType> &&
                  std::is_nothrow_destructible_v<ValueType>);
    static_assert(std::is_empty_v<Compare> && std::is_nothrow_default_constructible_v<Compare>);
    static_assert(std::is_nothrow_invocable_r_v<bool, const Compare&, const KeyType&, const KeyType&>);

public:
    struct Entry
    {
        KeyType Key;
        ValueType Value;

        template <typename... Args>
        explicit Entry(const KeyType& key, Args&&... args) noexcept : Key(key), Value(static_cast<Args&&>(args)...)
        {
            static_assert(std::is_nothrow_constructible_v<ValueType, Args...>);
        }
    };

    struct InsertResult
    {
        ValueType* Value = nullptr; // nullptr: allocation/capacity failure.
        bool Inserted = false;      // non-null and false: existing key, untouched.
    };

    SortedMap() noexcept = default;
    SortedMap(const SortedMap&) noexcept
        requires std::is_nothrow_copy_constructible_v<ValueType>
    = default;
    SortedMap& operator=(const SortedMap&) noexcept
        requires std::is_nothrow_copy_constructible_v<ValueType>
    = default;
    SortedMap(SortedMap&&) noexcept = default;
    SortedMap& operator=(SortedMap&&) noexcept = default;
    ~SortedMap() = default;

    [[nodiscard]] usize GetSize() const noexcept
    {
        return mEntries.GetSize();
    }
    [[nodiscard]] usize GetCapacity() const noexcept
    {
        return mEntries.GetCapacity();
    }
    [[nodiscard]] bool IsEmpty() const noexcept
    {
        return mEntries.IsEmpty();
    }

    void EnsureCapacity(usize capacity) noexcept
    {
        mEntries.EnsureCapacity(capacity);
    }
    [[nodiscard]] bool TryEnsureCapacity(usize capacity) noexcept
    {
        return mEntries.TryEnsureCapacity(capacity);
    }

    [[nodiscard]] ValueType* Find(const KeyType& key) noexcept
    {
        const usize index = LowerBound(key);
        return Matches(index, key) ? &mEntries[index].Value : nullptr;
    }
    [[nodiscard]] const ValueType* Find(const KeyType& key) const noexcept
    {
        const usize index = LowerBound(key);
        return Matches(index, key) ? &mEntries[index].Value : nullptr;
    }
    [[nodiscard]] bool Contains(const KeyType& key) const noexcept
    {
        return Find(key) != nullptr;
    }

    template <typename... Args>
        requires std::is_nothrow_constructible_v<ValueType, Args...>
    [[nodiscard]] InsertResult TryAddInPlace(const KeyType& key, Args&&... args) noexcept
    {
        const usize index = LowerBound(key);
        if (Matches(index, key))
        {
            return { .Value = &mEntries[index].Value, .Inserted = false };
        }
        Entry* entry = mEntries.TryInsertAtInPlace(index, key, static_cast<Args&&>(args)...);
        return { .Value = entry == nullptr ? nullptr : &entry->Value, .Inserted = entry != nullptr };
    }

    template <typename... Args>
        requires std::is_nothrow_constructible_v<ValueType, Args...>
    InsertResult AddInPlace(const KeyType& key, Args&&... args) noexcept
    {
        InsertResult result = TryAddInPlace(key, static_cast<Args&&>(args)...);
        if (result.Value == nullptr)
        {
            detail::OnAllocationFailure();
        }
        return result;
    }

    [[nodiscard]] bool Remove(const KeyType& key) noexcept
    {
        const usize index = LowerBound(key);
        if (!Matches(index, key))
        {
            return false;
        }
        mEntries.RemoveAt(index);
        return true;
    }

    void Clear() noexcept
    {
        mEntries.Clear();
    }
    void Reset() noexcept
    {
        mEntries.Clear();
        mEntries.TrimCapacity();
    }
    void Swap(SortedMap& other) noexcept
    {
        mEntries.Swap(other.mEntries);
    }

    [[nodiscard]] std::span<const Entry> AsSpan() const noexcept
    {
        return mEntries.AsSpan();
    }
    [[nodiscard]] const Entry* begin() const noexcept
    {
        return mEntries.GetData();
    }
    [[nodiscard]] const Entry* end() const noexcept
    {
        // Avoid pointer arithmetic on the null data of a default/moved map.
        return IsEmpty() ? mEntries.GetData() : mEntries.GetData() + GetSize();
    }

private:
    [[nodiscard]] usize LowerBound(const KeyType& key) const noexcept
    {
        usize first = 0;
        usize count = GetSize();
        while (count != 0)
        {
            const usize step = count / 2;
            const usize middle = first + step;
            if (Compare{}(mEntries[middle].Key, key))
            {
                first = middle + 1;
                count -= step + 1;
            }
            else
            {
                count = step;
            }
        }
        return first;
    }
    [[nodiscard]] bool Matches(usize index, const KeyType& key) const noexcept
    {
        // LowerBound already proves stored key is not less than the query.
        return index != GetSize() && !Compare{}(key, mEntries[index].Key);
    }

    Array<Entry> mEntries;
};

template <typename KeyType, typename ValueType, typename Compare>
void Swap(SortedMap<KeyType, ValueType, Compare>& left, SortedMap<KeyType, ValueType, Compare>& right) noexcept
{
    left.Swap(right);
}
} // namespace ludus::foundation::core

namespace ludus::foundation
{
using core::KeyLess;
template <typename KeyType, typename ValueType, typename Compare = KeyLess>
using SortedMap = core::SortedMap<KeyType, ValueType, Compare>;
} // namespace ludus::foundation
