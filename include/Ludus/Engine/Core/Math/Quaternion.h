#pragma once

#include <Ludus/Engine/Core/Common.h>
#include <Ludus/Engine/Core/Math/Matrix.h>
#include <Ludus/Engine/Core/Math/Vector.h>

#include <concepts>

namespace ludus::core
{
    template<std::floating_point T>
    struct Quaternion final
    {
        T X;
        T Y;
        T Z;
        T W;

        constexpr Quaternion() noexcept;
        constexpr Quaternion(T x, T y, T z, T w) noexcept;

        [[nodiscard]] static constexpr Quaternion Identity() noexcept;
        [[nodiscard]] static Quaternion FromAxisAngle(const Vector3<T>& axis, T radians) noexcept;
        [[nodiscard]] static Quaternion FromRotationMatrix(const Matrix3<T>& matrix) noexcept;

        [[nodiscard]] constexpr Quaternion operator*(const Quaternion& other) const noexcept;
        [[nodiscard]] constexpr Quaternion operator-() const noexcept;
        [[nodiscard]] constexpr Quaternion operator*(T scalar) const noexcept;

        [[nodiscard]] constexpr T Dot(const Quaternion& other) const noexcept;
        [[nodiscard]] T Length() const noexcept;
        [[nodiscard]] constexpr T LengthSquared() const noexcept;
        [[nodiscard]] Quaternion Normalized() const noexcept;
        void Normalize() noexcept;

        [[nodiscard]] constexpr Quaternion Conjugate() const noexcept;
        [[nodiscard]] Quaternion Inverse() const noexcept;

        [[nodiscard]] Vector3<T> Rotate(const Vector3<T>& value) const noexcept;
        [[nodiscard]] Matrix3<T> ToRotationMatrix() const noexcept;

        [[nodiscard]] Vector3<T> Axis() const noexcept;
        [[nodiscard]] T Angle() const noexcept;

        [[nodiscard]] static Quaternion Nlerp(const Quaternion& from, const Quaternion& to, T t) noexcept;
        [[nodiscard]] static Quaternion Slerp(const Quaternion& from, const Quaternion& to, T t) noexcept;
    };

    using QuaternionF = Quaternion<float>;
} // namespace ludus::core
