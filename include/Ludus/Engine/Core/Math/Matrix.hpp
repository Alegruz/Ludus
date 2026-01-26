#pragma once

#include <Ludus/Engine/Core/Math/Matrix.h>

namespace ludus::core
{
    template<std::floating_point T>
    LUDUS_INLINE constexpr Matrix3<T>::Matrix3() noexcept
        : M00(0)
        , M01(0)
        , M02(0)
        , M10(0)
        , M11(0)
        , M12(0)
        , M20(0)
        , M21(0)
        , M22(0)
    {
    }

    template<std::floating_point T>
    LUDUS_INLINE constexpr Matrix3<T>::Matrix3(
        T m00, T m01, T m02,
        T m10, T m11, T m12,
        T m20, T m21, T m22) noexcept
        : M00(m00)
        , M01(m01)
        , M02(m02)
        , M10(m10)
        , M11(m11)
        , M12(m12)
        , M20(m20)
        , M21(m21)
        , M22(m22)
    {
    }

    template<std::floating_point T>
    LUDUS_INLINE constexpr Matrix3<T> Matrix3<T>::Identity() noexcept
    {
        return Matrix3(
            static_cast<T>(1), static_cast<T>(0), static_cast<T>(0),
            static_cast<T>(0), static_cast<T>(1), static_cast<T>(0),
            static_cast<T>(0), static_cast<T>(0), static_cast<T>(1));
    }

    template<std::floating_point T>
    LUDUS_INLINE constexpr Matrix3<T> Matrix3<T>::operator*(const Matrix3& other) const noexcept
    {
        return Matrix3(
            M00 * other.M00 + M01 * other.M10 + M02 * other.M20,
            M00 * other.M01 + M01 * other.M11 + M02 * other.M21,
            M00 * other.M02 + M01 * other.M12 + M02 * other.M22,
            M10 * other.M00 + M11 * other.M10 + M12 * other.M20,
            M10 * other.M01 + M11 * other.M11 + M12 * other.M21,
            M10 * other.M02 + M11 * other.M12 + M12 * other.M22,
            M20 * other.M00 + M21 * other.M10 + M22 * other.M20,
            M20 * other.M01 + M21 * other.M11 + M22 * other.M21,
            M20 * other.M02 + M21 * other.M12 + M22 * other.M22);
    }

    template<std::floating_point T>
    LUDUS_INLINE constexpr Vector3<T> Matrix3<T>::operator*(const Vector3<T>& value) const noexcept
    {
        return {
            M00 * value.X + M01 * value.Y + M02 * value.Z,
            M10 * value.X + M11 * value.Y + M12 * value.Z,
            M20 * value.X + M21 * value.Y + M22 * value.Z};
    }

    template<std::floating_point T>
    LUDUS_INLINE constexpr Matrix3<T> Matrix3<T>::Transposed() const noexcept
    {
        return Matrix3(
            M00, M10, M20,
            M01, M11, M21,
            M02, M12, M22);
    }

    template<std::floating_point T>
    LUDUS_INLINE constexpr Matrix4<T>::Matrix4() noexcept
        : M00(0)
        , M01(0)
        , M02(0)
        , M03(0)
        , M10(0)
        , M11(0)
        , M12(0)
        , M13(0)
        , M20(0)
        , M21(0)
        , M22(0)
        , M23(0)
        , M30(0)
        , M31(0)
        , M32(0)
        , M33(0)
    {
    }

    template<std::floating_point T>
    LUDUS_INLINE constexpr Matrix4<T>::Matrix4(
        T m00, T m01, T m02, T m03,
        T m10, T m11, T m12, T m13,
        T m20, T m21, T m22, T m23,
        T m30, T m31, T m32, T m33) noexcept
        : M00(m00)
        , M01(m01)
        , M02(m02)
        , M03(m03)
        , M10(m10)
        , M11(m11)
        , M12(m12)
        , M13(m13)
        , M20(m20)
        , M21(m21)
        , M22(m22)
        , M23(m23)
        , M30(m30)
        , M31(m31)
        , M32(m32)
        , M33(m33)
    {
    }

