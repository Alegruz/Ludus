#pragma once

#include <Ludus/Engine/Core/Assert.h>
#include <Ludus/Engine/Core/Common.h>

#include <atomic>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace ludus::core
{
    template<typename T>
    struct DefaultDelete final
    {
        constexpr DefaultDelete() noexcept = default;
        void operator()(T* ptr) const noexcept
        {
            delete ptr;
        }
    };

    template<typename T, typename Deleter = DefaultDelete<T>>
    class UniquePtr final
    {
    public:
        using element_type = T;
        using deleter_type = Deleter;

    public:
        constexpr UniquePtr() noexcept = default;
        constexpr UniquePtr(std::nullptr_t) noexcept {}
        explicit UniquePtr(T* ptr) noexcept;
        UniquePtr(T* ptr, Deleter deleter) noexcept;

        UniquePtr(const UniquePtr&) = delete;
        UniquePtr& operator=(const UniquePtr&) = delete;

        UniquePtr(UniquePtr&& other) noexcept;
        UniquePtr& operator=(UniquePtr&& other) noexcept;

        ~UniquePtr() noexcept;

        [[nodiscard]] T* Get() const noexcept;
        [[nodiscard]] Deleter& GetDeleter() noexcept;
        [[nodiscard]] const Deleter& GetDeleter() const noexcept;

        T* Release() noexcept;
        void Reset(T* ptr = nullptr) noexcept;
        void Swap(UniquePtr& other) noexcept;

        [[nodiscard]] T& operator*() const noexcept;
        [[nodiscard]] T* operator->() const noexcept;
        [[nodiscard]] explicit operator bool() const noexcept;

    private:
        T* mPtr = nullptr;
        Deleter mDeleter{};
    };

    template<typename T, typename... Args>
    [[nodiscard]] LUDUS_INLINE UniquePtr<T> MakeUnique(Args&&... args);

    class ControlBlockBase
    {
    public:
        ControlBlockBase() noexcept = default;
        ControlBlockBase(const ControlBlockBase&) = delete;
        ControlBlockBase& operator=(const ControlBlockBase&) = delete;
        virtual ~ControlBlockBase() = default;

        std::atomic<uint32_t> Strong{1};
        std::atomic<uint32_t> Weak{1};

        virtual void DestroyObject() noexcept = 0;
        [[nodiscard]] virtual void* GetPtr() const noexcept = 0;
    };

    template<typename T, typename Deleter>
    class ControlBlock final : public ControlBlockBase
    {
    public:
        ControlBlock(T* ptr, Deleter deleter) noexcept
            : Ptr(ptr)
            , DeleterFn(std::move(deleter)) {}

        void DestroyObject() noexcept override
        {
            if(Ptr)
            {
                DeleterFn(Ptr);
                Ptr = nullptr;
            }
        }

        [[nodiscard]] void* GetPtr() const noexcept override
        {
            return Ptr;
        }

        T* Ptr = nullptr;
        Deleter DeleterFn;
    };

    template<typename T>
    class WeakPtr;

    template<typename T>
    class SharedPtr final
    {
    public:
        using element_type = T;

    public:
        constexpr SharedPtr() noexcept = default;
        constexpr SharedPtr(std::nullptr_t) noexcept {}
        explicit SharedPtr(T* ptr) noexcept;

        template<typename Deleter>
        SharedPtr(T* ptr, Deleter deleter) noexcept;

        SharedPtr(const SharedPtr& other) noexcept;
        SharedPtr& operator=(const SharedPtr& other) noexcept;

        SharedPtr(SharedPtr&& other) noexcept;
        SharedPtr& operator=(SharedPtr&& other) noexcept;

        ~SharedPtr() noexcept;

        [[nodiscard]] T* Get() const noexcept;
        [[nodiscard]] uint32_t UseCount() const noexcept;
        [[nodiscard]] bool Unique() const noexcept;

        void Reset(T* ptr = nullptr) noexcept;
        void Swap(SharedPtr& other) noexcept;

        [[nodiscard]] T& operator*() const noexcept;
        [[nodiscard]] T* operator->() const noexcept;
        [[nodiscard]] explicit operator bool() const noexcept;

    private:
        friend class WeakPtr<T>;

        SharedPtr(ControlBlockBase* block, T* ptr) noexcept;
        void AddRef() noexcept;
        void Release() noexcept;

        ControlBlockBase* mBlock = nullptr;
        T* mPtr = nullptr;
    };

    template<typename T>
    class WeakPtr final
    {
    public:
        using element_type = T;

    public:
        constexpr WeakPtr() noexcept = default;
        constexpr WeakPtr(std::nullptr_t) noexcept {}

        WeakPtr(const SharedPtr<T>& shared) noexcept;
        WeakPtr(const WeakPtr& other) noexcept;
        WeakPtr& operator=(const WeakPtr& other) noexcept;

        WeakPtr(WeakPtr&& other) noexcept;
        WeakPtr& operator=(WeakPtr&& other) noexcept;

        ~WeakPtr() noexcept;

        [[nodiscard]] uint32_t UseCount() const noexcept;
        [[nodiscard]] bool Expired() const noexcept;
        [[nodiscard]] SharedPtr<T> Lock() const noexcept;

        void Reset() noexcept;
        void Swap(WeakPtr& other) noexcept;

    private:
        void AddRef() noexcept;
        void Release() noexcept;

        ControlBlockBase* mBlock = nullptr;
    };

    template<typename T, typename... Args>
    [[nodiscard]] LUDUS_INLINE SharedPtr<T> MakeShared(Args&&... args);
}   // namespace ludus::core

