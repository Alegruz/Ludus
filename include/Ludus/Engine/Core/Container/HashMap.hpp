#pragma once

#include <Ludus/Engine/Core/Container/HashMap.h>
#include <Ludus/Engine/Core/Container/String.h>
#include <Ludus/Engine/Core/Assert.h>
#include <Ludus/Engine/Core/Memory.h>
#include <Ludus/Engine/Core/Math/Bit.hpp>

#include <algorithm>
#include <memory>

namespace ludus::core
{
    namespace detail
    {
        LUDUS_INLINE constexpr size_t HashBytes(const uint8_t* data, size_t length) noexcept
        {
            if (data == nullptr || length == 0)
            {
                return 0u;
            }

            constexpr uint64_t FNV_OFFSET = 14695981039346656037ull;
            constexpr uint64_t FNV_PRIME = 1099511628211ull;

            uint64_t hash = FNV_OFFSET;
            for (size_t i = 0; i < length; ++i)
            {
                hash ^= static_cast<uint64_t>(data[i]);
                hash *= FNV_PRIME;
            }

            return static_cast<size_t>(hash);
        }

        template<StringCharType CharT>
        LUDUS_INLINE constexpr size_t HashStringData(const CharT* data, uint32_t length) noexcept
        {
            return HashBytes(reinterpret_cast<const uint8_t*>(data), static_cast<size_t>(length) * sizeof(CharT));
        }
    }

    template<StringCharType CharT>
    LUDUS_INLINE constexpr size_t HashCString<CharT>::operator()(const CharT* str) const noexcept
    {
        if (str == nullptr)
        {
            return 0u;
        }

        const uint32_t length = GetStringLength(str);
        return detail::HashStringData(str, length);
    }

    template<>
    struct Hash<String> final
    {
        [[nodiscard]] LUDUS_INLINE constexpr size_t operator()(const String& value) const noexcept
        {
            return detail::HashStringData(value.GetData(), value.GetLength());
        }
    };

    template<>
    struct Hash<WString> final
    {
        [[nodiscard]] LUDUS_INLINE constexpr size_t operator()(const WString& value) const noexcept
        {
            return detail::HashStringData(value.GetData(), value.GetLength());
        }
    };

    template<typename Key, typename Value, typename Hasher, typename KeyEqual>
    LUDUS_INLINE constexpr HashMap<Key, Value, Hasher, KeyEqual>::HashMap(uint32_t capacity, const Hasher& hasher, const KeyEqual& keyEqual) noexcept
        : mHasher(hasher)
        , mKeyEqual(keyEqual)
        , mEntries(nullptr)
        , mStates(nullptr)
        , mSize(0)
        , mTombstones(0)
        , mCapacity(0)
        , mMask(0)
    {
        static_assert(std::is_move_constructible_v<Key>, "HashMap Key must be move constructible.");
        static_assert(std::is_move_constructible_v<Value>, "HashMap Value must be move constructible.");

        rehashBuckets(calculateCapacityForSize(capacity == 0 ? DEFAULT_CAPACITY : capacity));
    }

    template<typename Key, typename Value, typename Hasher, typename KeyEqual>
    LUDUS_INLINE constexpr HashMap<Key, Value, Hasher, KeyEqual>::HashMap(std::initializer_list<std::pair<Key, Value>> initList, const Hasher& hasher, const KeyEqual& keyEqual) noexcept
        : HashMap(static_cast<uint32_t>(initList.size()), hasher, keyEqual)
    {
        for (const auto& pair : initList)
        {
            Insert(pair.first, pair.second);
        }
    }

    template<typename Key, typename Value, typename Hasher, typename KeyEqual>
    LUDUS_INLINE constexpr HashMap<Key, Value, Hasher, KeyEqual>::HashMap(const HashMap& other) noexcept
        requires (std::is_copy_constructible_v<Key> && std::is_copy_constructible_v<Value>)
        : HashMap(other.mCapacity, other.mHasher, other.mKeyEqual)
    {
        for (uint32_t index = 0; index < other.mCapacity; ++index)
        {
            if (other.mStates[index] == SlotState::OCCUPIED)
            {
                const Entry& entry = other.mEntries[index];
                Insert(entry.KeyValue, entry.ValueValue);
            }
        }
    }

