#pragma once

// Quaternion: Hamilton convention, components stored (x, y, z, w), default
// identity (0,0,0,1). Unit quaternions represent rotations; the rotation of q
// equals the rotation of -q. Composition a*b rotates by b then a (ADR 0010 §3).
//
// Trusted operations (operator*, Conjugate, Rotate, Nlerp/SlerpShortest,
// SameRotation) assume unit inputs where documented; checked factories
// (TryFromAxisAngle, TryRotationBetween) validate and normalise. Robust
// algorithms (design §6) live in quaternion.cpp.

#include <ludus/foundation/base/core.h>

#include <ludus/foundation/math/status.hpp>
#include <ludus/foundation/math/vector.hpp>

namespace ludus::foundation::math
{
using core::float32;

struct Quaternion
{
    float32 X = 0.0f;
    float32 Y = 0.0f;
    float32 Z = 0.0f;
    float32 W = 1.0f; // identity

    // Exact componentwise equality (not rotation equivalence; q != -q here).
    [[nodiscard]] friend constexpr bool operator==(const Quaternion&, const Quaternion&) = default;
};

// QuaternionIdentity — explicit identity factory for readable call sites.
[[nodiscard]] constexpr Quaternion QuaternionIdentity() noexcept
{
    return Quaternion{0.0f, 0.0f, 0.0f, 1.0f};
}

// Hamilton product. a*b composes rotations: apply b, then a. Does NOT normalise
// (repeated composition renormalises at an explicit boundary via TryNormalize).
[[nodiscard]] constexpr Quaternion operator*(Quaternion a, Quaternion b) noexcept
{
    // vector(a*b) = aw*bv + bw*av + cross(av,bv); scalar = aw*bw - dot(av,bv).
    return Quaternion{
        a.W * b.X + b.W * a.X + (a.Y * b.Z - a.Z * b.Y),
        a.W * b.Y + b.W * a.Y + (a.Z * b.X - a.X * b.Z),
        a.W * b.Z + b.W * a.Z + (a.X * b.Y - a.Y * b.X),
        a.W * b.W - (a.X * b.X + a.Y * b.Y + a.Z * b.Z),
    };
}

// Conjugate negates the vector part. For a UNIT quaternion this is the inverse
// rotation; it is not a general non-unit inverse (not provided in v1).
[[nodiscard]] constexpr Quaternion Conjugate(Quaternion q) noexcept
{
    return Quaternion{-q.X, -q.Y, -q.Z, q.W};
}

[[nodiscard]] constexpr float32 Dot(Quaternion a, Quaternion b) noexcept
{
    return a.X * b.X + a.Y * b.Y + a.Z * b.Z + a.W * b.W;
}

[[nodiscard]] constexpr bool IsFinite(Quaternion q) noexcept
{
    return IsFinite(q.X) && IsFinite(q.Y) && IsFinite(q.Z) && IsFinite(q.W);
}

// Checked normalization of a quaternion (same contract as vector TryNormalize;
// zero fails Degenerate, non-finite fails NonFiniteInput). Output unchanged on
// failure; in-place aliasing supported.
[[nodiscard]] MathStatus TryNormalize(Quaternion q, Quaternion& out) noexcept;

// TryFromAxisAngle: finite non-zero axis (normalised internally), finite angle
// in radians (the physical rotation angle, not a half-angle). Builds
// (axis*sin(angle/2), cos(angle/2)) via double intermediates and returns a
// checked unit quaternion. Zero axis -> Degenerate; non-finite -> NonFiniteInput.
[[nodiscard]] MathStatus TryFromAxisAngle(Vector3 axis, float32 angleRadians, Quaternion& out) noexcept;

// Rotate a vector by a UNIT quaternion q. Equivalent to Matrix3(q)*v. Trusted:
// q is a caller obligation (asserted in development). No quaternion*point
// operator exists.
[[nodiscard]] Vector3 Rotate(Quaternion q, Vector3 v) noexcept;

// TryRotationBetween: the shortest-arc rotation mapping unit(from) to unit(to).
// Robustly normalises both in double; handles equal, opposite and near-opposite
// directions deterministically (design §6). Zero directions -> Degenerate.
[[nodiscard]] MathStatus TryRotationBetween(Vector3 from, Vector3 to, Quaternion& out) noexcept;

// Shortest-path interpolation. Trusted unit a,b and finite t in [0,1]. Both
// negate b when dot(a,b)<0 so the shorter arc is taken; both return a at t=0 and
// the hemisphere-corrected b at t=1 (the same rotation as b). Nlerp normalises a
// linear blend; Slerp uses spherical interpolation with a normalised close-angle
// fallback. Neither divides by zero for identical or opposite-sign inputs.
[[nodiscard]] Quaternion NlerpShortest(Quaternion a, Quaternion b, float32 t) noexcept;
[[nodiscard]] Quaternion SlerpShortest(Quaternion a, Quaternion b, float32 t) noexcept;

// SameRotation: do unit a and b represent the same rotation within an angular
// tolerance in [0, Pi]? Compares the clamped |dot(a,b)| against cos(tol/2) in
// double. This is NOT operator== (which stays componentwise). Non-unit inputs
// or an out-of-range tolerance return false.
[[nodiscard]] bool SameRotation(Quaternion a, Quaternion b, float32 toleranceRadians) noexcept;
} // namespace ludus::foundation::math
