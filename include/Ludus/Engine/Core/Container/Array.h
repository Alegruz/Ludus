#pragma once

#include <Ludus/Engine/Core/Common.h>

namespace ludus::core
{
    template<typename T>
    concept ArrayElementType = std::is_copy_constructible_v<T> && std::is_move_constructible_v<T>;

    enum class ArrayType : uint8_t
    {
        DYNAMIC,
        STATIC,
        COUNT,

        DEFAULT = DYNAMIC,
    };

    enum class ArrayResizePolicy : uint8_t
    {
        DOUBLE,
        FIXED_INCREMENT,
        COUNT,

        DEFAULT = DOUBLE,
    };

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY = 0, ArrayResizePolicy RESIZE_POLICY = ArrayResizePolicy::DEFAULT>
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
        explicit constexpr ArrayImplBase() noexcept requires (ARRAY_TYPE == ArrayType::DYNAMIC);
        explicit constexpr ArrayImplBase() noexcept requires (ARRAY_TYPE == ArrayType::STATIC);
        explicit constexpr ArrayImplBase(uint32_t capacity) noexcept requires (ARRAY_TYPE == ArrayType::DYNAMIC && StringCharType<T>);
        explicit constexpr ArrayImplBase(uint32_t capacity) noexcept requires (ARRAY_TYPE == ArrayType::DYNAMIC && !StringCharType<T>);
        explicit constexpr ArrayImplBase(uint32_t size, const T& defaultValue) noexcept;
        explicit constexpr ArrayImplBase(Iterator first, Iterator last) noexcept requires (ARRAY_TYPE == ArrayType::DYNAMIC);
        explicit constexpr ArrayImplBase(const T* array, uint32_t size) noexcept requires (ARRAY_TYPE == ArrayType::DYNAMIC);
        explicit constexpr ArrayImplBase(const T* str) noexcept requires (ARRAY_TYPE == ArrayType::DYNAMIC && StringCharType<T>);
        constexpr ArrayImplBase(const ArrayImplBase& other) noexcept requires (ARRAY_TYPE == ArrayType::DYNAMIC);
        constexpr ArrayImplBase(const ArrayImplBase& other) noexcept requires (ARRAY_TYPE == ArrayType::STATIC);
        constexpr ArrayImplBase(ArrayImplBase&& other) noexcept requires (ARRAY_TYPE == ArrayType::DYNAMIC);
        constexpr ArrayImplBase(ArrayImplBase&& other) noexcept requires (ARRAY_TYPE == ArrayType::STATIC);
        explicit constexpr ArrayImplBase(std::initializer_list<T> initList) noexcept;

        constexpr ~ArrayImplBase() noexcept requires (ARRAY_TYPE == ArrayType::DYNAMIC);
        constexpr ~ArrayImplBase() noexcept requires (ARRAY_TYPE == ArrayType::STATIC);

        constexpr ArrayImplBase& operator=(const ArrayImplBase& other) noexcept;
        constexpr ArrayImplBase& operator=(ArrayImplBase&& other) noexcept;
        constexpr ArrayImplBase& operator=(std::initializer_list<T> initList) noexcept;

        constexpr void Assign(uint32_t size, const T& defaultValue) noexcept;
        constexpr void Assign(Iterator first, Iterator last) noexcept requires (ARRAY_TYPE == ArrayType::DYNAMIC);
        constexpr void Assign(const T* array, uint32_t size) noexcept requires (ARRAY_TYPE == ArrayType::DYNAMIC);
        constexpr void Assign(const T* str) noexcept requires (ARRAY_TYPE == ArrayType::DYNAMIC && StringCharType<T>);
        constexpr void Assign(std::initializer_list<T> initList) noexcept;

        // Element Access
        constexpr T* GetData() noexcept;
        [[nodiscard]] constexpr const T* GetData() const noexcept;
        [[nodiscard]] constexpr const T* GetCStr() const noexcept requires (StringCharType<T>);

        // Iterators
        [[nodiscard]] constexpr Iterator begin() noexcept;  // NOLINT(readability-identifier-naming)
        [[nodiscard]] constexpr ConstIterator begin() const noexcept;   // NOLINT(readability-identifier-naming)
        [[nodiscard]] constexpr ConstIterator cbegin() const noexcept;  // NOLINT(readability-identifier-naming)
        [[nodiscard]] constexpr Iterator end() noexcept;    // NOLINT(readability-identifier-naming)
        [[nodiscard]] constexpr ConstIterator end() const noexcept;  // NOLINT(readability-identifier-naming)
        [[nodiscard]] constexpr ConstIterator cend() const noexcept;    // NOLINT(readability-identifier-naming)

        // Capacity
        constexpr void SetCapacity(uint32_t capacity) noexcept requires (ARRAY_TYPE == ArrayType::DYNAMIC);
        [[nodiscard]] constexpr uint32_t GetSize() const noexcept;
        [[nodiscard]] constexpr uint32_t GetLength() const noexcept requires (ARRAY_TYPE == ArrayType::DYNAMIC && StringCharType<T>);
        [[nodiscard]] constexpr uint32_t GetCapacity() const noexcept;

    protected:
        [[nodiscard]] constexpr uint32_t calculateCapacityToAllocate(uint32_t requiredCapacity) const noexcept requires (ARRAY_TYPE == ArrayType::DYNAMIC);
        constexpr void copy(std::initializer_list<T> initList) noexcept;
        constexpr void updateSize(uint32_t newSize) noexcept requires (ARRAY_TYPE == ArrayType::DYNAMIC);