    template<typename Key, typename Value, typename Hasher, typename KeyEqual>
    LUDUS_INLINE constexpr HashMap<Key, Value, Hasher, KeyEqual>::HashMap(HashMap&& other) noexcept
        : mHasher(std::move(other.mHasher))
        , mKeyEqual(std::move(other.mKeyEqual))
        , mEntries(other.mEntries)
        , mStates(other.mStates)
        , mSize(other.mSize)
        , mTombstones(other.mTombstones)
        , mCapacity(other.mCapacity)
        , mMask(other.mMask)
    {
        other.mEntries = nullptr;
        other.mStates = nullptr;
        other.mSize = 0;
        other.mTombstones = 0;
        other.mCapacity = 0;
        other.mMask = 0;
    }

    template<typename Key, typename Value, typename Hasher, typename KeyEqual>
    LUDUS_INLINE constexpr HashMap<Key, Value, Hasher, KeyEqual>::~HashMap() noexcept
    {
        Clear();
        memory::Deallocate(mEntries);
        memory::Deallocate(mStates);
    }

    template<typename Key, typename Value, typename Hasher, typename KeyEqual>
    LUDUS_INLINE constexpr HashMap<Key, Value, Hasher, KeyEqual>& HashMap<Key, Value, Hasher, KeyEqual>::operator=(const HashMap& other) noexcept
        requires (std::is_copy_constructible_v<Key> && std::is_copy_constructible_v<Value>)
    {
        if (this == &other)
        {
            return *this;
        }

        Clear();
        rehashBuckets(other.mCapacity);

        for (uint32_t index = 0; index < other.mCapacity; ++index)
        {
            if (other.mStates[index] == SlotState::OCCUPIED)
            {
                const Entry& entry = other.mEntries[index];
                Insert(entry.KeyValue, entry.ValueValue);
            }
        }

        return *this;
    }

    template<typename Key, typename Value, typename Hasher, typename KeyEqual>
    LUDUS_INLINE constexpr HashMap<Key, Value, Hasher, KeyEqual>& HashMap<Key, Value, Hasher, KeyEqual>::operator=(HashMap&& other) noexcept
    {
        if (this == &other)
        {
            return *this;
        }

        Clear();
        memory::Deallocate(mEntries);
        memory::Deallocate(mStates);

        mHasher = std::move(other.mHasher);
        mKeyEqual = std::move(other.mKeyEqual);
        mEntries = other.mEntries;
        mStates = other.mStates;
        mSize = other.mSize;
        mTombstones = other.mTombstones;
        mCapacity = other.mCapacity;
        mMask = other.mMask;

        other.mEntries = nullptr;
        other.mStates = nullptr;
        other.mSize = 0;
        other.mTombstones = 0;
        other.mCapacity = 0;
        other.mMask = 0;

        return *this;
    }

    template<typename Key, typename Value, typename Hasher, typename KeyEqual>
    LUDUS_INLINE constexpr void HashMap<Key, Value, Hasher, KeyEqual>::SetCapacity(uint32_t capacity) noexcept
    {
        if (capacity <= mCapacity)
        {
            return;
        }

        rehashBuckets(calculateCapacityForSize(capacity));
    }

    template<typename Key, typename Value, typename Hasher, typename KeyEqual>
    LUDUS_INLINE constexpr void HashMap<Key, Value, Hasher, KeyEqual>::Clear() noexcept
    {
        if (mStates == nullptr)
        {
            return;
        }

        for (uint32_t index = 0; index < mCapacity; ++index)
        {
            if (mStates[index] == SlotState::OCCUPIED)
            {
                destroyEntry(index);
            }
            mStates[index] = SlotState::EMPTY;
        }

        mSize = 0;
        mTombstones = 0;
    }

    template<typename Key, typename Value, typename Hasher, typename KeyEqual>
    LUDUS_INLINE constexpr uint32_t HashMap<Key, Value, Hasher, KeyEqual>::GetSize() const noexcept
    {
        return mSize;
    }

