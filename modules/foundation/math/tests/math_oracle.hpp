#pragma once

// Independent, higher-precision test oracles for FoundationMath. These helpers
// MUST NOT call the production function they are used to verify: they are hand
// written in double (or long double where it helps) so a comparison is a real
// cross-check, not a tautology. Their own supported domain is noted per helper.
//
// Everything widens float32 components to double EXPLICITLY: the project builds
// with -Wdouble-promotion -Werror, so implicit float->double promotion is an
// error here too.

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
    const double ax = static_cast<double>(a.X);
    const double ay = static_cast<double>(a.Y);
    const double az = static_cast<double>(a.Z);
    const double bx = static_cast<double>(b.X);
    const double by = static_cast<double>(b.Y);
    const double bz = static_cast<double>(b.Z);
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
    const double qx = static_cast<double>(q.X);
    const double qy = static_cast<double>(q.Y);
    const double qz = static_cast<double>(q.Z);
    const double qw = static_cast<double>(q.W);
    const double vx = static_cast<double>(v.X);
    const double vy = static_cast<double>(v.Y);
    const double vz = static_cast<double>(v.Z);
    // p' = q * (0, v) * conjugate(q). Compute q*(0,v) = r, then r*conj(q).
    // r = (rw, rv): rw = -dot(qv, v); rv = qw*v + cross(qv, v).
    const double rw = -(qx * vx + qy * vy + qz * vz);
    const double rvx = qw * vx + (qy * vz - qz * vy);
    const double rvy = qw * vy + (qz * vx - qx * vz);
    const double rvz = qw * vz + (qx * vy - qy * vx);
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
    const double x = static_cast<double>(q.X);
    const double y = static_cast<double>(q.Y);
    const double z = static_cast<double>(q.Z);
    const double w = static_cast<double>(q.W);
    return std::sqrt(x * x + y * y + z * z + w * w);
}

// Reference vector length (double, no scaling trick — valid for moderate values).
[[nodiscard]] inline double VecLen(Vector3 v) noexcept
{
    const double x = static_cast<double>(v.X);
    const double y = static_cast<double>(v.Y);
    const double z = static_cast<double>(v.Z);
    return std::sqrt(x * x + y * y + z * z);
}
} // namespace ludus::math_test
