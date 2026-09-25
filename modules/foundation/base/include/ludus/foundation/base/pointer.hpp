#pragma once

#include <ludus/foundation/base/core.h>

namespace ludus::foundation::core
{
template <typename ElementType>
struct DefaultDeleter
{
    void operator()(ElementType* ptr) const noexcept
    {
        delete ptr;
    }
};

template <typename ElementType>
concept Deleter = requires(ElementType* ptr) {
    { DefaultDeleter<ElementType>{}(ptr) } noexcept;
};

template <typename ElementType, Deleter DeleterType = DefaultDeleter<ElementType>>
class UniquePtr final
{
    template <typename OtherElementType, Deleter OtherDeleterType>
    friend class UniquePtr;

public:
    constexpr UniquePtr() noexcept : mPtr(nullptr) {}
    constexpr UniquePtr(nullptr_t) noexcept : mPtr(nullptr) {}
    explicit UniquePtr(ElementType*& ptr) noexcept : mPtr(ptr)
    {
        ptr = nullptr;
    }

    UniquePtr(const UniquePtr&) = delete;
    UniquePtr& operator=(const UniquePtr&) = delete;

    constexpr UniquePtr(UniquePtr&& other) noexcept : mPtr(other.mPtr)
    {
        other.mPtr = nullptr;
    }
    UniquePtr& operator=(UniquePtr&& other) noexcept;

    template <DerivedFrom<ElementType> OtherType, Deleter OtherDeleterType = DefaultDeleter<OtherType>>
    UniquePtr& operator=(UniquePtr<OtherType, OtherDeleterType>&& other) noexcept;

    ~UniquePtr();

    [[nodiscard]] ElementType* Get() const noexcept
    {
        return mPtr;
    }
    [[nodiscard]] ElementType* Release() noexcept;
    void Reset(ElementType* ptr = nullptr) noexcept;

    [[nodiscard]] ElementType& operator*() const noexcept
    {
        return *mPtr;
    }
    [[nodiscard]] ElementType* operator->() const noexcept
    {
        return mPtr;
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return mPtr != nullptr;
    }

    void Swap(UniquePtr& other) noexcept;

private:
    [[no_unique_address]] DeleterType mDeleter;
    ElementType* mPtr;
};

template <typename ElementType, Deleter DeleterType>
UniquePtr<ElementType, DeleterType>& UniquePtr<ElementType, DeleterType>::operator=(UniquePtr&& other) noexcept
{
    if (this != &other)
    {
        mPtr = other.mPtr;
        other.mPtr = nullptr;
    }
    return *this;
}

template <typename ElementType, Deleter DeleterType>
template <DerivedFrom<ElementType> OtherType, Deleter OtherDeleterType>
UniquePtr<ElementType, DeleterType>&
UniquePtr<ElementType, DeleterType>::operator=(UniquePtr<OtherType, OtherDeleterType>&& other) noexcept
{
    if (this != reinterpret_cast<UniquePtr<ElementType, DeleterType>*>(&other))
    {
        mPtr = other.mPtr;
        other.mPtr = nullptr;
    }
    return *this;
}

template <typename ElementType, Deleter DeleterType>
UniquePtr<ElementType, DeleterType>::~UniquePtr()
{
    mDeleter(mPtr);
}

template <typename ElementType, Deleter DeleterType>
ElementType* UniquePtr<ElementType, DeleterType>::Release() noexcept
{
    ElementType* temp = mPtr;
    mPtr = nullptr;
    return temp;
}

template <typename ElementType, Deleter DeleterType>
void UniquePtr<ElementType, DeleterType>::Reset(ElementType* ptr) noexcept
{
    mDeleter(mPtr);
    mPtr = ptr;
}

template <typename ElementType, Deleter DeleterType>
void UniquePtr<ElementType, DeleterType>::Swap(UniquePtr<ElementType, DeleterType>& other) noexcept
{
    ElementType* temp = mPtr;
    mPtr = other.mPtr;
    other.mPtr = temp;
}
} // namespace ludus::foundation::core

namespace ludus::foundation
{
template <typename ElementType>
using UniquePtr = core::UniquePtr<ElementType>;
}
