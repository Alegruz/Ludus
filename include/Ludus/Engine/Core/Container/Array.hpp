#pragma once

#include <Ludus/Engine/Core/Container/Array.h>
#include <Ludus/Engine/Core/Memory.h>

#include <Ludus/Engine/Core/Math/Bit.hpp>

namespace ludus::core
{
    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    template<typename IteratorType>
        requires std::is_same_v<IteratorType, T> || std::is_same_v<IteratorType, const T>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::IteratorImpl<IteratorType>::IteratorImpl(IteratorType& start) noexcept
        : mCurrentOrNull(&start) {}

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    template<typename IteratorType>
        requires std::is_same_v<IteratorType, T> || std::is_same_v<IteratorType, const T>
    LUDUS_INLINE constexpr typename  ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::template IteratorImpl<IteratorType>& ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::IteratorImpl<IteratorType>::operator++() noexcept
    {
        ++mCurrentOrNull;
        return *this;
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    template<typename IteratorType>
        requires std::is_same_v<IteratorType, T> || std::is_same_v<IteratorType, const T>
    LUDUS_INLINE constexpr IteratorType& ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::IteratorImpl<IteratorType>::operator*() noexcept
        requires (!std::is_const_v<IteratorType>)
    {
        LUDUS_ASSERT_MSG(mCurrentOrNull != nullptr, "Dereferencing a null iterator.");
        return *mCurrentOrNull;
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    template<typename IteratorType>
        requires std::is_same_v<IteratorType, T> || std::is_same_v<IteratorType, const T>
    LUDUS_INLINE constexpr IteratorType& ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::IteratorImpl<IteratorType>::operator*() const noexcept
        requires (std::is_const_v<IteratorType>)
    {
        LUDUS_ASSERT_MSG(mCurrentOrNull != nullptr, "Dereferencing a null iterator.");
        return *mCurrentOrNull;
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    template<typename IteratorType>
        requires std::is_same_v<IteratorType, T> || std::is_same_v<IteratorType, const T>
    LUDUS_INLINE constexpr bool ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::IteratorImpl<IteratorType>::operator==(const IteratorImpl& other) const noexcept
    {
        return mCurrentOrNull == other.mCurrentOrNull;
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    template<typename IteratorType>
        requires std::is_same_v<IteratorType, T> || std::is_same_v<IteratorType, const T>
    LUDUS_INLINE constexpr bool ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::IteratorImpl<IteratorType>::operator!=(const IteratorImpl& other) const noexcept
    {
        return operator==(other) == false;
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /*= ArrayResizePolicy::DEFAULT*/>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ArrayImplBase() noexcept
        requires (ARRAY_TYPE == ArrayType::DYNAMIC)
        : mCapacity(StringCharType<T> ? INITIAL_CAPACITY + 1 : INITIAL_CAPACITY)
        , mSize(0)
        , mData(nullptr) {}

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /*= ArrayResizePolicy::DEFAULT*/>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ArrayImplBase() noexcept
        requires (ARRAY_TYPE == ArrayType::STATIC) = default;
    
    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /*= ArrayResizePolicy::DEFAULT*/>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ArrayImplBase(const uint32_t capacity) noexcept
        requires (ARRAY_TYPE == ArrayType::DYNAMIC && StringCharType<T>)
        : mCapacity(calculateCapacityToAllocate(capacity + 1))
        , mSize(0)
        , mData(nullptr) {}
        
    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /*= ArrayResizePolicy::DEFAULT*/>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ArrayImplBase(const uint32_t capacity) noexcept
        requires (ARRAY_TYPE == ArrayType::DYNAMIC && !StringCharType<T>)
        : mCapacity(calculateCapacityToAllocate(capacity))
        , mSize(0)
        , mData(nullptr) {}

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /*= ArrayResizePolicy::DEFAULT*/>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ArrayImplBase(const uint32_t size, const T& defaultValue) noexcept
        : ArrayImplBase()
    {
        Assign(size, defaultValue);
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /*= ArrayResizePolicy::DEFAULT*/>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ArrayImplBase(Iterator first, Iterator last) noexcept
        requires (ARRAY_TYPE == ArrayType::DYNAMIC)
        : ArrayImplBase()
    {
        Assign(first, last);
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /*= ArrayResizePolicy::DEFAULT*/>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ArrayImplBase(const T* array, uint32_t size) noexcept
        requires (ARRAY_TYPE == ArrayType::DYNAMIC)
        : ArrayImplBase()
    {
        Assign(array, size);
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /*= ArrayResizePolicy::DEFAULT*/>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ArrayImplBase(const T* str) noexcept
        requires (ARRAY_TYPE == ArrayType::DYNAMIC && StringCharType<T>)
        : ArrayImplBase()
    {
        Assign(str);
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /*= ArrayResizePolicy::DEFAULT*/>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ArrayImplBase(const ArrayImplBase& other) noexcept
        requires (ARRAY_TYPE == ArrayType::DYNAMIC && std::is_copy_constructible_v<T>)
        : mCapacity(other.mCapacity)
        , mSize(other.mSize)
    {
        if(other.mData != nullptr)
        {
            mData = memory::Allocate<T>(mCapacity);
            // Use placement new for non-trivial types, memcpy for trivial types
            if constexpr (std::is_trivially_copyable_v<T>)
            {
                memory::CopyMemory(mData, other.mData, mSize * sizeof(T));
            }
            else
            {
                for(uint32_t i = 0; i < mSize; ++i)
                {
                    new (&mData[i]) T(other.mData[i]);
                }
            }
        }
        else
        {
            mData = nullptr;
        }
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /*= ArrayResizePolicy::DEFAULT*/>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ArrayImplBase(const ArrayImplBase& other) noexcept
        requires (ARRAY_TYPE == ArrayType::STATIC && std::is_copy_constructible_v<T>)
        : mSize(other.mSize)
    {
        if(other.mData != nullptr)
        {
            memory::CopyMemory(mData, other.mData, mSize * sizeof(T));
        }
        else
        {
            mSize = 0;
        }
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /*= ArrayResizePolicy::DEFAULT*/>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ArrayImplBase(ArrayImplBase&& other) noexcept
        requires (ARRAY_TYPE == ArrayType::DYNAMIC)
        : mCapacity(other.mCapacity)
        , mSize(other.mSize)
        , mData(other.mData)
    {
        other.mCapacity = 0;
        other.mSize = 0;
        other.mData = nullptr;
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /*= ArrayResizePolicy::DEFAULT*/>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ArrayImplBase(ArrayImplBase&& other) noexcept
        requires (ARRAY_TYPE == ArrayType::STATIC)
        : mSize(other.mSize)
    {
        LUDUS_ASSERT_MSG(other.mData != nullptr, "Source array data is null in move constructor.");
        memory::CopyMemory(mData, other.mData, mSize * sizeof(T));
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /*= ArrayResizePolicy::DEFAULT*/>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ArrayImplBase(std::initializer_list<T> initList) noexcept
        : ArrayImplBase(static_cast<uint32_t>(initList.size()))
    {
        Assign(initList);
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /*= ArrayResizePolicy::DEFAULT*/>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::~ArrayImplBase() noexcept
        requires (ARRAY_TYPE == ArrayType::DYNAMIC)
    {
        if(mData != nullptr)
        {
            for(uint32_t i = 0; i < mSize; ++i)
            {
                mData[i].~T();
            }
            memory::Deallocate(mData);
            mSize = 0;
            mCapacity = 0;
        }
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /*= ArrayResizePolicy::DEFAULT*/>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::~ArrayImplBase() noexcept
        requires (ARRAY_TYPE == ArrayType::STATIC)
    {
        LUDUS_ASSERT_MSG(mData != nullptr, "Array data is null in static array destructor.");
        for(uint32_t i = 0; i < mSize; ++i)
        {
            mData[i].~T();
        }
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /*= ArrayResizePolicy::DEFAULT*/>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>& ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::operator=(const ArrayImplBase& other) noexcept
        requires std::is_copy_constructible_v<T>
    {
        if(this != &other)
        {
            if constexpr (ARRAY_TYPE == ArrayType::DYNAMIC)
            {
                destroyAndDeallocate();
                mCapacity = other.mCapacity;
                mSize = other.mSize;
                if(other.mData != nullptr)
                {
                    mData = memory::Allocate<T>(mCapacity);
                    // Use placement new for non-trivial types, memcpy for trivial types
                    if constexpr (std::is_trivially_copyable_v<T>)
                    {
                        memory::CopyMemory(mData, other.mData, mSize * sizeof(T));
                    }
                    else
                    {
                        for(uint32_t i = 0; i < mSize; ++i)
                        {
                            new (&mData[i]) T(other.mData[i]);
                        }
                    }
                }
                else
                {
                    mData = nullptr;
                }
            }
            else if constexpr (ARRAY_TYPE == ArrayType::STATIC)
            {
                mSize = other.mSize;
                LUDUS_ASSERT_MSG(other.mData != nullptr, "Source array data is null in copy assignment.");
                memory::CopyMemory(mData, other.mData, mSize * sizeof(T));
            }
        }
        return *this;
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /*= ArrayResizePolicy::DEFAULT*/>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>& ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::operator=(ArrayImplBase&& other) noexcept
    {
        if(this != &other)
        {
            if constexpr (ARRAY_TYPE == ArrayType::DYNAMIC)
            {
                destroyAndDeallocate();
                mCapacity = other.mCapacity;
                mSize = other.mSize;
                mData = other.mData;

                other.mCapacity = 0;
                other.mSize = 0;
                other.mData = nullptr;
            }
            else if constexpr (ARRAY_TYPE == ArrayType::STATIC)
            {
                mSize = other.mSize;
                LUDUS_ASSERT_MSG(other.mData != nullptr, "Source array data is null in move assignment.");
                memory::CopyMemory(mData, other.mData, mSize * sizeof(T));
            }
        }
        return *this;
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /*= ArrayResizePolicy::DEFAULT*/>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>& ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::operator=(std::initializer_list<T> initList) noexcept
    {
        Assign(initList);
        return *this;
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /*= ArrayResizePolicy::DEFAULT*/>
    LUDUS_INLINE constexpr void ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::Assign(const uint32_t size, const T& defaultValue) noexcept
    {
        const uint32_t logicalSize = size;
        const uint32_t requiredSize = logicalSize + (StringCharType<T> ? 1U : 0U);
        if constexpr (ARRAY_TYPE == ArrayType::DYNAMIC)
        {
            SetCapacity(requiredSize);
        }

        const uint32_t remainder = logicalSize % 4;
        const uint32_t vectorizedEnd = logicalSize - remainder;
        
        // Process four elements at a time
        for(uint32_t i = 0; i < vectorizedEnd; i += 4)
        {
            mData[i + 0] = defaultValue;
            mData[i + 1] = defaultValue;
            mData[i + 2] = defaultValue;
            mData[i + 3] = defaultValue;
        }
        
        // Handle remainder elements
        switch(remainder)
        {
            case 3: mData[vectorizedEnd + 2] = defaultValue; [[fallthrough]];
            case 2: mData[vectorizedEnd + 1] = defaultValue; [[fallthrough]];
            case 1: mData[vectorizedEnd + 0] = defaultValue; break;
            default: break;
        }

        if constexpr (StringCharType<T>)
        {
            mData[logicalSize] = STRING_NULL_CHAR(T{});
        }
        mSize = requiredSize;
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr void ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::Assign(Iterator first, Iterator last) noexcept
        requires (ARRAY_TYPE == ArrayType::DYNAMIC)
    {
        uint32_t logicalSize = 0;
        for(auto it = first; it != last; ++it)
        {
            ++logicalSize;
        }

        const uint32_t requiredSize = logicalSize + (StringCharType<T> ? 1U : 0U);
        SetCapacity(requiredSize);
        uint32_t index = 0;
        for(auto it = first; it != last; ++it)
        {
            mData[index++] = *it;
        }

        if constexpr (StringCharType<T>)
        {
            mData[logicalSize] = STRING_NULL_CHAR(T{});
            mSize = requiredSize;
        }
        else
        {
            mSize = logicalSize;
        }
    }
    
    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr void ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::Assign(const T* array, uint32_t size) noexcept
        requires (ARRAY_TYPE == ArrayType::DYNAMIC)
    {
        if(array != nullptr && size > 0)
        {
            const uint32_t requiredSize = size + (StringCharType<T> ? 1U : 0U);
            SetCapacity(requiredSize);
            
            // Use placement new for non-trivial types, memcpy for trivial types
            if constexpr (std::is_trivially_copyable_v<T>)
            {
                memory::CopyMemory(mData, array, size * sizeof(T));
            }
            else
            {
                for(uint32_t i = 0; i < size; ++i)
                {
                    new (&mData[i]) T(array[i]);
                }
            }
            
            if constexpr (StringCharType<T>)
            {
                mData[size] = STRING_NULL_CHAR(T{});
            }
            mSize = requiredSize;
        }
        else if constexpr (StringCharType<T>)
        {
            SetCapacity(1);
            mData[0] = STRING_NULL_CHAR(T{});
            mSize = 1;
        }
        else
        {
            mSize = 0;
        }
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr void ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::Assign(const T* str) noexcept
        requires (ARRAY_TYPE == ArrayType::DYNAMIC && StringCharType<T>)
    {
        if(str != nullptr)
        {
            const uint32_t length = GetStringLength(str);

            Assign(str, length);
        }
        else
        {
            SetCapacity(1);
            mData[0] = STRING_NULL_CHAR(T{});
            mSize = 1;
        }
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /*= ArrayResizePolicy::DEFAULT*/>
    LUDUS_INLINE constexpr void ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::Assign(std::initializer_list<T> initList) noexcept
    {
        const uint32_t logicalSize = static_cast<uint32_t>(initList.size());
        const uint32_t requiredSize = logicalSize + (StringCharType<T> ? 1U : 0U);
        if constexpr (ARRAY_TYPE == ArrayType::DYNAMIC)
        {
            SetCapacity(requiredSize);
        }

        mSize = logicalSize;
        copy(initList);

        if constexpr (StringCharType<T>)
        {
            mData[logicalSize] = STRING_NULL_CHAR(T{});
            mSize = requiredSize;
        }
        else
        {
            mSize = logicalSize;
        }
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /*= ArrayResizePolicy::DEFAULT*/>
    LUDUS_INLINE constexpr uint32_t ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::GetSize() const noexcept
    {
        if constexpr (ARRAY_TYPE == ArrayType::DYNAMIC)
        {
            if constexpr (StringCharType<T>)
            {
                return mSize > 0 ? mSize - 1 : 0;
            }
            else
            {
                return mSize;
            }
        }
        else
        {
            return mSize;
        }
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr uint32_t ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::GetLength() const noexcept
        requires (ARRAY_TYPE == ArrayType::DYNAMIC && StringCharType<T>)
    {
        return GetSize();
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr uint32_t ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::GetCapacity() const noexcept
    {
        if constexpr (ARRAY_TYPE == ArrayType::DYNAMIC)
        {
            if constexpr (StringCharType<T>)
            {
                return mCapacity > 0 ? mCapacity - 1 : 0;
            }
            else
            {
                return mCapacity;
            }
        }
        else
        {
            return STATIC_CAPACITY;
        }
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr uint32_t ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::calculateCapacityToAllocate(const uint32_t requiredCapacity) const noexcept
        requires (ARRAY_TYPE == ArrayType::DYNAMIC)
    {
        uint32_t newCapacity = mCapacity;
        if constexpr (RESIZE_POLICY == ArrayResizePolicy::DOUBLE)
        {
            newCapacity = GetNextPowerOfTwo(requiredCapacity);
        }
        else if constexpr (RESIZE_POLICY == ArrayResizePolicy::FIXED_INCREMENT)
        {
            newCapacity += ((requiredCapacity - newCapacity + FIXED_INCREMENT - 1) / FIXED_INCREMENT) * FIXED_INCREMENT;
        }
        return newCapacity;
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr void ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::copy(std::initializer_list<T> initList) noexcept
    {
        auto it = initList.begin();
        
        if constexpr (std::is_trivially_copyable_v<T>)
        {
            // Vectorized copy for trivial types
            const uint32_t remainder = mSize % 4;
            const uint32_t vectorizedEnd = mSize - remainder;
            
            // Process four elements at a time
            for(uint32_t i = 0; i < vectorizedEnd; i += 4)
            {
                mData[i + 0] = *it++;
                mData[i + 1] = *it++;
                mData[i + 2] = *it++;
                mData[i + 3] = *it++;
            }
            
            // Handle remainder elements
            switch(remainder)
            {
                case 3: mData[vectorizedEnd + 2] = *it++; [[fallthrough]];
                case 2: mData[vectorizedEnd + 1] = *it++; [[fallthrough]];
                case 1: mData[vectorizedEnd + 0] = *it++; break;
                default: break;
            }
        }
        else
        {
            // Use placement new for non-trivial types
            for(uint32_t i = 0; i < mSize; ++i)
            {
                new (&mData[i]) T(*it++);
            }
        }
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr void ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::updateSize(const uint32_t newSize) noexcept
        requires (ARRAY_TYPE == ArrayType::DYNAMIC)
    {
        if constexpr (StringCharType<T>)
        {
            const uint32_t requiredSize = newSize + 1U;
            LUDUS_ASSERT_MSG(requiredSize <= mCapacity, "String capacity insufficient during resize");
            mSize = requiredSize;
            mData[newSize] = STRING_NULL_CHAR(T{});
        }
        else
        {
            mSize = newSize;
        }
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr void ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::destroyAndDeallocate() noexcept
        requires (ARRAY_TYPE == ArrayType::DYNAMIC)
    {
        if(mData != nullptr)
        {
            // Call destructors on all existing elements
            for(uint32_t i = 0; i < mSize; ++i)
            {
                mData[i].~T();
            }
            memory::Deallocate(mData);
            mData = nullptr;
            mSize = 0;
            mCapacity = 0;
        }
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ArrayImpl() noexcept = default;
    
    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ArrayImpl(const uint32_t capacity) noexcept 
        requires (ARRAY_TYPE == ArrayType::DYNAMIC)
        // cppcheck-suppress missingReturn
        : ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>(capacity) {}

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ArrayImpl(const uint32_t size, const T& defaultValue) noexcept 
        : ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>(size, defaultValue) {}

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ArrayImpl(ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::Iterator first, ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::Iterator last) noexcept
        requires (ARRAY_TYPE == ArrayType::DYNAMIC)
        // cppcheck-suppress missingReturn
        : ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>(first, last) {}

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ArrayImpl(const T* array, uint32_t size) noexcept
        requires (ARRAY_TYPE == ArrayType::DYNAMIC)
        // cppcheck-suppress missingReturn
        : ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>(array, size) {}

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ArrayImpl(const T* str) noexcept
        requires (ARRAY_TYPE == ArrayType::DYNAMIC && StringCharType<T>)
        // cppcheck-suppress missingReturn
        : ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>(str) {}

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ArrayImpl(const ArrayImpl& other) noexcept = default;

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ArrayImpl(ArrayImpl&& other) noexcept = default;

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ArrayImpl(std::initializer_list<T> initList) noexcept
        : ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>(initList) {}

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::~ArrayImpl() noexcept = default;

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>& ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::operator=(const ArrayImpl& other) noexcept = default;

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>& ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::operator=(ArrayImpl&& other) noexcept = default;

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    // cppcheck-suppress duplInheritedMember ; Intentional override to return derived type instead of base type
    LUDUS_INLINE constexpr ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>& ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::operator=(std::initializer_list<T> initList) noexcept
    {
        Base::operator=(initList);
        return *this;
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr T& ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::At(const uint32_t index) noexcept
    {
        LUDUS_ASSERT_MSG(index < this->GetSize(), "Index out of bounds in Array::At()");
        return this->mData[index];
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr const T& ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::At(const uint32_t index) const noexcept
    {
        LUDUS_ASSERT_MSG(index < this->GetSize(), "Index out of bounds in Array::At()");
        return this->mData[index];
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr T& ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::operator[](const uint32_t index) noexcept
    {
        return At(index);
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr const T& ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::operator[](const uint32_t index) const noexcept
    {
        return At(index);
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr T& ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::GetFront() noexcept
    {
        LUDUS_ASSERT_MSG(this->GetSize() > 0, "Array is empty in GetFront()");
        return this->mData[0];
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr const T& ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::GetFront() const noexcept
    {
        LUDUS_ASSERT_MSG(this->GetSize() > 0, "Array is empty in GetFront()");
        return this->mData[0];
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr T& ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::GetBack() noexcept
    {
        LUDUS_ASSERT_MSG(this->GetSize() > 0, "Array is empty in GetBack()");
        return this->mData[this->GetSize() - 1];
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr const T& ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::GetBack() const noexcept
    {
        LUDUS_ASSERT_MSG(this->GetSize() > 0, "Array is empty in GetBack()");
        return this->mData[this->GetSize() - 1];
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    // cppcheck-suppress duplInheritedMember ; Intentional override to add runtime assertion for safety
    LUDUS_INLINE constexpr T* ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::GetData() noexcept
    {
        T* data = Base::GetData();
        LUDUS_ASSERT_MSG(data != nullptr, "Array data is null in GetData()");
        return data;
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    // cppcheck-suppress duplInheritedMember ; Intentional override to add runtime assertion for safety
    LUDUS_INLINE constexpr const T* ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::GetData() const noexcept
    {
        const T* data = Base::GetData();
        LUDUS_ASSERT_MSG(data != nullptr, "Array data is null in GetData()");
        return data;
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr const T* ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::GetCStr() const noexcept
        requires (StringCharType<T>)
    {
        const T* data = Base::GetCStr();
        LUDUS_ASSERT_MSG(data != nullptr, "String is null in GetCStr()");
        return data;
    }
    
    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr T* ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::GetData() noexcept
    {
        if(mData == nullptr)
        {
            SetCapacity(mCapacity);
        }
        LUDUS_ASSERT_MSG(mData != nullptr, "Array data is null in GetData()");
        return mData;
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr const T* ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::GetData() const noexcept
    {
        LUDUS_ASSERT_MSG(mData != nullptr, "Array data is null in GetData()");
        return mData;
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr const T* ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::GetCStr() const noexcept
        requires (StringCharType<T>)
    {
        if(mData != nullptr)
        {
            return mData;
        }
        
        // NOLINTNEXTLINE(readability-identifier-naming) - Local function static, not a constant
        static const T emptyString[] = { static_cast<T>(0) };
        return emptyString;
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::Iterator ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::begin() noexcept    // NOLINT(readability-identifier-naming)
    {
        if(this->GetSize() == 0)
        {
            return end();
        }
        return Iterator(this->mData[0]);
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ConstIterator ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::begin() const noexcept  // NOLINT(readability-identifier-naming)
    {
        if(this->GetSize() == 0)
        {
            return cend();
        }
        return ConstIterator(this->mData[0]);
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ConstIterator ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::cbegin() const noexcept    // NOLINT(readability-identifier-naming)
    {
        if(this->GetSize() == 0)
        {
            return cend();
        }
        return ConstIterator(this->mData[0]);
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    // cppcheck-suppress functionConst ; end() returns mutable iterator, cannot be const
// cppcheck-suppress functionConst
LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::Iterator ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::end() noexcept  // NOLINT(readability-identifier-naming)
    {
        return Iterator(*(this->mData + this->GetSize()));
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ConstIterator ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::end() const noexcept   // NOLINT(readability-identifier-naming)
    {
        return ConstIterator(*(this->mData + this->GetSize()));
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ConstIterator ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::cend() const noexcept  // NOLINT(readability-identifier-naming)
    {
        return ConstIterator(*(this->mData + this->GetSize()));
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr bool ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::IsEmpty() const noexcept
    {
        return this->GetSize() == 0;
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr void ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::SetCapacity(uint32_t capacity) noexcept
        requires (ARRAY_TYPE == ArrayType::DYNAMIC)
    {
        if(capacity <= mCapacity)
        {
            if(mData == nullptr)
            {
                mData = memory::Allocate<T>(mCapacity);
                memory::ZeroOutMemory(mData, mCapacity * sizeof(T));
            }
            return;
        }

        capacity = calculateCapacityToAllocate(capacity);

        T* newData = memory::Allocate<T>(capacity);
        if(mData != nullptr)
        {
            // Use placement new for non-trivial types, memcpy for trivial types
            if constexpr (std::is_trivially_copyable_v<T>)
            {
                memory::CopyMemory(newData, mData, mSize * sizeof(T));
            }
            else
            {
                for(uint32_t i = 0; i < mSize; ++i)
                {
                    new (&newData[i]) T(std::move(mData[i]));
                    mData[i].~T();
                }
            }
            memory::Deallocate(mData);
        }
        
        memory::ZeroOutMemory(newData + mSize, (capacity - mSize) * sizeof(T));
        mData = newData;
        mCapacity = capacity;
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>& ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::Append(const T* array, const uint32_t size) noexcept
        requires (ARRAY_TYPE == ArrayType::DYNAMIC)
    {
        if(array != nullptr && size > 0)
        {
            const uint32_t nextSize = this->GetSize() + size;
            const uint32_t requiredCapacity = nextSize + (StringCharType<T> ? 1U : 0U);
            this->SetCapacity(requiredCapacity);
            memory::CopyMemory(&this->mData[this->GetSize()], array, size * sizeof(T));
            this->updateSize(nextSize);
        }
        return *this;
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr void ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::PushBack(const T& element) noexcept
        requires (ARRAY_TYPE == ArrayType::DYNAMIC)
    {
        const uint32_t nextSize = this->GetSize() + 1;
        const uint32_t requiredCapacity = nextSize + (StringCharType<T> ? 1U : 0U);
        this->SetCapacity(requiredCapacity);
        this->mData[this->GetSize()] = element;
        this->updateSize(nextSize);
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr void ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::PushBack(T&& element) noexcept
        requires (ARRAY_TYPE == ArrayType::DYNAMIC)
    {
        const uint32_t nextSize = this->GetSize() + 1;
        const uint32_t requiredCapacity = nextSize + (StringCharType<T> ? 1U : 0U);
        this->SetCapacity(requiredCapacity);
        (void)(this->mData[this->GetSize()] = std::move(element));
        this->updateSize(nextSize);
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr void ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::PopBack() noexcept
        requires (ARRAY_TYPE == ArrayType::DYNAMIC)
    {
        LUDUS_ASSERT_MSG(this->GetSize() > 0, "Cannot PopBack from an empty array");
        const uint32_t newSize = this->GetSize() - 1;
        // Explicitly call destructor for the element being removed
        if constexpr (!std::is_trivially_destructible_v<T>)
        {
            this->mData[newSize].~T();
        }
        this->updateSize(newSize);
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr bool operator==(const ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>& lhs, const ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>& rhs) noexcept
    {
        if(lhs.GetSize() != rhs.GetSize())
        {
            return false;
        }

        for(uint32_t i = 0; i < lhs.GetSize(); ++i)
        {
            if(!(lhs[i] == rhs[i]))
            {
                return false;
            }
        }
        return true;
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr bool operator!=(const ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>& lhs, const ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>& rhs) noexcept
    {
        return !(lhs == rhs);
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr bool operator==(const ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>& lhs, const T* rhs) noexcept
        requires (StringCharType<T>)
    {
        if(rhs == nullptr)
        {
            return lhs.GetSize() == 0;
        }

        const uint32_t rhsLength = GetStringLength(rhs);
        if(lhs.GetSize() != rhsLength)
        {
            return false;
        }

        for(uint32_t i = 0; i < lhs.GetSize(); ++i)
        {
            if(!(lhs[i] == rhs[i]))
            {
                return false;
            }
        }
        return true;
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr bool operator==(const T* lhs, const ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>& rhs) noexcept
        requires (StringCharType<T>)
    {
        return rhs == lhs;
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::DEFAULT */>
    LUDUS_INLINE constexpr bool operator!=(const T* lhs, const ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>& rhs) noexcept
        requires (StringCharType<T>)
    {
        return !(lhs == rhs);
    }
}   // namespace ludus::core
