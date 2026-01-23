#pragma once

#include <Ludus/Engine/Core/Math/Vector.h>

#include <cmath>

namespace ludus::core
{
    template<Arithmetic T>
    LUDUS_INLINE constexpr Vector2<T>::Vector2() noexcept
        : X(0)
        , Y(0)
    {
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr Vector2<T>::Vector2(T x, T y) noexcept
        : X(x)
        , Y(y)
    {
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr Vector2<T> Vector2<T>::operator+(const Vector2& other) const noexcept
    {
        return {X + other.X, Y + other.Y};
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr Vector2<T> Vector2<T>::operator-(const Vector2& other) const noexcept
    {
        return {X - other.X, Y - other.Y};
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr Vector2<T> Vector2<T>::operator*(const Vector2& other) const noexcept
    {
        return {X * other.X, Y * other.Y};
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr Vector2<T> Vector2<T>::operator/(const Vector2& other) const noexcept
    {
        return {X / other.X, Y / other.Y};
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr Vector2<T> Vector2<T>::operator*(T scalar) const noexcept
    {
        return {X * scalar, Y * scalar};
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr Vector2<T> Vector2<T>::operator/(T scalar) const noexcept
    {
        return {X / scalar, Y / scalar};
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr Vector2<T>& Vector2<T>::operator+=(const Vector2& other) noexcept
    {
        X += other.X;
        Y += other.Y;
        return *this;
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr Vector2<T>& Vector2<T>::operator-=(const Vector2& other) noexcept
    {
        X -= other.X;
        Y -= other.Y;
        return *this;
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr Vector2<T>& Vector2<T>::operator*=(const Vector2& other) noexcept
    {
        X *= other.X;
        Y *= other.Y;
        return *this;
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr Vector2<T>& Vector2<T>::operator/=(const Vector2& other) noexcept
    {
        X /= other.X;
        Y /= other.Y;
        return *this;
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr Vector2<T>& Vector2<T>::operator*=(T scalar) noexcept
    {
        X *= scalar;
        Y *= scalar;
        return *this;
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr Vector2<T>& Vector2<T>::operator/=(T scalar) noexcept
    {
        X /= scalar;
        Y /= scalar;
        return *this;
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr bool Vector2<T>::operator==(const Vector2& other) const noexcept
    {
        return X == other.X && Y == other.Y;
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr bool Vector2<T>::operator!=(const Vector2& other) const noexcept
    {
        return !(*this == other);
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr T Vector2<T>::Dot(const Vector2& other) const noexcept
    {
        return X * other.X + Y * other.Y;
    }

    template<Arithmetic T>
    LUDUS_INLINE T Vector2<T>::Length() const noexcept
    {
        return static_cast<T>(std::sqrt(static_cast<double>(LengthSquared())));
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr T Vector2<T>::LengthSquared() const noexcept
    {
        return Dot(*this);
    }

    template<Arithmetic T>
    LUDUS_INLINE Vector2<T> Vector2<T>::Normalized() const noexcept
    {
        const T length = Length();
        if (length == static_cast<T>(0))
        {
            return {};
        }
        return *this / length;
    }

    template<Arithmetic T>
    LUDUS_INLINE void Vector2<T>::Normalize() noexcept
    {
        *this = Normalized();
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr Vector3<T>::Vector3() noexcept
        : X(0)
        , Y(0)
        , Z(0)
    {
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr Vector3<T>::Vector3(T x, T y, T z) noexcept
        : X(x)
        , Y(y)
        , Z(z)
    {
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr Vector3<T> Vector3<T>::operator+(const Vector3& other) const noexcept
    {
        return {X + other.X, Y + other.Y, Z + other.Z};
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr Vector3<T> Vector3<T>::operator-(const Vector3& other) const noexcept
    {
        return {X - other.X, Y - other.Y, Z - other.Z};
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr Vector3<T> Vector3<T>::operator*(const Vector3& other) const noexcept
    {
        return {X * other.X, Y * other.Y, Z * other.Z};
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr Vector3<T> Vector3<T>::operator/(const Vector3& other) const noexcept
    {
        return {X / other.X, Y / other.Y, Z / other.Z};
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr Vector3<T> Vector3<T>::operator*(T scalar) const noexcept
    {
        return {X * scalar, Y * scalar, Z * scalar};
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr Vector3<T> Vector3<T>::operator/(T scalar) const noexcept
    {
        return {X / scalar, Y / scalar, Z / scalar};
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr Vector3<T>& Vector3<T>::operator+=(const Vector3& other) noexcept
    {
        X += other.X;
        Y += other.Y;
        Z += other.Z;
        return *this;
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr Vector3<T>& Vector3<T>::operator-=(const Vector3& other) noexcept
    {
        X -= other.X;
        Y -= other.Y;
        Z -= other.Z;
        return *this;
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr Vector3<T>& Vector3<T>::operator*=(const Vector3& other) noexcept
    {
        X *= other.X;
        Y *= other.Y;
        Z *= other.Z;
        return *this;
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr Vector3<T>& Vector3<T>::operator/=(const Vector3& other) noexcept
    {
        X /= other.X;
        Y /= other.Y;
        Z /= other.Z;
        return *this;
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr Vector3<T>& Vector3<T>::operator*=(T scalar) noexcept
    {
        X *= scalar;
        Y *= scalar;
        Z *= scalar;
        return *this;
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr Vector3<T>& Vector3<T>::operator/=(T scalar) noexcept
    {
        X /= scalar;
        Y /= scalar;
        Z /= scalar;
        return *this;
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr bool Vector3<T>::operator==(const Vector3& other) const noexcept
    {
        return X == other.X && Y == other.Y && Z == other.Z;
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr bool Vector3<T>::operator!=(const Vector3& other) const noexcept
    {
        return !(*this == other);
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr T Vector3<T>::Dot(const Vector3& other) const noexcept
    {
        return X * other.X + Y * other.Y + Z * other.Z;
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr Vector3<T> Vector3<T>::Cross(const Vector3& other) const noexcept
    {
        return {
            Y * other.Z - Z * other.Y,
            Z * other.X - X * other.Z,
            X * other.Y - Y * other.X
        };
    }

    template<Arithmetic T>
    LUDUS_INLINE T Vector3<T>::Length() const noexcept
    {
        return static_cast<T>(std::sqrt(static_cast<double>(LengthSquared())));
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr T Vector3<T>::LengthSquared() const noexcept
    {
        return Dot(*this);
    }

    template<Arithmetic T>
    LUDUS_INLINE Vector3<T> Vector3<T>::Normalized() const noexcept
    {
        const T length = Length();
        if (length == static_cast<T>(0))
        {
            return {};
        }
        return *this / length;
    }

    template<Arithmetic T>
    LUDUS_INLINE void Vector3<T>::Normalize() noexcept
    {
        *this = Normalized();
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr Vector4<T>::Vector4() noexcept
        : X(0)
        , Y(0)
        , Z(0)
        , W(0)
    {
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr Vector4<T>::Vector4(T x, T y, T z, T w) noexcept
        : X(x)
        , Y(y)
        , Z(z)
        , W(w)
    {
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr Vector4<T> Vector4<T>::operator+(const Vector4& other) const noexcept
    {
        return {X + other.X, Y + other.Y, Z + other.Z, W + other.W};
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr Vector4<T> Vector4<T>::operator-(const Vector4& other) const noexcept
    {
        return {X - other.X, Y - other.Y, Z - other.Z, W - other.W};
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr Vector4<T> Vector4<T>::operator*(const Vector4& other) const noexcept
    {
        return {X * other.X, Y * other.Y, Z * other.Z, W * other.W};
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr Vector4<T> Vector4<T>::operator/(const Vector4& other) const noexcept
    {
        return {X / other.X, Y / other.Y, Z / other.Z, W / other.W};
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr Vector4<T> Vector4<T>::operator*(T scalar) const noexcept
    {
        return {X * scalar, Y * scalar, Z * scalar, W * scalar};
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr Vector4<T> Vector4<T>::operator/(T scalar) const noexcept
    {
        return {X / scalar, Y / scalar, Z / scalar, W / scalar};
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr Vector4<T>& Vector4<T>::operator+=(const Vector4& other) noexcept
    {
        X += other.X;
        Y += other.Y;
        Z += other.Z;
        W += other.W;
        return *this;
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr Vector4<T>& Vector4<T>::operator-=(const Vector4& other) noexcept
    {
        X -= other.X;
        Y -= other.Y;
        Z -= other.Z;
        W -= other.W;
        return *this;
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr Vector4<T>& Vector4<T>::operator*=(const Vector4& other) noexcept
    {
        X *= other.X;
        Y *= other.Y;
        Z *= other.Z;
        W *= other.W;
        return *this;
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr Vector4<T>& Vector4<T>::operator/=(const Vector4& other) noexcept
    {
        X /= other.X;
        Y /= other.Y;
        Z /= other.Z;
        W /= other.W;
        return *this;
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr Vector4<T>& Vector4<T>::operator*=(T scalar) noexcept
    {
        X *= scalar;
        Y *= scalar;
        Z *= scalar;
        W *= scalar;
        return *this;
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr Vector4<T>& Vector4<T>::operator/=(T scalar) noexcept
    {
        X /= scalar;
        Y /= scalar;
        Z /= scalar;
        W /= scalar;
        return *this;
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr bool Vector4<T>::operator==(const Vector4& other) const noexcept
    {
        return X == other.X && Y == other.Y && Z == other.Z && W == other.W;
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr bool Vector4<T>::operator!=(const Vector4& other) const noexcept
    {
        return !(*this == other);
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr T Vector4<T>::Dot(const Vector4& other) const noexcept
    {
        return X * other.X + Y * other.Y + Z * other.Z + W * other.W;
    }

    template<Arithmetic T>
    LUDUS_INLINE T Vector4<T>::Length() const noexcept
    {
        return static_cast<T>(std::sqrt(static_cast<double>(LengthSquared())));
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr T Vector4<T>::LengthSquared() const noexcept
    {
        return Dot(*this);
    }

    template<Arithmetic T>
    LUDUS_INLINE Vector4<T> Vector4<T>::Normalized() const noexcept
    {
        const T length = Length();
        if (length == static_cast<T>(0))
        {
            return {};
        }
        return *this / length;
    }

    template<Arithmetic T>
    LUDUS_INLINE void Vector4<T>::Normalize() noexcept
    {
        *this = Normalized();
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr Vector2<T> operator*(T scalar, const Vector2<T>& value) noexcept
    {
        return value * scalar;
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr Vector3<T> operator*(T scalar, const Vector3<T>& value) noexcept
    {
        return value * scalar;
    }

    template<Arithmetic T>
    LUDUS_INLINE constexpr Vector4<T> operator*(T scalar, const Vector4<T>& value) noexcept
    {
        return value * scalar;
    }
} // namespace ludus::core
