#pragma once

#include <Ludus/Engine/Core/Common.h>

#include <functional>
#include <initializer_list>
#include <type_traits>
#include <utility>

namespace ludus::core
{
    template<typename T>
    struct Hash final
    {
        [[nodiscard]] LUDUS_INLINE constexpr size_t operator()(const T& value) const noexcept
        {
            return std::hash<T>{}(value);
        }
    };

    template<StringCharType CharT>
    struct HashCString final
    {
        [[nodiscard]] LUDUS_INLINE constexpr size_t operator()(const CharT* str) const noexcept;
    };

    template<typename Key, typename Value, typename Hasher = Hash<Key>, typename KeyEqual = std::equal_to<Key>>
    class HashMap final
    {
    public:
        explicit constexpr HashMap(uint32_t capacity = DEFAULT_CAPACITY, const Hasher& hasher = {}, const KeyEqual& keyEqual = {}) noexcept;
        explicit constexpr HashMap(std::initializer_list<std::pair<Key, Value>> initList, const Hasher& hasher = {}, const KeyEqual& keyEqual = {}) noexcept;
        constexpr HashMap(const HashMap& other) noexcept requires (std::is_copy_constructible_v<Key> && std::is_copy_constructible_v<Value>);
        constexpr HashMap(HashMap&& other) noexcept;
        constexpr ~HashMap() noexcept;

        constexpr HashMap& operator=(const HashMap& other) noexcept requires (std::is_copy_constructible_v<Key> && std::is_copy_constructible_v<Value>);
        constexpr HashMap& operator=(HashMap&& other) noexcept;

        // Capacity
        constexpr void SetCapacity(uint32_t capacity) noexcept;
        constexpr void Clear() noexcept;
        [[nodiscard]] constexpr uint32_t GetSize() const noexcept;
        [[nodiscard]] constexpr uint32_t GetCapacity() const noexcept;
        [[nodiscard]] constexpr bool IsEmpty() const noexcept;

        // Modifiers
        constexpr bool Insert(const Key& key, const Value& value) noexcept;
        constexpr bool Insert(Key&& key, Value&& value) noexcept;
        constexpr bool InsertOrAssign(const Key& key, const Value& value) noexcept;
        constexpr bool InsertOrAssign(Key&& key, Value&& value) noexcept;
        constexpr bool Remove(const Key& key) noexcept;

        // Lookup
        [[nodiscard]] constexpr Value* Find(const Key& key) noexcept;
        [[nodiscard]] constexpr const Value* Find(const Key& key) const noexcept;
        [[nodiscard]] constexpr bool Contains(const Key& key) const noexcept;
        [[nodiscard]] constexpr const Key* FindKey(const Key& key) const noexcept;

        constexpr Value& At(const Key& key) noexcept;
        [[nodiscard]] constexpr const Value& At(const Key& key) const noexcept;
        constexpr Value& operator[](const Key& key) noexcept requires (std::is_default_constructible_v<Value>);

    private:
        enum class SlotState : uint8_t
        {
            EMPTY = 0,
            TOMBSTONE = 1,
            OCCUPIED = 2,
        };

        struct Entry final
        {
            Key KeyValue;
            Value ValueValue;
        };

        struct SlotResult final
        {
            uint32_t Index = INVALID_INDEX;
            bool Found = false;
        };

    private:
        static constexpr uint32_t DEFAULT_CAPACITY = 16;
        static constexpr uint32_t INVALID_INDEX = UINT32_MAX;
        static constexpr uint32_t MAX_LOAD_PERCENT = 70;
        static constexpr uint32_t TOMBSTONE_REBUILD_PERCENT = 25;

    private:
        [[nodiscard]] constexpr uint32_t calculateCapacityForSize(uint32_t desiredSize) const noexcept;
        constexpr void rehashBuckets(uint32_t newCapacity) noexcept;
        [[nodiscard]] constexpr SlotResult findSlot(const Key& key) const noexcept;
        [[nodiscard]] constexpr SlotResult findSlotForInsert(const Key& key) noexcept;
        [[nodiscard]] constexpr uint32_t nextIndex(uint32_t index) const noexcept;
        constexpr void destroyEntry(uint32_t index) noexcept;
        constexpr void maybeGrowForInsert() noexcept;
        constexpr void maybeRebuildAfterRemove() noexcept;

    private:
        [[no_unique_address]] Hasher mHasher;
        [[no_unique_address]] KeyEqual mKeyEqual;
        Entry* mEntries;
        SlotState* mStates;
        uint32_t mSize;
        uint32_t mTombstones;
        uint32_t mCapacity;
        uint32_t mMask;
    };
}   // namespace ludus::core
