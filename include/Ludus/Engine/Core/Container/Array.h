#pragma once

#include <Ludus/Engine/Core/Common.h>

namespace ludus::core
{
    template<typename T>
    concept ArrayElementType = std::is_copy_constructible_v<T> && std::is_move_constructible_v<T>;

    enum class ArrayType : uint8_t
    {
        Dynamic,
        Static,
        Count,

        Default = Dynamic,
    };

    enum class ArrayResizePolicy : uint8_t
    {
        Double,
        FixedIncrement,
        Count,

        Default = Double,
    };

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY = 0, ArrayResizePolicy RESIZE_POLICY = ArrayResizePolicy::Default>
    class ArrayImplBase
    {
    private:
        template<typename IteratorType>
            requires std::is_same_v<IteratorType, T> || std::is_same_v<IteratorType, const T>
        class IteratorImpl final
        {
        public:
            explicit constexpr IteratorImpl(IteratorType& start) noexcept;

            constexpr IteratorImpl& operator++() noexcept;
            constexpr IteratorType& operator*() noexcept requires (!std::is_const_v<IteratorType>);
            constexpr IteratorType& operator*() const noexcept requires (std::is_const_v<IteratorType>);
            constexpr bool operator==(const IteratorImpl& other) const noexcept;
            constexpr bool operator!=(const IteratorImpl& other) const noexcept;

        private:
            IteratorType* mCurrentOrNull;
        };

    public:
        using Iterator = IteratorImpl<T>;
        using ConstIterator = IteratorImpl<const T>;

    public:
        explicit constexpr ArrayImplBase() noexcept requires (ARRAY_TYPE == ArrayType::Dynamic);
        explicit constexpr ArrayImplBase() noexcept requires (ARRAY_TYPE == ArrayType::Static);
        explicit constexpr ArrayImplBase(const uint32_t capacity) noexcept requires (ARRAY_TYPE == ArrayType::Dynamic && StringCharType<T>);
        explicit constexpr ArrayImplBase(const uint32_t capacity) noexcept requires (ARRAY_TYPE == ArrayType::Dynamic && !StringCharType<T>);
        explicit constexpr ArrayImplBase(const uint32_t size, const T& defaultValue) noexcept;
        explicit constexpr ArrayImplBase(Iterator first, Iterator last) noexcept requires (ARRAY_TYPE == ArrayType::Dynamic);
        explicit constexpr ArrayImplBase(const T* array, uint32_t size) noexcept requires (ARRAY_TYPE == ArrayType::Dynamic);
        explicit constexpr ArrayImplBase(const T* str) noexcept requires (ARRAY_TYPE == ArrayType::Dynamic && StringCharType<T>);
        constexpr ArrayImplBase(const ArrayImplBase& other) noexcept requires (ARRAY_TYPE == ArrayType::Dynamic);
        constexpr ArrayImplBase(const ArrayImplBase& other) noexcept requires (ARRAY_TYPE == ArrayType::Static);
        constexpr ArrayImplBase(ArrayImplBase&& other) noexcept requires (ARRAY_TYPE == ArrayType::Dynamic);
        constexpr ArrayImplBase(ArrayImplBase&& other) noexcept requires (ARRAY_TYPE == ArrayType::Static);
        explicit constexpr ArrayImplBase(std::initializer_list<T> initList) noexcept;

        constexpr ~ArrayImplBase() noexcept requires (ARRAY_TYPE == ArrayType::Dynamic);
        constexpr ~ArrayImplBase() noexcept requires (ARRAY_TYPE == ArrayType::Static);

        constexpr ArrayImplBase& operator=(const ArrayImplBase& other) noexcept;
        constexpr ArrayImplBase& operator=(ArrayImplBase&& other) noexcept;
        constexpr ArrayImplBase& operator=(std::initializer_list<T> initList) noexcept;

        constexpr void Assign(const uint32_t size, const T& defaultValue) noexcept;
        constexpr void Assign(Iterator first, Iterator last) noexcept requires (ARRAY_TYPE == ArrayType::Dynamic);
        constexpr void Assign(const T* array, uint32_t size) noexcept requires (ARRAY_TYPE == ArrayType::Dynamic);
        constexpr void Assign(const T* str) noexcept requires (ARRAY_TYPE == ArrayType::Dynamic && StringCharType<T>);
        constexpr void Assign(std::initializer_list<T> initList) noexcept;

        // Element Access
        constexpr T* GetData() noexcept;
        [[nodiscard]] constexpr const T* GetData() const noexcept;

        // Iterators
        [[nodiscard]] constexpr Iterator begin() noexcept;
        [[nodiscard]] constexpr ConstIterator begin() const noexcept;
        [[nodiscard]] constexpr ConstIterator cbegin() const noexcept;
        [[nodiscard]] constexpr Iterator end() noexcept;
        [[nodiscard]] constexpr ConstIterator end() const noexcept;
        [[nodiscard]] constexpr ConstIterator cend() const noexcept;

