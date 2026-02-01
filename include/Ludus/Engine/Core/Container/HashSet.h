#pragma once

#include <Ludus/Engine/Core/Container/HashMap.h>

#include <type_traits>

namespace ludus::core
{
    template<typename Key, typename Hasher = Hash<Key>, typename KeyEqual = std::equal_to<Key>>
    class HashSet final
    {
    public:
        explicit constexpr HashSet(uint32_t capacity = DEFAULT_CAPACITY, const Hasher& hasher = {}, const KeyEqual& keyEqual = {}) noexcept;

        // Capacity
        constexpr void SetCapacity(uint32_t capacity) noexcept;
        constexpr void Clear() noexcept;
        [[nodiscard]] constexpr uint32_t GetSize() const noexcept;
        [[nodiscard]] constexpr bool IsEmpty() const noexcept;

        // Modifiers
        constexpr bool Insert(const Key& key) noexcept;
        constexpr bool Insert(Key&& key) noexcept;
        constexpr bool Remove(const Key& key) noexcept;

        // Lookup
        [[nodiscard]] constexpr bool Contains(const Key& key) const noexcept;
        [[nodiscard]] constexpr const Key* Find(const Key& key) const noexcept;

    private:
        struct HashSetValue final
        {
            uint8_t Unused = 0;
        };

    private:
        static constexpr uint32_t DEFAULT_CAPACITY = 16;

    private:
        HashMap<Key, HashSetValue, Hasher, KeyEqual> mMap;
    };
}   // namespace ludus::core
