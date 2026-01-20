#pragma once

#include <Ludus/Engine/Core/Container/Array.h>

#include <Ludus/Engine/Core/Math/Bit.hpp>

namespace ludus::core
{
    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    template<typename IteratorType>
        requires std::is_same_v<IteratorType, T> || std::is_same_v<IteratorType, const T>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::IteratorImpl<IteratorType>::IteratorImpl(IteratorType& start) noexcept
        : mCurrentOrNull(&start) {}

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    template<typename IteratorType>
        requires std::is_same_v<IteratorType, T> || std::is_same_v<IteratorType, const T>
    LUDUS_INLINE constexpr typename  ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::template IteratorImpl<IteratorType>& ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::IteratorImpl<IteratorType>::operator++() noexcept
    {
        ++mCurrentOrNull;
        return *this;
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    template<typename IteratorType>
        requires std::is_same_v<IteratorType, T> || std::is_same_v<IteratorType, const T>
    LUDUS_INLINE constexpr IteratorType& ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::IteratorImpl<IteratorType>::operator*() noexcept
        requires (!std::is_const_v<IteratorType>)
    {
        return *mCurrentOrNull;
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    template<typename IteratorType>
        requires std::is_same_v<IteratorType, T> || std::is_same_v<IteratorType, const T>
    LUDUS_INLINE constexpr IteratorType& ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::IteratorImpl<IteratorType>::operator*() const noexcept
        requires (std::is_const_v<IteratorType>)
    {
        return *mCurrentOrNull;
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    template<typename IteratorType>
        requires std::is_same_v<IteratorType, T> || std::is_same_v<IteratorType, const T>
    LUDUS_INLINE constexpr bool ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::IteratorImpl<IteratorType>::operator==(const IteratorImpl& other) const noexcept
    {
        return mCurrentOrNull == other.mCurrentOrNull;
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    template<typename IteratorType>
        requires std::is_same_v<IteratorType, T> || std::is_same_v<IteratorType, const T>
    LUDUS_INLINE constexpr bool ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::IteratorImpl<IteratorType>::operator!=(const IteratorImpl& other) const noexcept
    {
        return operator==(other) == false;
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /*= ArrayResizePolicy::Default*/>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ArrayImplBase() noexcept
        requires (ARRAY_TYPE == ArrayType::Dynamic)
        : mCapacity(0)
        , mSize(0)
        , mData(nullptr) {}

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /*= ArrayResizePolicy::Default*/>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ArrayImplBase() noexcept
        requires (ARRAY_TYPE == ArrayType::Static) = default;
    
    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /*= ArrayResizePolicy::Default*/>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ArrayImplBase(const uint32_t capacity) noexcept
        requires (ARRAY_TYPE == ArrayType::Dynamic && StringCharType<T>)
        : mCapacity(calculateCapacityToAllocate(capacity + 1))
        , mSize(0)
        , mData(nullptr) {}
        
    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /*= ArrayResizePolicy::Default*/>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ArrayImplBase(const uint32_t capacity) noexcept
        requires (ARRAY_TYPE == ArrayType::Dynamic && !StringCharType<T>)
        : mCapacity(calculateCapacityToAllocate(capacity))
        , mSize(0)
        , mData(nullptr) {}

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /*= ArrayResizePolicy::Default*/>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ArrayImplBase(const uint32_t size, const T& defaultValue) noexcept
        : ArrayImplBase()
    {
        Assign(size, defaultValue);
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /*= ArrayResizePolicy::Default*/>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ArrayImplBase(Iterator first, Iterator last) noexcept
        requires (ARRAY_TYPE == ArrayType::Dynamic)
        : ArrayImplBase()
    {
        Assign(first, last);
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /*= ArrayResizePolicy::Default*/>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ArrayImplBase(const T* array, uint32_t size) noexcept
        requires (ARRAY_TYPE == ArrayType::Dynamic)
        : ArrayImplBase()
    {
        Assign(array, size);
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /*= ArrayResizePolicy::Default*/>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ArrayImplBase(const T* str) noexcept
        requires (ARRAY_TYPE == ArrayType::Dynamic && StringCharType<T>)
        : ArrayImplBase()
    {
        Assign(str);
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /*= ArrayResizePolicy::Default*/>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ArrayImplBase(const ArrayImplBase& other) noexcept
        requires (ARRAY_TYPE == ArrayType::Dynamic)
        : mCapacity(other.mCapacity)
        , mSize(other.mSize)
    {
        if(other.mData != nullptr)
        {
            mData = new T[mCapacity];
            std::memcpy(mData, other.mData, mSize * sizeof(T));
        }
        else
        {
            mData = nullptr;
        }
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /*= ArrayResizePolicy::Default*/>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ArrayImplBase(const ArrayImplBase& other) noexcept
        requires (ARRAY_TYPE == ArrayType::Static)
        : mSize(other.mSize)
    {
        std::memcpy(mData, other.mData, mSize * sizeof(T));
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /*= ArrayResizePolicy::Default*/>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ArrayImplBase(ArrayImplBase&& other) noexcept
        requires (ARRAY_TYPE == ArrayType::Dynamic)
        : mCapacity(other.mCapacity)
        , mSize(other.mSize)
        , mData(other.mData)
    {
        other.mCapacity = 0;
        other.mSize = 0;
        other.mData = nullptr;
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /*= ArrayResizePolicy::Default*/>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ArrayImplBase(ArrayImplBase&& other) noexcept
        requires (ARRAY_TYPE == ArrayType::Static)
        : mSize(other.mSize)
    {
        std::memcpy(mData, other.mData, mSize * sizeof(T));
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /*= ArrayResizePolicy::Default*/>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ArrayImplBase(std::initializer_list<T> initList) noexcept
        : ArrayImplBase(static_cast<uint32_t>(initList.size()))
    {
        Assign(initList);
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /*= ArrayResizePolicy::Default*/>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::~ArrayImplBase() noexcept
        requires (ARRAY_TYPE == ArrayType::Dynamic)
    {
        if(mData != nullptr)
        {
            delete[] mData;
            mSize = 0;
            mCapacity = 0;
        }
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /*= ArrayResizePolicy::Default*/>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::~ArrayImplBase() noexcept
        requires (ARRAY_TYPE == ArrayType::Static) = default;

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /*= ArrayResizePolicy::Default*/>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>& ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::operator=(const ArrayImplBase& other) noexcept
    {
        if(this != &other)
        {
            if constexpr (ARRAY_TYPE == ArrayType::Dynamic)
            {
                if(mData != nullptr)
                {
                    delete[] mData;
                }
                mCapacity = other.mCapacity;
                mSize = other.mSize;
                if(other.mData != nullptr)
                {
                    mData = new T[mCapacity];
                    std::memcpy(mData, other.mData, mSize * sizeof(T));
                }
                else
                {
                    mData = nullptr;
                }
            }
            else if constexpr (ARRAY_TYPE == ArrayType::Static)
            {
                mSize = other.mSize;
                std::memcpy(mData, other.mData, mSize * sizeof(T));
            }
        }
        return *this;
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /*= ArrayResizePolicy::Default*/>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>& ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::operator=(ArrayImplBase&& other) noexcept
    {
        if(this != &other)
        {
            if constexpr (ARRAY_TYPE == ArrayType::Dynamic)
            {
                if(mData != nullptr)
                {
                    delete[] mData;
                }
                mCapacity = other.mCapacity;
                mSize = other.mSize;
                mData = other.mData;

                other.mCapacity = 0;
                other.mSize = 0;
                other.mData = nullptr;
            }
            else if constexpr (ARRAY_TYPE == ArrayType::Static)
            {
                mSize = other.mSize;
                std::memcpy(mData, other.mData, mSize * sizeof(T));
            }
        }
        return *this;
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /*= ArrayResizePolicy::Default*/>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>& ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::operator=(std::initializer_list<T> initList) noexcept
    {
        Assign(initList);
        return *this;
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /*= ArrayResizePolicy::Default*/>
    LUDUS_INLINE constexpr void ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::Assign(const uint32_t size, const T& defaultValue) noexcept
    {
        mSize = size;
        if constexpr (ARRAY_TYPE == ArrayType::Dynamic)
        {
            SetCapacity(size);
        }

        const uint32_t remainder = mSize % 4;
        const uint32_t vectorizedEnd = mSize - remainder;
        
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
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    LUDUS_INLINE constexpr void ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::Assign(Iterator first, Iterator last) noexcept
        requires (ARRAY_TYPE == ArrayType::Dynamic)
    {
        mSize = 0;
        for(auto it = first; it != last; ++it)
        {
            ++mSize;
        }

        SetCapacity(mSize);
        uint32_t index = 0;
        for(auto it = first; it != last; ++it)
        {
            mData[index++] = *it;
        }
    }
    
    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    LUDUS_INLINE constexpr void ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::Assign(const T* array, uint32_t size) noexcept
        requires (ARRAY_TYPE == ArrayType::Dynamic)
    {
        if(array != nullptr && size > 0)
        {
            if constexpr (StringCharType<T>)
            {
                ++size;
            }
            
            SetCapacity(size);
            std::memcpy(mData, array, size * sizeof(T));
            mSize = size;
        }
        else
        {
            mSize = 0;
        }
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    LUDUS_INLINE constexpr void ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::Assign(const T* str) noexcept
        requires (ARRAY_TYPE == ArrayType::Dynamic && StringCharType<T>)
    {
        if(str != nullptr)
        {
            const uint32_t length = GetStringLength(str);

            Assign(str, length);
        }
        else
        {
            mSize = 0;
        }
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /*= ArrayResizePolicy::Default*/>
    LUDUS_INLINE constexpr void ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::Assign(std::initializer_list<T> initList) noexcept
    {
        mSize = static_cast<uint32_t>(initList.size());
        if constexpr (ARRAY_TYPE == ArrayType::Dynamic)
        {
            SetCapacity(mSize);
        }

        copy(initList);
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /*= ArrayResizePolicy::Default*/>
    LUDUS_INLINE constexpr uint32_t ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::GetSize() const noexcept
    {
        if constexpr (ARRAY_TYPE == ArrayType::Dynamic)
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
            return STATIC_CAPACITY;
        }
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    LUDUS_INLINE constexpr uint32_t ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::GetLength() const noexcept
        requires (ARRAY_TYPE == ArrayType::Dynamic && StringCharType<T>)
    {
        return GetSize();
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    LUDUS_INLINE constexpr uint32_t ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::GetCapacity() const noexcept
    {
        if constexpr (ARRAY_TYPE == ArrayType::Dynamic)
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

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    LUDUS_INLINE constexpr uint32_t ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::calculateCapacityToAllocate(const uint32_t requiredCapacity) const noexcept
        requires (ARRAY_TYPE == ArrayType::Dynamic)
    {
        uint32_t newCapacity = mCapacity;
        if constexpr (RESIZE_POLICY == ArrayResizePolicy::Double)
        {
            newCapacity = GetNextPowerOfTwo(requiredCapacity);
        }
        else if constexpr (RESIZE_POLICY == ArrayResizePolicy::FixedIncrement)
        {
            newCapacity += ((requiredCapacity - newCapacity + FIXED_INCREMENT - 1) / FIXED_INCREMENT) * FIXED_INCREMENT;
        }
        return newCapacity;
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    LUDUS_INLINE constexpr void ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::copy(std::initializer_list<T> initList) noexcept
    {
        const uint32_t remainder = mSize % 4;
        const uint32_t vectorizedEnd = mSize - remainder;
        
        auto it = initList.begin();
        
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

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    LUDUS_INLINE constexpr void ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::updateSize(const uint32_t newSize) noexcept
        requires (ARRAY_TYPE == ArrayType::Dynamic)
    {
        mSize = newSize;
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    LUDUS_INLINE constexpr ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ArrayImpl() noexcept = default;
    
    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    LUDUS_INLINE constexpr ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ArrayImpl(const uint32_t capacity) noexcept 
        requires (ARRAY_TYPE == ArrayType::Dynamic)
        : ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>(capacity) {}

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    LUDUS_INLINE constexpr ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ArrayImpl(const uint32_t size, const T& defaultValue) noexcept 
        : ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>(size, defaultValue) {}

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    LUDUS_INLINE constexpr ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ArrayImpl(ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::Iterator first, ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::Iterator last) noexcept
        requires (ARRAY_TYPE == ArrayType::Dynamic)
        : ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>(first, last) {}

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    LUDUS_INLINE constexpr ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ArrayImpl(const T* array, uint32_t size) noexcept
        requires (ARRAY_TYPE == ArrayType::Dynamic)
        : ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>(array, size) {}

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    LUDUS_INLINE constexpr ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ArrayImpl(const T* str) noexcept
        requires (ARRAY_TYPE == ArrayType::Dynamic && StringCharType<T>)
        : ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>(str) {}

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    LUDUS_INLINE constexpr ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ArrayImpl(const ArrayImpl& other) noexcept = default;

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    LUDUS_INLINE constexpr ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ArrayImpl(ArrayImpl&& other) noexcept = default;

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    LUDUS_INLINE constexpr ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ArrayImpl(std::initializer_list<T> initList) noexcept
        : ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>(initList) {}

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    LUDUS_INLINE constexpr ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::~ArrayImpl() noexcept = default;

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    LUDUS_INLINE constexpr ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>& ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::operator=(const ArrayImpl& other) noexcept = default;

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    LUDUS_INLINE constexpr ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>& ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::operator=(ArrayImpl&& other) noexcept = default;

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    LUDUS_INLINE constexpr ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>& ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::operator=(std::initializer_list<T> initList) noexcept
    {
        Base::operator=(initList);
        return *this;
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    LUDUS_INLINE constexpr T& ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::At(const uint32_t index) noexcept
    {
        // Optionally, you can add bounds checking here
        return this->mData[index];
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    LUDUS_INLINE constexpr const T& ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::At(const uint32_t index) const noexcept
    {
        // Optionally, you can add bounds checking here
        return this->mData[index];
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    LUDUS_INLINE constexpr T& ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::operator[](const uint32_t index) noexcept
    {
        return this->mData[index];
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    LUDUS_INLINE constexpr const T& ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::operator[](const uint32_t index) const noexcept
    {
        return this->mData[index];
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    LUDUS_INLINE constexpr T& ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::GetFront() noexcept
    {
        return this->mData[0];
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    LUDUS_INLINE constexpr const T& ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::GetFront() const noexcept
    {
        return this->mData[0];
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    LUDUS_INLINE constexpr T& ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::GetBack() noexcept
    {
        return this->mData[this->GetSize() - 1];
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    LUDUS_INLINE constexpr const T& ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::GetBack() const noexcept
    {
        return this->mData[this->GetSize() - 1];
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    LUDUS_INLINE constexpr T* ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::GetData() noexcept
    {
        return Base::GetData();
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    LUDUS_INLINE constexpr const T* ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::GetData() const noexcept
    {
        return Base::GetData();
    }
    
    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    LUDUS_INLINE constexpr T* ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::GetData() noexcept
    {
        if constexpr (ARRAY_TYPE == ArrayType::Dynamic)
        {
            return mData;
        }
        else
        {
            return mData;
        }
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    LUDUS_INLINE constexpr const T* ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::GetData() const noexcept
    {
        if constexpr (ARRAY_TYPE == ArrayType::Dynamic)
        {
            return mData;
        }
        else
        {
            return mData;
        }
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::Iterator ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::begin() noexcept
    {
        return Iterator(this->mData[0]);
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ConstIterator ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::begin() const noexcept
    {
        return ConstIterator(this->mData[0]);
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ConstIterator ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::cbegin() const noexcept
    {
        return ConstIterator(this->mData[0]);
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::Iterator ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::end() noexcept
    {
        return Iterator(*(this->mData + this->GetSize()));
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ConstIterator ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::end() const noexcept
    {
        return ConstIterator(*(this->mData + this->GetSize()));
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    LUDUS_INLINE constexpr ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::ConstIterator ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::cend() const noexcept
    {
        return ConstIterator(*(this->mData + this->GetSize()));
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    LUDUS_INLINE constexpr bool ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::IsEmpty() const noexcept
    {
        return this->GetSize() == 0;
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    LUDUS_INLINE constexpr void ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::SetCapacity(uint32_t capacity) noexcept
        requires (ARRAY_TYPE == ArrayType::Dynamic)
    {
        if(capacity <= mCapacity && mData != nullptr)
        {
            return;
        }

        capacity = calculateCapacityToAllocate(capacity);

        T* newData = new T[capacity];
        if(mData != nullptr)
        {
            std::memcpy(newData, mData, mSize * sizeof(T));
            delete[] mData;
        }
        
        std::memset(newData + mSize, 0, (capacity - mSize) * sizeof(T));
        mData = newData;
        mCapacity = capacity;
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    LUDUS_INLINE constexpr ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>& ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::Append(const T* array, const uint32_t size) noexcept
        requires (ARRAY_TYPE == ArrayType::Dynamic)
    {
        if(array != nullptr && size > 0)
        {
            uint32_t nextSize = this->GetSize() + size;
            if constexpr (StringCharType<T>)
            {
                ++nextSize;
            }
            this->SetCapacity(this->calculateCapacityToAllocate(nextSize));
            std::memcpy(&this->mData[this->GetSize()], array, size * sizeof(T));
            this->updateSize(nextSize);
        }
        return *this;
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    LUDUS_INLINE constexpr void ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::PushBack(const T& element) noexcept
        requires (ARRAY_TYPE == ArrayType::Dynamic)
    {
        const uint32_t nextSize = this->GetSize() + 1;
        this->SetCapacity(this->calculateCapacityToAllocate(nextSize));
        this->mData[this->GetSize()] = element;
        this->updateSize(nextSize);
    }

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY /*= 0*/, ArrayResizePolicy RESIZE_POLICY /* = ArrayResizePolicy::Default */>
    LUDUS_INLINE constexpr void ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::PushBack(T&& element) noexcept
        requires (ARRAY_TYPE == ArrayType::Dynamic)
    {
        const uint32_t nextSize = this->GetSize() + 1;
        this->SetCapacity(this->calculateCapacityToAllocate(nextSize));
        (void)(this->mData[this->GetSize()] = std::move(element));
        this->updateSize(nextSize);
    }
}   // namespace ludus::core