    template<typename Key, typename Value, typename Hasher, typename KeyEqual>
    LUDUS_INLINE constexpr uint32_t HashMap<Key, Value, Hasher, KeyEqual>::GetCapacity() const noexcept
    {
        return mCapacity;
    }

    template<typename Key, typename Value, typename Hasher, typename KeyEqual>
    LUDUS_INLINE constexpr bool HashMap<Key, Value, Hasher, KeyEqual>::IsEmpty() const noexcept
    {
        return mSize == 0;
    }

    template<typename Key, typename Value, typename Hasher, typename KeyEqual>
    LUDUS_INLINE constexpr bool HashMap<Key, Value, Hasher, KeyEqual>::Insert(const Key& key, const Value& value) noexcept
    {
        maybeGrowForInsert();

        const SlotResult slot = findSlotForInsert(key);
        if (slot.Found)
        {
            return false;
        }

        new (&mEntries[slot.Index]) Entry{key, value};
        mStates[slot.Index] = SlotState::OCCUPIED;
        ++mSize;
        return true;
    }

    template<typename Key, typename Value, typename Hasher, typename KeyEqual>
    LUDUS_INLINE constexpr bool HashMap<Key, Value, Hasher, KeyEqual>::Insert(Key&& key, Value&& value) noexcept
    {
        maybeGrowForInsert();

        const SlotResult slot = findSlotForInsert(key);
        if (slot.Found)
        {
            return false;
        }

        new (&mEntries[slot.Index]) Entry{std::move(key), std::move(value)};
        mStates[slot.Index] = SlotState::OCCUPIED;
        ++mSize;
        return true;
    }

    template<typename Key, typename Value, typename Hasher, typename KeyEqual>
    LUDUS_INLINE constexpr bool HashMap<Key, Value, Hasher, KeyEqual>::InsertOrAssign(const Key& key, const Value& value) noexcept
    {
        maybeGrowForInsert();

        const SlotResult slot = findSlotForInsert(key);
        if (slot.Found)
        {
            mEntries[slot.Index].ValueValue = value;
            return false;
        }

        new (&mEntries[slot.Index]) Entry{key, value};
        mStates[slot.Index] = SlotState::OCCUPIED;
        ++mSize;
        return true;
    }

    template<typename Key, typename Value, typename Hasher, typename KeyEqual>
    LUDUS_INLINE constexpr bool HashMap<Key, Value, Hasher, KeyEqual>::InsertOrAssign(Key&& key, Value&& value) noexcept
    {
        maybeGrowForInsert();

        const SlotResult slot = findSlotForInsert(key);
        if (slot.Found)
        {
            mEntries[slot.Index].ValueValue = std::move(value);
            return false;
        }

        new (&mEntries[slot.Index]) Entry{std::move(key), std::move(value)};
        mStates[slot.Index] = SlotState::OCCUPIED;
        ++mSize;
        return true;
    }

    template<typename Key, typename Value, typename Hasher, typename KeyEqual>
    LUDUS_INLINE constexpr bool HashMap<Key, Value, Hasher, KeyEqual>::Remove(const Key& key) noexcept
    {
        const SlotResult slot = findSlot(key);
        if (!slot.Found)
        {
            return false;
        }

        destroyEntry(slot.Index);
        mStates[slot.Index] = SlotState::TOMBSTONE;
        --mSize;
        ++mTombstones;

        maybeRebuildAfterRemove();

        return true;
    }

    template<typename Key, typename Value, typename Hasher, typename KeyEqual>
    LUDUS_INLINE constexpr Value* HashMap<Key, Value, Hasher, KeyEqual>::Find(const Key& key) noexcept
    {
        const SlotResult slot = findSlot(key);
        if (!slot.Found)
        {
            return nullptr;
        }

        return &mEntries[slot.Index].ValueValue;
    }

    template<typename Key, typename Value, typename Hasher, typename KeyEqual>
    LUDUS_INLINE constexpr const Value* HashMap<Key, Value, Hasher, KeyEqual>::Find(const Key& key) const noexcept
    {
        const SlotResult slot = findSlot(key);
        if (!slot.Found)
        {
            return nullptr;
        }

        return &mEntries[slot.Index].ValueValue;
    }

