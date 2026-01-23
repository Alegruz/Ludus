#pragma once

#include <Ludus/Engine/Core/Common.h>

#include <concepts>
#include <cstdint>

namespace ludus::core
{
    template<Arithmetic T>
    struct Vector2 final
    {
        T X;
        T Y;

        constexpr Vector2() noexcept;
        constexpr Vector2(T x, T y) noexcept;

        [[nodiscard]] constexpr Vector2 operator+(const Vector2& other) const noexcept;
        [[nodiscard]] constexpr Vector2 operator-(const Vector2& other) const noexcept;
        [[nodiscard]] constexpr Vector2 operator*(const Vector2& other) const noexcept;
        [[nodiscard]] constexpr Vector2 operator/(const Vector2& other) const noexcept;
        [[nodiscard]] constexpr Vector2 operator*(T scalar) const noexcept;
        [[nodiscard]] constexpr Vector2 operator/(T scalar) const noexcept;

        constexpr Vector2& operator+=(const Vector2& other) noexcept;
        constexpr Vector2& operator-=(const Vector2& other) noexcept;
        constexpr Vector2& operator*=(const Vector2& other) noexcept;
        constexpr Vector2& operator/=(const Vector2& other) noexcept;
        constexpr Vector2& operator*=(T scalar) noexcept;
        constexpr Vector2& operator/=(T scalar) noexcept;

        [[nodiscard]] constexpr bool operator==(const Vector2& other) const noexcept;
        [[nodiscard]] constexpr bool operator!=(const Vector2& other) const noexcept;

        [[nodiscard]] constexpr T Dot(const Vector2& other) const noexcept;
        [[nodiscard]] T Length() const noexcept;
        [[nodiscard]] constexpr T LengthSquared() const noexcept;
        [[nodiscard]] Vector2 Normalized() const noexcept;
        void Normalize() noexcept;
    };

    template<Arithmetic T>
    struct Vector3 final
    {
        T X;
        T Y;
        T Z;

        constexpr Vector3() noexcept;
        constexpr Vector3(T x, T y, T z) noexcept;

        [[nodiscard]] constexpr Vector3 operator+(const Vector3& other) const noexcept;
        [[nodiscard]] constexpr Vector3 operator-(const Vector3& other) const noexcept;
        [[nodiscard]] constexpr Vector3 operator*(const Vector3& other) const noexcept;
        [[nodiscard]] constexpr Vector3 operator/(const Vector3& other) const noexcept;
        [[nodiscard]] constexpr Vector3 operator*(T scalar) const noexcept;
        [[nodiscard]] constexpr Vector3 operator/(T scalar) const noexcept;

        constexpr Vector3& operator+=(const Vector3& other) noexcept;
        constexpr Vector3& operator-=(const Vector3& other) noexcept;
        constexpr Vector3& operator*=(const Vector3& other) noexcept;
        constexpr Vector3& operator/=(const Vector3& other) noexcept;
        constexpr Vector3& operator*=(T scalar) noexcept;
        constexpr Vector3& operator/=(T scalar) noexcept;

        [[nodiscard]] constexpr bool operator==(const Vector3& other) const noexcept;
        [[nodiscard]] constexpr bool operator!=(const Vector3& other) const noexcept;

        [[nodiscard]] constexpr T Dot(const Vector3& other) const noexcept;
        [[nodiscard]] constexpr Vector3 Cross(const Vector3& other) const noexcept;
        [[nodiscard]] T Length() const noexcept;
        [[nodiscard]] constexpr T LengthSquared() const noexcept;
        [[nodiscard]] Vector3 Normalized() const noexcept;
        void Normalize() noexcept;
    };

    template<Arithmetic T>
    struct Vector4 final
    {
        T X;
        T Y;
        T Z;
        T W;

        constexpr Vector4() noexcept;
        constexpr Vector4(T x, T y, T z, T w) noexcept;

        [[nodiscard]] constexpr Vector4 operator+(const Vector4& other) const noexcept;
        [[nodiscard]] constexpr Vector4 operator-(const Vector4& other) const noexcept;
        [[nodiscard]] constexpr Vector4 operator*(const Vector4& other) const noexcept;
        [[nodiscard]] constexpr Vector4 operator/(const Vector4& other) const noexcept;
        [[nodiscard]] constexpr Vector4 operator*(T scalar) const noexcept;
        [[nodiscard]] constexpr Vector4 operator/(T scalar) const noexcept;

        constexpr Vector4& operator+=(const Vector4& other) noexcept;
        constexpr Vector4& operator-=(const Vector4& other) noexcept;
        constexpr Vector4& operator*=(const Vector4& other) noexcept;
        constexpr Vector4& operator/=(const Vector4& other) noexcept;
        constexpr Vector4& operator*=(T scalar) noexcept;
        constexpr Vector4& operator/=(T scalar) noexcept;

        [[nodiscard]] constexpr bool operator==(const Vector4& other) const noexcept;
        [[nodiscard]] constexpr bool operator!=(const Vector4& other) const noexcept;

        [[nodiscard]] constexpr T Dot(const Vector4& other) const noexcept;
        [[nodiscard]] T Length() const noexcept;
        [[nodiscard]] constexpr T LengthSquared() const noexcept;
        [[nodiscard]] Vector4 Normalized() const noexcept;
        void Normalize() noexcept;
    };

    template<Arithmetic T>
    [[nodiscard]] constexpr Vector2<T> operator*(T scalar, const Vector2<T>& value) noexcept;

    template<Arithmetic T>
    [[nodiscard]] constexpr Vector3<T> operator*(T scalar, const Vector3<T>& value) noexcept;

    template<Arithmetic T>
    [[nodiscard]] constexpr Vector4<T> operator*(T scalar, const Vector4<T>& value) noexcept;

    using Vector2I = Vector2<int32_t>;
    using Vector2U = Vector2<uint32_t>;
    using Vector2F = Vector2<float>;
    using Vector3I = Vector3<int32_t>;
    using Vector3U = Vector3<uint32_t>;
    using Vector3F = Vector3<float>;
    using Vector4I = Vector4<int32_t>;
    using Vector4U = Vector4<uint32_t>;
    using Vector4F = Vector4<float>;
} // namespace ludus::core