    template<std::floating_point T>
    LUDUS_INLINE constexpr Matrix4<T> Matrix4<T>::Identity() noexcept
    {
        return Matrix4(
            static_cast<T>(1), static_cast<T>(0), static_cast<T>(0), static_cast<T>(0),
            static_cast<T>(0), static_cast<T>(1), static_cast<T>(0), static_cast<T>(0),
            static_cast<T>(0), static_cast<T>(0), static_cast<T>(1), static_cast<T>(0),
            static_cast<T>(0), static_cast<T>(0), static_cast<T>(0), static_cast<T>(1));
    }

    template<std::floating_point T>
    LUDUS_INLINE constexpr Matrix4<T> Matrix4<T>::operator*(const Matrix4& other) const noexcept
    {
        return Matrix4(
            M00 * other.M00 + M01 * other.M10 + M02 * other.M20 + M03 * other.M30,
            M00 * other.M01 + M01 * other.M11 + M02 * other.M21 + M03 * other.M31,
            M00 * other.M02 + M01 * other.M12 + M02 * other.M22 + M03 * other.M32,
            M00 * other.M03 + M01 * other.M13 + M02 * other.M23 + M03 * other.M33,
            M10 * other.M00 + M11 * other.M10 + M12 * other.M20 + M13 * other.M30,
            M10 * other.M01 + M11 * other.M11 + M12 * other.M21 + M13 * other.M31,
            M10 * other.M02 + M11 * other.M12 + M12 * other.M22 + M13 * other.M32,
            M10 * other.M03 + M11 * other.M13 + M12 * other.M23 + M13 * other.M33,
            M20 * other.M00 + M21 * other.M10 + M22 * other.M20 + M23 * other.M30,
            M20 * other.M01 + M21 * other.M11 + M22 * other.M21 + M23 * other.M31,
            M20 * other.M02 + M21 * other.M12 + M22 * other.M22 + M23 * other.M32,
            M20 * other.M03 + M21 * other.M13 + M22 * other.M23 + M23 * other.M33,
            M30 * other.M00 + M31 * other.M10 + M32 * other.M20 + M33 * other.M30,
            M30 * other.M01 + M31 * other.M11 + M32 * other.M21 + M33 * other.M31,
            M30 * other.M02 + M31 * other.M12 + M32 * other.M22 + M33 * other.M32,
            M30 * other.M03 + M31 * other.M13 + M32 * other.M23 + M33 * other.M33);
    }

    template<std::floating_point T>
    LUDUS_INLINE constexpr Vector4<T> Matrix4<T>::operator*(const Vector4<T>& value) const noexcept
    {
        return {
            M00 * value.X + M01 * value.Y + M02 * value.Z + M03 * value.W,
            M10 * value.X + M11 * value.Y + M12 * value.Z + M13 * value.W,
            M20 * value.X + M21 * value.Y + M22 * value.Z + M23 * value.W,
            M30 * value.X + M31 * value.Y + M32 * value.Z + M33 * value.W};
    }

    template<std::floating_point T>
    LUDUS_INLINE constexpr Matrix4<T> Matrix4<T>::Transposed() const noexcept
    {
        return Matrix4(
            M00, M10, M20, M30,
            M01, M11, M21, M31,
            M02, M12, M22, M32,
            M03, M13, M23, M33);
    }

    template<std::floating_point T>
    LUDUS_INLINE constexpr Vector3<T> Matrix4<T>::TransformPoint(const Vector3<T>& value) const noexcept
    {
        const Vector4<T> transformed = (*this) * Vector4<T>(value.X, value.Y, value.Z, static_cast<T>(1));
        return {transformed.X, transformed.Y, transformed.Z};
    }

    template<std::floating_point T>
    LUDUS_INLINE constexpr Vector3<T> Matrix4<T>::TransformVector(const Vector3<T>& value) const noexcept
    {
        const Vector4<T> transformed = (*this) * Vector4<T>(value.X, value.Y, value.Z, static_cast<T>(0));
        return {transformed.X, transformed.Y, transformed.Z};
    }
} // namespace ludus::core