    protected:
        static constexpr uint32_t INITIAL_CAPACITY = 16;
        static constexpr uint32_t FIXED_INCREMENT = 16;

    protected:
        // Conditional members based on array type
        [[no_unique_address]] std::conditional_t<ARRAY_TYPE == ArrayType::DYNAMIC, uint32_t, std::monostate> mCapacity;

    private:
        [[no_unique_address]] std::conditional_t<ARRAY_TYPE == ArrayType::DYNAMIC, uint32_t, std::monostate> mSize;

    protected:
        std::conditional_t<ARRAY_TYPE == ArrayType::DYNAMIC, T*, T[STATIC_CAPACITY]> mData;
    };

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY = 0, ArrayResizePolicy RESIZE_POLICY = ArrayResizePolicy::DEFAULT>
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
        explicit constexpr ArrayImpl(uint32_t capacity) noexcept requires (ARRAY_TYPE == ArrayType::DYNAMIC);
        explicit constexpr ArrayImpl(uint32_t size, const T& defaultValue) noexcept;
        explicit constexpr ArrayImpl(ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::Iterator first, ArrayImplBase<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>::Iterator last) noexcept requires (ARRAY_TYPE == ArrayType::DYNAMIC);
        explicit constexpr ArrayImpl(const T* array, uint32_t size) noexcept requires (ARRAY_TYPE == ArrayType::DYNAMIC);
        constexpr ArrayImpl(const T* str) noexcept requires (ARRAY_TYPE == ArrayType::DYNAMIC && StringCharType<T>);
        constexpr ArrayImpl(const ArrayImpl& other) noexcept;
        constexpr ArrayImpl(ArrayImpl&& other) noexcept;
        explicit constexpr ArrayImpl(std::initializer_list<T> initList) noexcept;
        constexpr ~ArrayImpl() noexcept;

        constexpr ArrayImpl& operator=(const ArrayImpl& other) noexcept;
        constexpr ArrayImpl& operator=(ArrayImpl&& other) noexcept;
        constexpr ArrayImpl& operator=(std::initializer_list<T> initList) noexcept;
        
        // Element Access
        constexpr T& At(uint32_t index) noexcept;
        [[nodiscard]] constexpr const T& At(uint32_t index) const noexcept;
        constexpr T& operator[](uint32_t index) noexcept;
        [[nodiscard]] constexpr const T& operator[](uint32_t index) const noexcept;
        constexpr T& GetFront() noexcept;
        [[nodiscard]] constexpr const T& GetFront() const noexcept;
        constexpr T& GetBack() noexcept;
        [[nodiscard]] constexpr const T& GetBack() const noexcept;
        constexpr T* GetData() noexcept;
        [[nodiscard]] constexpr const T* GetData() const noexcept;
        [[nodiscard]] constexpr const T* GetCStr() const noexcept requires (StringCharType<T>);

        // Capacity
        [[nodiscard]] constexpr bool IsEmpty() const noexcept;

        // Modifiers
        constexpr ArrayImpl& Append(const T* array, uint32_t size) noexcept requires (ARRAY_TYPE == ArrayType::DYNAMIC);
        constexpr void PushBack(const T& element) noexcept requires (ARRAY_TYPE == ArrayType::DYNAMIC);
        constexpr void PushBack(T&& element) noexcept requires (ARRAY_TYPE == ArrayType::DYNAMIC);
    };

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY = 0, ArrayResizePolicy RESIZE_POLICY = ArrayResizePolicy::DEFAULT>
    constexpr bool operator==(const ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>& lhs, const ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>& rhs) noexcept;
    
    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY = 0, ArrayResizePolicy RESIZE_POLICY = ArrayResizePolicy::DEFAULT>
    constexpr bool operator!=(const ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>& lhs, const ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>& rhs) noexcept;

    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY = 0, ArrayResizePolicy RESIZE_POLICY = ArrayResizePolicy::DEFAULT>
    constexpr bool operator==(const ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>& lhs, const T* rhs) noexcept requires (StringCharType<T>);
    
    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY = 0, ArrayResizePolicy RESIZE_POLICY = ArrayResizePolicy::DEFAULT>
    constexpr bool operator!=(const ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>& lhs, const T* rhs) noexcept requires (StringCharType<T>);
    
    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY = 0, ArrayResizePolicy RESIZE_POLICY = ArrayResizePolicy::DEFAULT>
    constexpr bool operator==(const T* lhs, const ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>& rhs) noexcept requires (StringCharType<T>);
    
    template<ArrayElementType T, ArrayType ARRAY_TYPE, uint32_t STATIC_CAPACITY = 0, ArrayResizePolicy RESIZE_POLICY = ArrayResizePolicy::DEFAULT>
    constexpr bool operator!=(const T* lhs, const ArrayImpl<T, ARRAY_TYPE, STATIC_CAPACITY, RESIZE_POLICY>& rhs) noexcept requires (StringCharType<T>);

    // ARRAY_TYPE aliases for convenience
    template<ArrayElementType T>
    using DynamicArray = ArrayImpl<T, ArrayType::DYNAMIC>;

    template<ArrayElementType T, uint32_t CAPACITY>
    using StaticArray = ArrayImpl<T, ArrayType::STATIC, CAPACITY>;
}   // namespace ludus::core