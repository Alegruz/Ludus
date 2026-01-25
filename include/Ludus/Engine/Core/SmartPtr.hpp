#pragma once

#include <Ludus/Engine/Core/SmartPtr.h>

namespace ludus::core
{
    template<typename T, typename Deleter>
    LUDUS_INLINE UniquePtr<T, Deleter>::UniquePtr(T* ptr) noexcept
        : mPtr(ptr) {}

    template<typename T, typename Deleter>
    LUDUS_INLINE UniquePtr<T, Deleter>::UniquePtr(T* ptr, Deleter deleter) noexcept
        : mPtr(ptr)
        , mDeleter(std::move(deleter)) {}

    template<typename T, typename Deleter>
    LUDUS_INLINE UniquePtr<T, Deleter>::UniquePtr(UniquePtr&& other) noexcept
        : mPtr(other.mPtr)
        , mDeleter(std::move(other.mDeleter))
    {
        other.mPtr = nullptr;
    }

    template<typename T, typename Deleter>
    LUDUS_INLINE UniquePtr<T, Deleter>& UniquePtr<T, Deleter>::operator=(UniquePtr&& other) noexcept
    {
        if(this != &other)
        {
            Reset();
            mPtr = other.mPtr;
            mDeleter = std::move(other.mDeleter);
            other.mPtr = nullptr;
        }
        return *this;
    }

    template<typename T, typename Deleter>
    LUDUS_INLINE UniquePtr<T, Deleter>::~UniquePtr() noexcept
    {
        Reset();
    }

    template<typename T, typename Deleter>
    LUDUS_INLINE T* UniquePtr<T, Deleter>::Get() const noexcept
    {
        return mPtr;
    }

    template<typename T, typename Deleter>
    LUDUS_INLINE Deleter& UniquePtr<T, Deleter>::GetDeleter() noexcept
    {
        return mDeleter;
    }

    template<typename T, typename Deleter>
    LUDUS_INLINE const Deleter& UniquePtr<T, Deleter>::GetDeleter() const noexcept
    {
        return mDeleter;
    }

    template<typename T, typename Deleter>
    LUDUS_INLINE T* UniquePtr<T, Deleter>::Release() noexcept
    {
        T* ptr = mPtr;
        mPtr = nullptr;
        return ptr;
    }

    template<typename T, typename Deleter>
    LUDUS_INLINE void UniquePtr<T, Deleter>::Reset(T* ptr) noexcept
    {
        if(mPtr != nullptr)
        {
            mDeleter(mPtr);
        }
        mPtr = ptr;
    }

    template<typename T, typename Deleter>
    LUDUS_INLINE void UniquePtr<T, Deleter>::Swap(UniquePtr& other) noexcept
    {
        std::swap(mPtr, other.mPtr);
        std::swap(mDeleter, other.mDeleter);
    }

    template<typename T, typename Deleter>
    LUDUS_INLINE T& UniquePtr<T, Deleter>::operator*() const noexcept
    {
        LUDUS_ASSERT_MSG(mPtr != nullptr, "Dereferencing a null UniquePtr.");
        return *mPtr;
    }

    template<typename T, typename Deleter>
    LUDUS_INLINE T* UniquePtr<T, Deleter>::operator->() const noexcept
    {
        LUDUS_ASSERT_MSG(mPtr != nullptr, "Accessing a null UniquePtr.");
        return mPtr;
    }

    template<typename T, typename Deleter>
    LUDUS_INLINE UniquePtr<T, Deleter>::operator bool() const noexcept
    {
        return mPtr != nullptr;
    }

    template<typename T, typename... Args>
    LUDUS_INLINE UniquePtr<T> MakeUnique(Args&&... args)
    {
        return UniquePtr<T>(new T(std::forward<Args>(args)...));
    }

