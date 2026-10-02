#pragma once

// Independent, higher-precision test oracles for FoundationMath. These helpers
// MUST NOT call the production function they are used to verify: they are hand
// written in double (or long double where it helps) so a comparison is a real
// cross-check, not a tautology. Their own supported domain is noted per helper.

#include <ludus/foundation/math/quaternion.hpp>
#include <ludus/foundation/math/vector.hpp>

#include <cmath>

namespace ludus::math_test
{
using ludus::foundation::math::Quaternion;
using ludus::foundation::math::Vector3;

// Angle between two unit-ish vectors measured with atan2 of |cross|/dot — stable
// near 0 and Pi, unlike acos of a nearly-one dot. Inputs need not be exactly
// unit; the result is the geometric angle in radians.
[[nodiscard]] inline double AngleBetween(Vector3 a, Vector3 b) noexcept
{
    const double ax = a.X, ay = a.Y, az = a.Z, bx = b.X, by = b.Y, bz = b.Z;
    const double cx = ay * bz - az * by;
    const double cy = az * bx - ax * bz;
    const double cz = ax * by - ay * bx;
    const double crossLen = std::sqrt(cx * cx + cy * cy + cz * cz);
    const double dot = ax * bx + ay * by + az * bz;
    return std::atan2(crossLen, dot);
}

// Reference rotation of v by a unit quaternion using the q*(0,v)*conj(q) product
// expanded independently of the production Rotate(). Double throughout.
[[nodiscard]] inline Vector3 RotateReference(Quaternion q, Vector3 v) noexcept
{
    const double qx = q.X, qy = q.Y, qz = q.Z, qw = q.W;
    // p' = q * (0, v) * conjugate(q). Compute q*(0,v) = r, then r*conj(q).
    // r = (rw, rv): rw = -dot(qv, v); rv = qw*v + cross(qv, v).
    const double rw = -(qx * v.X + qy * v.Y + qz * v.Z);
    const double rvx = qw * v.X + (qy * v.Z - qz * v.Y);
    const double rvy = qw * v.Y + (qz * v.X - qx * v.Z);
    const double rvz = qw * v.Z + (qx * v.Y - qy * v.X);
    // result vector part of r * conj(q), conj(q) = (-qv, qw).
    const double cx = -qx, cy = -qy, cz = -qz, cw = qw;
    const double ox = rw * cx + cw * rvx + (rvy * cz - rvz * cy);
    const double oy = rw * cy + cw * rvy + (rvz * cx - rvx * cz);
    const double oz = rw * cz + cw * rvz + (rvx * cy - rvy * cx);
    return Vector3{static_cast<float>(ox), static_cast<float>(oy), static_cast<float>(oz)};
}

// Reference quaternion length (double).
[[nodiscard]] inline double QuatLen(Quaternion q) noexcept
{
    return std::sqrt((double)q.X * q.X + (double)q.Y * q.Y + (double)q.Z * q.Z + (double)q.W * q.W);
}

// Reference vector length (double, no scaling trick — valid for moderate values).
[[nodiscard]] inline double VecLen(Vector3 v) noexcept
{
    return std::sqrt((double)v.X * v.X + (double)v.Y * v.Y + (double)v.Z * v.Z);
}
} // namespace ludus::math_test