    template<typename Key, typename Value, typename Hasher, typename KeyEqual>
    LUDUS_INLINE constexpr bool HashMap<Key, Value, Hasher, KeyEqual>::Contains(const Key& key) const noexcept
    {
        return findSlot(key).Found;
    }

    template<typename Key, typename Value, typename Hasher, typename KeyEqual>
    LUDUS_INLINE constexpr const Key* HashMap<Key, Value, Hasher, KeyEqual>::FindKey(const Key& key) const noexcept
    {
        const SlotResult slot = findSlot(key);
        if (!slot.Found)
        {
            return nullptr;
        }

        return &mEntries[slot.Index].KeyValue;
    }

    template<typename Key, typename Value, typename Hasher, typename KeyEqual>
    LUDUS_INLINE constexpr Value& HashMap<Key, Value, Hasher, KeyEqual>::At(const Key& key) noexcept
    {
        Value* value = Find(key);
        LUDUS_ASSERT_MSG(value != nullptr, "HashMap::At called with missing key.");
        return *value;
    }

    template<typename Key, typename Value, typename Hasher, typename KeyEqual>
    LUDUS_INLINE constexpr const Value& HashMap<Key, Value, Hasher, KeyEqual>::At(const Key& key) const noexcept
    {
        const Value* value = Find(key);
        LUDUS_ASSERT_MSG(value != nullptr, "HashMap::At called with missing key.");
        return *value;
    }

    template<typename Key, typename Value, typename Hasher, typename KeyEqual>
    LUDUS_INLINE constexpr Value& HashMap<Key, Value, Hasher, KeyEqual>::operator[](const Key& key) noexcept
        requires (std::is_default_constructible_v<Value>)
    {
        Value* value = Find(key);
        if (value != nullptr)
        {
            return *value;
        }

        Insert(key, Value{});
        return *Find(key);
    }

    template<typename Key, typename Value, typename Hasher, typename KeyEqual>
    LUDUS_INLINE constexpr uint32_t HashMap<Key, Value, Hasher, KeyEqual>::calculateCapacityForSize(uint32_t desiredSize) const noexcept
    {
        const uint64_t numerator = static_cast<uint64_t>(desiredSize) * 100U;
        const uint32_t minCapacity = static_cast<uint32_t>(numerator / MAX_LOAD_PERCENT + 1U);
        const uint32_t adjusted = std::max(DEFAULT_CAPACITY, minCapacity);
        return static_cast<uint32_t>(GetNextPowerOfTwo(adjusted));
    }

    template<typename Key, typename Value, typename Hasher, typename KeyEqual>
    LUDUS_INLINE constexpr void HashMap<Key, Value, Hasher, KeyEqual>::rehashBuckets(uint32_t newCapacity) noexcept
    {
        const uint32_t adjusted = std::max(DEFAULT_CAPACITY, newCapacity);
        const uint32_t targetCapacity = static_cast<uint32_t>(GetNextPowerOfTwo(adjusted));

        Entry* oldEntries = mEntries;
        SlotState* oldStates = mStates;
        const uint32_t oldCapacity = mCapacity;

        Entry* newEntries = memory::Allocate<Entry>(targetCapacity);
        SlotState* newStates = memory::Allocate<SlotState>(targetCapacity);

        if (newEntries == nullptr || newStates == nullptr)
        {
            memory::Deallocate(newEntries);
            memory::Deallocate(newStates);
            LUDUS_ASSERT_MSG(false, "HashMap allocation failed.");
            return;
        }

        mEntries = newEntries;
        mStates = newStates;

        for (uint32_t index = 0; index < targetCapacity; ++index)
        {
            mStates[index] = SlotState::EMPTY;
        }

        mSize = 0;
        mTombstones = 0;
        mCapacity = targetCapacity;
        mMask = targetCapacity - 1u;

        if (oldEntries != nullptr && oldStates != nullptr)
        {
            for (uint32_t index = 0; index < oldCapacity; ++index)
            {
                if (oldStates[index] == SlotState::OCCUPIED)
                {
                    Entry& entry = oldEntries[index];
                    Insert(std::move(entry.KeyValue), std::move(entry.ValueValue));
                    std::destroy_at(&entry);
                }
            }

            memory::Deallocate(oldEntries);
            memory::Deallocate(oldStates);
        }
    }