    template<typename T>
    LUDUS_INLINE SharedPtr<T>::SharedPtr(T* ptr) noexcept
    {
        if(ptr)
        {
            mBlock = new ControlBlock<T, DefaultDelete<T>>(ptr, DefaultDelete<T>{});
            mPtr = ptr;
        }
    }

    template<typename T>
    template<typename Deleter>
    LUDUS_INLINE SharedPtr<T>::SharedPtr(T* ptr, Deleter deleter) noexcept
    {
        if(ptr)
        {
            mBlock = new ControlBlock<T, Deleter>(ptr, std::move(deleter));
            mPtr = ptr;
        }
    }

    template<typename T>
    LUDUS_INLINE SharedPtr<T>::SharedPtr(const SharedPtr& other) noexcept
        : mBlock(other.mBlock)
        , mPtr(other.mPtr)
    {
        AddRef();
    }

    template<typename T>
    LUDUS_INLINE SharedPtr<T>& SharedPtr<T>::operator=(const SharedPtr& other) noexcept
    {
        if(this != &other)
        {
            Release();
            mBlock = other.mBlock;
            mPtr = other.mPtr;
            AddRef();
        }
        return *this;
    }

    template<typename T>
    LUDUS_INLINE SharedPtr<T>::SharedPtr(SharedPtr&& other) noexcept
        : mBlock(other.mBlock)
        , mPtr(other.mPtr)
    {
        other.mBlock = nullptr;
        other.mPtr = nullptr;
    }

    template<typename T>
    LUDUS_INLINE SharedPtr<T>& SharedPtr<T>::operator=(SharedPtr&& other) noexcept
    {
        if(this != &other)
        {
            Release();
            mBlock = other.mBlock;
            mPtr = other.mPtr;
            other.mBlock = nullptr;
            other.mPtr = nullptr;
        }
        return *this;
    }

    template<typename T>
    LUDUS_INLINE SharedPtr<T>::~SharedPtr() noexcept
    {
        Release();
    }

    template<typename T>
    LUDUS_INLINE T* SharedPtr<T>::Get() const noexcept
    {
        return mPtr;
    }

    template<typename T>
    LUDUS_INLINE uint32_t SharedPtr<T>::UseCount() const noexcept
    {
        return mBlock ? mBlock->Strong.load(std::memory_order_relaxed) : 0;
    }

    template<typename T>
    LUDUS_INLINE bool SharedPtr<T>::Unique() const noexcept
    {
        return UseCount() == 1;
    }

    template<typename T>
    LUDUS_INLINE void SharedPtr<T>::Reset(T* ptr) noexcept
    {
        Release();
        if(ptr)
        {
            mBlock = new ControlBlock<T, DefaultDelete<T>>(ptr, DefaultDelete<T>{});
            mPtr = ptr;
        }
    }

    template<typename T>
    LUDUS_INLINE void SharedPtr<T>::Swap(SharedPtr& other) noexcept
    {
        std::swap(mBlock, other.mBlock);
        std::swap(mPtr, other.mPtr);
    }

    template<typename T>
    LUDUS_INLINE T& SharedPtr<T>::operator*() const noexcept
    {
        LUDUS_ASSERT_MSG(mPtr != nullptr, "Dereferencing a null SharedPtr.");
        return *mPtr;
    }

    template<typename T>
    LUDUS_INLINE T* SharedPtr<T>::operator->() const noexcept
    {
        LUDUS_ASSERT_MSG(mPtr != nullptr, "Accessing a null SharedPtr.");
        return mPtr;
    }

    template<typename T>
    LUDUS_INLINE SharedPtr<T>::operator bool() const noexcept
    {
        return mPtr != nullptr;
    }

    template<typename T>
    LUDUS_INLINE SharedPtr<T>::SharedPtr(ControlBlockBase* block, T* ptr) noexcept
        : mBlock(block)
        , mPtr(ptr) {}

    template<typename T>
    LUDUS_INLINE void SharedPtr<T>::AddRef() noexcept
    {
        if(mBlock)
        {
            mBlock->Strong.fetch_add(1, std::memory_order_relaxed);
        }
    }

