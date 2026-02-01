#pragma once

#include <Ludus/Engine/Core/Container/HashSet.h>
#include <Ludus/Engine/Core/Container/HashMap.hpp>

namespace ludus::core
{
    template<typename Key, typename Hasher, typename KeyEqual>
    LUDUS_INLINE constexpr HashSet<Key, Hasher, KeyEqual>::HashSet(uint32_t capacity, const Hasher& hasher, const KeyEqual& keyEqual) noexcept
        : mMap(capacity, hasher, keyEqual)
    {
    }

    template<typename Key, typename Hasher, typename KeyEqual>
    LUDUS_INLINE constexpr void HashSet<Key, Hasher, KeyEqual>::SetCapacity(uint32_t capacity) noexcept
    {
        mMap.SetCapacity(capacity);
    }

    template<typename Key, typename Hasher, typename KeyEqual>
    LUDUS_INLINE constexpr void HashSet<Key, Hasher, KeyEqual>::Clear() noexcept
    {
        mMap.Clear();
    }

    template<typename Key, typename Hasher, typename KeyEqual>
    LUDUS_INLINE constexpr uint32_t HashSet<Key, Hasher, KeyEqual>::GetSize() const noexcept
    {
        return mMap.GetSize();
    }

    template<typename Key, typename Hasher, typename KeyEqual>
    LUDUS_INLINE constexpr bool HashSet<Key, Hasher, KeyEqual>::IsEmpty() const noexcept
    {
        return mMap.IsEmpty();
    }

    template<typename Key, typename Hasher, typename KeyEqual>
    LUDUS_INLINE constexpr bool HashSet<Key, Hasher, KeyEqual>::Insert(const Key& key) noexcept
    {
        return mMap.Insert(key, HashSetValue{});
    }

    template<typename Key, typename Hasher, typename KeyEqual>
    LUDUS_INLINE constexpr bool HashSet<Key, Hasher, KeyEqual>::Insert(Key&& key) noexcept
    {
        return mMap.Insert(std::move(key), HashSetValue{});
    }

    template<typename Key, typename Hasher, typename KeyEqual>
    LUDUS_INLINE constexpr bool HashSet<Key, Hasher, KeyEqual>::Remove(const Key& key) noexcept
    {
        return mMap.Remove(key);
    }

    template<typename Key, typename Hasher, typename KeyEqual>
    LUDUS_INLINE constexpr bool HashSet<Key, Hasher, KeyEqual>::Contains(const Key& key) const noexcept
    {
        return mMap.Contains(key);
    }

    template<typename Key, typename Hasher, typename KeyEqual>
    LUDUS_INLINE constexpr const Key* HashSet<Key, Hasher, KeyEqual>::Find(const Key& key) const noexcept
    {
        return mMap.FindKey(key);
    }
}