        // Capacity
        constexpr void SetCapacity(uint32_t capacity) noexcept requires (ARRAY_TYPE == ArrayType::Dynamic);
        [[nodiscard]] constexpr uint32_t GetSize() const noexcept;
        [[nodiscard]] constexpr uint32_t GetLength() const noexcept requires (ARRAY_TYPE == ArrayType::Dynamic && StringCharType<T>);
        [[nodiscard]] constexpr uint32_t GetCapacity() const noexcept;

    protected:
        [[nodiscard]] constexpr uint32_t calculateCapacityToAllocate(const uint32_t requiredCapacity) const noexcept requires (ARRAY_TYPE == ArrayType::Dynamic);
        constexpr void copy(std::initializer_list<T> initList) noexcept;
        constexpr void updateSize(const uint32_t newSize) noexcept requires (ARRAY_TYPE == ArrayType::Dynamic);

    protected:
        static constexpr uint32_t INITIAL_CAPACITY = 16;
        static constexpr uint32_t FIXED_INCREMENT = 16;

    protected:
        // Conditional members based on array type
        [[no_unique_address]] std::conditional_t<ARRAY_TYPE == ArrayType::Dynamic, uint32_t, std::monostate> mCapacity;

    private:
        [[no_unique_address]] std::conditional_t<ARRAY_TYPE == ArrayType::Dynamic, uint32_t, std::monostate> mSize;

    protected:
        std::conditional_t<ARRAY_TYPE == ArrayType::Dynamic, T*, T[STATIC_CAPACITY]> mData;
    };

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY = 0, ArrayResizePolicy RESIZE_POLICY = ArrayResizePolicy::Default>
    class ArrayImpl final : public ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>
    {
    private:
        using Base = ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>;

    public:
        // Bring base class methods into scope
        using Base::GetData;
        using Base::GetSize;
        using Base::GetCapacity;
        using Base::begin;
        using Base::end;
        using Base::cbegin;
        using Base::cend;

    public:
        explicit constexpr ArrayImpl() noexcept;
        explicit constexpr ArrayImpl(const uint32_t capacity) noexcept requires (ARRAY_TYPE == ArrayType::Dynamic);
        explicit constexpr ArrayImpl(const uint32_t size, const T& defaultValue) noexcept;
        explicit constexpr ArrayImpl(ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::Iterator first, ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::Iterator last) noexcept requires (ARRAY_TYPE == ArrayType::Dynamic);
        explicit constexpr ArrayImpl(const T* array, uint32_t size) noexcept requires (ARRAY_TYPE == ArrayType::Dynamic);
        constexpr ArrayImpl(const T* str) noexcept requires (ARRAY_TYPE == ArrayType::Dynamic && StringCharType<T>);
        constexpr ArrayImpl(const ArrayImpl& other) noexcept;
        constexpr ArrayImpl(ArrayImpl&& other) noexcept;
        explicit constexpr ArrayImpl(std::initializer_list<T> initList) noexcept;
        constexpr ~ArrayImpl() noexcept;

        constexpr ArrayImpl& operator=(const ArrayImpl& other) noexcept;
        constexpr ArrayImpl& operator=(ArrayImpl&& other) noexcept;
        constexpr ArrayImpl& operator=(std::initializer_list<T> initList) noexcept;
        
        // Element Access
        constexpr T& At(const uint32_t index) noexcept;
        [[nodiscard]] constexpr const T& At(const uint32_t index) const noexcept;
        constexpr T& operator[](const uint32_t index) noexcept;
        [[nodiscard]] constexpr const T& operator[](const uint32_t index) const noexcept;
        constexpr T& GetFront() noexcept;
        [[nodiscard]] constexpr const T& GetFront() const noexcept;
        constexpr T& GetBack() noexcept;
        [[nodiscard]] constexpr const T& GetBack() const noexcept;
        constexpr T* GetData() noexcept;
        [[nodiscard]] constexpr const T* GetData() const noexcept;

        // Capacity
        [[nodiscard]] constexpr bool IsEmpty() const noexcept;

        // Modifiers
        constexpr ArrayImpl& Append(const T* array, const uint32_t size) noexcept requires (ARRAY_TYPE == ArrayType::Dynamic);
        constexpr void PushBack(const T& element) noexcept requires (ARRAY_TYPE == ArrayType::Dynamic);
        constexpr void PushBack(T&& element) noexcept requires (ARRAY_TYPE == ArrayType::Dynamic);
    };

    // ARRAY_TYPE aliases for convenience
    template<ArrayElementType T>
    using DynamicArray = ArrayImpl<T, ArrayType::Dynamic>;

    template<ArrayElementType T, uint32_t CAPACITY>
    using StaticArray = ArrayImpl<T, ArrayType::Static, CAPACITY>;
}   // namespace ludus::core