    template<typename T>
    LUDUS_INLINE void SharedPtr<T>::Release() noexcept
    {
        if(!mBlock)
        {
            return;
        }

        if(mBlock->Strong.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            mBlock->DestroyObject();
            if(mBlock->Weak.fetch_sub(1, std::memory_order_acq_rel) == 1)
            {
                delete mBlock;
            }
        }

        mBlock = nullptr;
        mPtr = nullptr;
    }

    template<typename T>
    LUDUS_INLINE WeakPtr<T>::WeakPtr(const SharedPtr<T>& shared) noexcept
        : mBlock(shared.mBlock)
    {
        AddRef();
    }

    template<typename T>
    LUDUS_INLINE WeakPtr<T>::WeakPtr(const WeakPtr& other) noexcept
        : mBlock(other.mBlock)
    {
        AddRef();
    }

    template<typename T>
    LUDUS_INLINE WeakPtr<T>& WeakPtr<T>::operator=(const WeakPtr& other) noexcept
    {
        if(this != &other)
        {
            Release();
            mBlock = other.mBlock;
            AddRef();
        }
        return *this;
    }

    template<typename T>
    LUDUS_INLINE WeakPtr<T>::WeakPtr(WeakPtr&& other) noexcept
        : mBlock(other.mBlock)
    {
        other.mBlock = nullptr;
    }

    template<typename T>
    LUDUS_INLINE WeakPtr<T>& WeakPtr<T>::operator=(WeakPtr&& other) noexcept
    {
        if(this != &other)
        {
            Release();
            mBlock = other.mBlock;
            other.mBlock = nullptr;
        }
        return *this;
    }

    template<typename T>
    LUDUS_INLINE WeakPtr<T>::~WeakPtr() noexcept
    {
        Release();
    }

    template<typename T>
    LUDUS_INLINE uint32_t WeakPtr<T>::UseCount() const noexcept
    {
        return mBlock ? mBlock->Strong.load(std::memory_order_relaxed) : 0;
    }

    template<typename T>
    LUDUS_INLINE bool WeakPtr<T>::Expired() const noexcept
    {
        return UseCount() == 0;
    }

    template<typename T>
    LUDUS_INLINE SharedPtr<T> WeakPtr<T>::Lock() const noexcept
    {
        if(!mBlock)
        {
            return {};
        }

        uint32_t strong = mBlock->Strong.load(std::memory_order_acquire);
        while(strong != 0)
        {
            if(mBlock->Strong.compare_exchange_weak(
                strong,
                strong + 1,
                std::memory_order_acq_rel,
                std::memory_order_acquire))
            {
                return SharedPtr<T>(mBlock, static_cast<T*>(mBlock->GetPtr()));
            }
        }
        return {};
    }

    template<typename T>
    LUDUS_INLINE void WeakPtr<T>::Reset() noexcept
    {
        Release();
        mBlock = nullptr;
    }

    template<typename T>
    LUDUS_INLINE void WeakPtr<T>::Swap(WeakPtr& other) noexcept
    {
        std::swap(mBlock, other.mBlock);
    }

    template<typename T>
    LUDUS_INLINE void WeakPtr<T>::AddRef() noexcept
    {
        if(mBlock)
        {
            mBlock->Weak.fetch_add(1, std::memory_order_relaxed);
        }
    }

    template<typename T>
    LUDUS_INLINE void WeakPtr<T>::Release() noexcept
    {
        if(!mBlock)
        {
            return;
        }

        if(mBlock->Weak.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            delete mBlock;
        }
        mBlock = nullptr;
    }

    template<typename T, typename... Args>
    LUDUS_INLINE SharedPtr<T> MakeShared(Args&&... args)
    {
        T* ptr = new T(std::forward<Args>(args)...);
        return SharedPtr<T>(ptr);
    }
}   // namespace ludus::core

