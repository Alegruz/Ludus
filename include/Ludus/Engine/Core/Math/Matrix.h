#pragma once

#include <Ludus/Engine/Core/Common.h>
#include <Ludus/Engine/Core/Math/Vector.h>

#include <concepts>

namespace ludus::core
{
    template<std::floating_point T>
    struct Matrix3 final
    {
        T M00;
        T M01;
        T M02;
        T M10;
        T M11;
        T M12;
        T M20;
        T M21;
        T M22;

        constexpr Matrix3() noexcept;
        constexpr Matrix3(
            T m00, T m01, T m02,
            T m10, T m11, T m12,
            T m20, T m21, T m22) noexcept;

        [[nodiscard]] static constexpr Matrix3 Identity() noexcept;

        [[nodiscard]] constexpr Matrix3 operator*(const Matrix3& other) const noexcept;
        [[nodiscard]] constexpr Vector3<T> operator*(const Vector3<T>& value) const noexcept;
        [[nodiscard]] constexpr Matrix3 Transposed() const noexcept;
    };

    template<std::floating_point T>
    struct Matrix4 final
    {
        T M00;
        T M01;
        T M02;
        T M03;
        T M10;
        T M11;
        T M12;
        T M13;
        T M20;
        T M21;
        T M22;
        T M23;
        T M30;
        T M31;
        T M32;
        T M33;

        constexpr Matrix4() noexcept;
        constexpr Matrix4(
            T m00, T m01, T m02, T m03,
            T m10, T m11, T m12, T m13,
            T m20, T m21, T m22, T m23,
            T m30, T m31, T m32, T m33) noexcept;

        [[nodiscard]] static constexpr Matrix4 Identity() noexcept;

        [[nodiscard]] constexpr Matrix4 operator*(const Matrix4& other) const noexcept;
        [[nodiscard]] constexpr Vector4<T> operator*(const Vector4<T>& value) const noexcept;
        [[nodiscard]] constexpr Matrix4 Transposed() const noexcept;

        [[nodiscard]] constexpr Vector3<T> TransformPoint(const Vector3<T>& value) const noexcept;
        [[nodiscard]] constexpr Vector3<T> TransformVector(const Vector3<T>& value) const noexcept;
    };

    using Matrix3F = Matrix3<float>;
    using Matrix4F = Matrix4<float>;
} // namespace ludus::core
