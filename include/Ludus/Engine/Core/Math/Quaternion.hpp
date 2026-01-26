#pragma once

#include <Ludus/Engine/Core/Math/Interpolation.h>
#include <Ludus/Engine/Core/Math/Quaternion.h>
#include <Ludus/Engine/Core/Math/Trigonometry.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace ludus::core
{
    template<std::floating_point T>
    LUDUS_INLINE constexpr Quaternion<T>::Quaternion() noexcept
        : X(0)
        , Y(0)
        , Z(0)
        , W(1)
    {
    }

    template<std::floating_point T>
    LUDUS_INLINE constexpr Quaternion<T>::Quaternion(T x, T y, T z, T w) noexcept
        : X(x)
        , Y(y)
        , Z(z)
        , W(w)
    {
    }

    template<std::floating_point T>
    LUDUS_INLINE constexpr Quaternion<T> Quaternion<T>::Identity() noexcept
    {
        return Quaternion(static_cast<T>(0), static_cast<T>(0), static_cast<T>(0), static_cast<T>(1));
    }

    template<std::floating_point T>
    LUDUS_INLINE Quaternion<T> Quaternion<T>::FromAxisAngle(const Vector3<T>& axis, T radians) noexcept
    {
        const T axisLength = axis.Length();
        if (axisLength <= std::numeric_limits<T>::epsilon())
        {
            return Identity();
        }

        const Vector3<T> normAxis = axis / axisLength;
        const T halfAngle = radians * static_cast<T>(0.5);
        const T sinHalf = std::sin(halfAngle);
        const T cosHalf = std::cos(halfAngle);
        return Quaternion(
            normAxis.X * sinHalf,
            normAxis.Y * sinHalf,
            normAxis.Z * sinHalf,
            cosHalf);
    }

    template<std::floating_point T>
    LUDUS_INLINE Quaternion<T> Quaternion<T>::FromRotationMatrix(const Matrix3<T>& matrix) noexcept
    {
        const T trace = matrix.M00 + matrix.M11 + matrix.M22;
        if (trace > static_cast<T>(0))
        {
            const T root = std::sqrt(trace + static_cast<T>(1));
            const T half = static_cast<T>(0.5) / root;
            return Quaternion(
                (matrix.M21 - matrix.M12) * half,
                (matrix.M02 - matrix.M20) * half,
                (matrix.M10 - matrix.M01) * half,
                root * static_cast<T>(0.5));
        }

        if (matrix.M00 > matrix.M11 && matrix.M00 > matrix.M22)
        {
            const T root = std::sqrt(static_cast<T>(1) + matrix.M00 - matrix.M11 - matrix.M22);
            const T half = static_cast<T>(0.5) / root;
            return Quaternion(
                root * static_cast<T>(0.5),
                (matrix.M01 + matrix.M10) * half,
                (matrix.M02 + matrix.M20) * half,
                (matrix.M21 - matrix.M12) * half);
        }

        if (matrix.M11 > matrix.M22)
        {
            const T root = std::sqrt(static_cast<T>(1) + matrix.M11 - matrix.M00 - matrix.M22);
            const T half = static_cast<T>(0.5) / root;
            return Quaternion(
                (matrix.M01 + matrix.M10) * half,
                root * static_cast<T>(0.5),
                (matrix.M12 + matrix.M21) * half,
                (matrix.M02 - matrix.M20) * half);
        }

        const T root = std::sqrt(static_cast<T>(1) + matrix.M22 - matrix.M00 - matrix.M11);
        const T half = static_cast<T>(0.5) / root;
        return Quaternion(
            (matrix.M02 + matrix.M20) * half,
            (matrix.M12 + matrix.M21) * half,
            root * static_cast<T>(0.5),
            (matrix.M10 - matrix.M01) * half);
    }

    template<std::floating_point T>
    LUDUS_INLINE constexpr Quaternion<T> Quaternion<T>::operator*(const Quaternion& other) const noexcept
    {
        return Quaternion(
            W * other.X + X * other.W + Y * other.Z - Z * other.Y,
            W * other.Y - X * other.Z + Y * other.W + Z * other.X,
            W * other.Z + X * other.Y - Y * other.X + Z * other.W,
            W * other.W - X * other.X - Y * other.Y - Z * other.Z);
    }

    template<std::floating_point T>
    LUDUS_INLINE constexpr Quaternion<T> Quaternion<T>::operator-() const noexcept
    {
        return Quaternion(-X, -Y, -Z, -W);
    }

    template<std::floating_point T>
    LUDUS_INLINE constexpr Quaternion<T> Quaternion<T>::operator*(T scalar) const noexcept
    {
        return Quaternion(X * scalar, Y * scalar, Z * scalar, W * scalar);
    }

    template<std::floating_point T>
    LUDUS_INLINE constexpr T Quaternion<T>::Dot(const Quaternion& other) const noexcept
    {
        return X * other.X + Y * other.Y + Z * other.Z + W * other.W;
    }

    template<std::floating_point T>
    LUDUS_INLINE T Quaternion<T>::Length() const noexcept
    {
        return static_cast<T>(std::sqrt(static_cast<double>(LengthSquared())));
    }

    template<std::floating_point T>
    LUDUS_INLINE constexpr T Quaternion<T>::LengthSquared() const noexcept
    {
        return Dot(*this);
    }

    template<std::floating_point T>
    LUDUS_INLINE Quaternion<T> Quaternion<T>::Normalized() const noexcept
    {
        const T length = Length();
        if (length <= std::numeric_limits<T>::epsilon())
        {
            return Identity();
        }
        return (*this) * (static_cast<T>(1) / length);
    }

    template<std::floating_point T>
    LUDUS_INLINE void Quaternion<T>::Normalize() noexcept
    {
        *this = Normalized();
    }

    template<std::floating_point T>
    LUDUS_INLINE constexpr Quaternion<T> Quaternion<T>::Conjugate() const noexcept
    {
        return Quaternion(-X, -Y, -Z, W);
    }

    template<std::floating_point T>
    LUDUS_INLINE Quaternion<T> Quaternion<T>::Inverse() const noexcept
    {
        const T lengthSq = LengthSquared();
        if (lengthSq <= std::numeric_limits<T>::epsilon())
        {
            return Identity();
        }
        return Conjugate() * (static_cast<T>(1) / lengthSq);
    }

    template<std::floating_point T>
    LUDUS_INLINE Vector3<T> Quaternion<T>::Rotate(const Vector3<T>& value) const noexcept
    {
        const Vector3<T> qVec{X, Y, Z};
        const Vector3<T> t = static_cast<T>(2) * qVec.Cross(value);
        return value + (W * t) + qVec.Cross(t);
    }

    template<std::floating_point T>
    LUDUS_INLINE Matrix3<T> Quaternion<T>::ToRotationMatrix() const noexcept
    {
        const Quaternion normalized = Normalized();
        const T xx = normalized.X * normalized.X;
        const T yy = normalized.Y * normalized.Y;
        const T zz = normalized.Z * normalized.Z;
        const T xy = normalized.X * normalized.Y;
        const T xz = normalized.X * normalized.Z;
        const T yz = normalized.Y * normalized.Z;
        const T wx = normalized.W * normalized.X;
        const T wy = normalized.W * normalized.Y;
        const T wz = normalized.W * normalized.Z;

        return Matrix3<T>(
            static_cast<T>(1) - static_cast<T>(2) * (yy + zz),
            static_cast<T>(2) * (xy - wz),
            static_cast<T>(2) * (xz + wy),
            static_cast<T>(2) * (xy + wz),
            static_cast<T>(1) - static_cast<T>(2) * (xx + zz),
            static_cast<T>(2) * (yz - wx),
            static_cast<T>(2) * (xz - wy),
            static_cast<T>(2) * (yz + wx),
            static_cast<T>(1) - static_cast<T>(2) * (xx + yy));
    }

    template<std::floating_point T>
    LUDUS_INLINE Vector3<T> Quaternion<T>::Axis() const noexcept
    {
        const Quaternion normalized = Normalized();
        const T sinHalf = std::sqrt(std::max(static_cast<T>(0), static_cast<T>(1) - normalized.W * normalized.W));
        if (sinHalf <= std::numeric_limits<T>::epsilon())
        {
            return Vector3<T>(static_cast<T>(1), static_cast<T>(0), static_cast<T>(0));
        }
        return Vector3<T>(normalized.X, normalized.Y, normalized.Z) / sinHalf;
    }

    template<std::floating_point T>
    LUDUS_INLINE T Quaternion<T>::Angle() const noexcept
    {
        const Quaternion normalized = Normalized();
        const T clamped = std::clamp(normalized.W, static_cast<T>(-1), static_cast<T>(1));
        return static_cast<T>(2) * std::acos(clamped);
    }

    template<std::floating_point T>
    LUDUS_INLINE Quaternion<T> Quaternion<T>::Nlerp(const Quaternion& from, const Quaternion& to, T t) noexcept
    {
        const T clamped = Clamp01(t);
        Quaternion end = to;
        if (from.Dot(to) < static_cast<T>(0))
        {
            end = -to;
        }

        const Quaternion blended(
            from.X + (end.X - from.X) * clamped,
            from.Y + (end.Y - from.Y) * clamped,
            from.Z + (end.Z - from.Z) * clamped,
            from.W + (end.W - from.W) * clamped);
        return blended.Normalized();
    }

    template<std::floating_point T>
    LUDUS_INLINE Quaternion<T> Quaternion<T>::Slerp(const Quaternion& from, const Quaternion& to, T t) noexcept
    {
        const T clamped = Clamp01(t);
        Quaternion end = to;
        T cosTheta = from.Dot(to);
        if (cosTheta < static_cast<T>(0))
        {
            end = -to;
            cosTheta = -cosTheta;
        }

        if (cosTheta > static_cast<T>(0.9995))
        {
            return Nlerp(from, end, clamped);
        }

        const T angle = std::acos(std::clamp(cosTheta, static_cast<T>(-1), static_cast<T>(1)));
        const T sinAngle = std::sin(angle);
        if (sinAngle <= std::numeric_limits<T>::epsilon())
        {
            return from;
        }

        const T invSin = static_cast<T>(1) / sinAngle;
        const T weightFrom = std::sin((static_cast<T>(1) - clamped) * angle) * invSin;
        const T weightTo = std::sin(clamped * angle) * invSin;

        return Quaternion(
            from.X * weightFrom + end.X * weightTo,
            from.Y * weightFrom + end.Y * weightTo,
            from.Z * weightFrom + end.Z * weightTo,
            from.W * weightFrom + end.W * weightTo);
    }
} // namespace ludus::core