    template<typename Key, typename Value, typename Hasher, typename KeyEqual>
    LUDUS_INLINE constexpr typename HashMap<Key, Value, Hasher, KeyEqual>::SlotResult HashMap<Key, Value, Hasher, KeyEqual>::findSlot(const Key& key) const noexcept
    {
        if (mCapacity == 0)
        {
            return {};
        }

        const size_t hash = mHasher(key);
        uint32_t index = static_cast<uint32_t>(hash) & mMask;

        while (true)
        {
            const SlotState state = mStates[index];
            if (state == SlotState::EMPTY)
            {
                return {INVALID_INDEX, false};
            }

            if (state == SlotState::OCCUPIED && mKeyEqual(mEntries[index].KeyValue, key))
            {
                return {index, true};
            }

            index = nextIndex(index);
        }
    }

    template<typename Key, typename Value, typename Hasher, typename KeyEqual>
    LUDUS_INLINE constexpr typename HashMap<Key, Value, Hasher, KeyEqual>::SlotResult HashMap<Key, Value, Hasher, KeyEqual>::findSlotForInsert(const Key& key) noexcept
    {
        const size_t hash = mHasher(key);
        uint32_t index = static_cast<uint32_t>(hash) & mMask;
        uint32_t firstTombstone = INVALID_INDEX;

        while (true)
        {
            const SlotState state = mStates[index];
            if (state == SlotState::EMPTY)
            {
                const uint32_t target = (firstTombstone == INVALID_INDEX) ? index : firstTombstone;
                if (firstTombstone != INVALID_INDEX)
                {
                    --mTombstones;
                }
                return {target, false};
            }

            if (state == SlotState::TOMBSTONE)
            {
                if (firstTombstone == INVALID_INDEX)
                {
                    firstTombstone = index;
                }
            }
            else if (mKeyEqual(mEntries[index].KeyValue, key))
            {
                return {index, true};
            }

            index = nextIndex(index);
        }
    }

    template<typename Key, typename Value, typename Hasher, typename KeyEqual>
    LUDUS_INLINE constexpr uint32_t HashMap<Key, Value, Hasher, KeyEqual>::nextIndex(uint32_t index) const noexcept
    {
        return (index + 1u) & mMask;
    }

    template<typename Key, typename Value, typename Hasher, typename KeyEqual>
    LUDUS_INLINE constexpr void HashMap<Key, Value, Hasher, KeyEqual>::destroyEntry(uint32_t index) noexcept
    {
        std::destroy_at(&mEntries[index]);
    }

    template<typename Key, typename Value, typename Hasher, typename KeyEqual>
    LUDUS_INLINE constexpr void HashMap<Key, Value, Hasher, KeyEqual>::maybeGrowForInsert() noexcept
    {
        if (mCapacity == 0)
        {
            rehashBuckets(DEFAULT_CAPACITY);
            return;
        }

        const uint64_t projected = static_cast<uint64_t>(mSize + mTombstones + 1U) * 100U;
        if (projected > static_cast<uint64_t>(mCapacity) * MAX_LOAD_PERCENT)
        {
            rehashBuckets(mCapacity * 2u);
        }
    }

    template<typename Key, typename Value, typename Hasher, typename KeyEqual>
    LUDUS_INLINE constexpr void HashMap<Key, Value, Hasher, KeyEqual>::maybeRebuildAfterRemove() noexcept
    {
        if (mCapacity == 0)
        {
            return;
        }

        const uint64_t tombstonePercent = static_cast<uint64_t>(mTombstones) * 100U;
        if (tombstonePercent > static_cast<uint64_t>(mCapacity) * TOMBSTONE_REBUILD_PERCENT)
        {
            rehashBuckets(mCapacity);
            return;
        }

        if (mSize > 0 && mSize < (mCapacity / 4u) && mCapacity > DEFAULT_CAPACITY)
        {
            rehashBuckets(mCapacity / 2u);
        }
    }
}
