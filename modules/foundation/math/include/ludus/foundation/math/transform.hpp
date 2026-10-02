#pragma once

// Affine transforms and authoring TRS (design §7).
//
// Affine2 = { 2 basis columns (L), translation t }; Affine3 = { 3 basis columns
// (L), translation t }. Both default to identity. Points, vectors and normals
// are transformed by NAMED functions (never one generic overload): points add
// the translation, vectors do not, normals use the inverse-transpose of L.
//
// TransformTRS (translation, unit rotation, scale) is AUTHORING DATA. It
// converts to an Affine3 (L = Rotation(q) * Diagonal(scale)); parent non-uniform
// scale composed with child rotation produces shear, which is retained because
// composition happens on Affine3. There is deliberately no TRS*TRS and no
// general TRS inverse/decomposition in v1.

#include <ludus/foundation/base/core.h>

#include <ludus/foundation/math/matrix.hpp>
#include <ludus/foundation/math/quaternion.hpp>
#include <ludus/foundation/math/status.hpp>
#include <ludus/foundation/math/vector.hpp>

namespace ludus::foundation::math
{
using core::float32;

// ===========================================================================
// Affine2
// ===========================================================================
struct Affine2
{
    Vector2 Columns[2] = {Vector2{1, 0}, Vector2{0, 1}}; // linear block L
    Vector2 Translation = {0, 0};

    [[nodiscard]] static constexpr Affine2 Identity() noexcept
    {
        return Affine2{};
    }
    [[nodiscard]] friend constexpr bool operator==(const Affine2&, const Affine2&) = default;
};

[[nodiscard]] constexpr Vector2 TransformPoint(const Affine2& a, Vector2 p) noexcept
{
    return a.Columns[0] * p.X + a.Columns[1] * p.Y + a.Translation;
}
[[nodiscard]] constexpr Vector2 TransformVector(const Affine2& a, Vector2 v) noexcept
{
    return a.Columns[0] * v.X + a.Columns[1] * v.Y;
}
[[nodiscard]] constexpr Affine2 Compose(const Affine2& a, const Affine2& b) noexcept
{
    // (a * b): apply b, then a. L = a.L * b.L; t = a.L * b.t + a.t.
    return Affine2{{a.Columns[0] * b.Columns[0].X + a.Columns[1] * b.Columns[0].Y,
                    a.Columns[0] * b.Columns[1].X + a.Columns[1] * b.Columns[1].Y},
                   a.Columns[0] * b.Translation.X + a.Columns[1] * b.Translation.Y + a.Translation};
}
[[nodiscard]] constexpr bool IsFinite(const Affine2& a) noexcept
{
    return IsFinite(a.Columns[0]) && IsFinite(a.Columns[1]) && IsFinite(a.Translation);
}

// ===========================================================================
// Affine3
// ===========================================================================
struct Affine3
{
    Vector3 Columns[3] = {Vector3{1, 0, 0}, Vector3{0, 1, 0}, Vector3{0, 0, 1}}; // linear block L
    Vector3 Translation = {0, 0, 0};

    [[nodiscard]] static constexpr Affine3 Identity() noexcept
    {
        return Affine3{};
    }
    [[nodiscard]] friend constexpr bool operator==(const Affine3&, const Affine3&) = default;
};

[[nodiscard]] constexpr Vector3 TransformPoint(const Affine3& a, Vector3 p) noexcept
{
    return a.Columns[0] * p.X + a.Columns[1] * p.Y + a.Columns[2] * p.Z + a.Translation;
}
[[nodiscard]] constexpr Vector3 TransformVector(const Affine3& a, Vector3 v) noexcept
{
    return a.Columns[0] * v.X + a.Columns[1] * v.Y + a.Columns[2] * v.Z;
}
[[nodiscard]] constexpr Affine3 Compose(const Affine3& a, const Affine3& b) noexcept
{
    const Vector3 lb0 = a.Columns[0] * b.Columns[0].X + a.Columns[1] * b.Columns[0].Y + a.Columns[2] * b.Columns[0].Z;
    const Vector3 lb1 = a.Columns[0] * b.Columns[1].X + a.Columns[1] * b.Columns[1].Y + a.Columns[2] * b.Columns[1].Z;
    const Vector3 lb2 = a.Columns[0] * b.Columns[2].X + a.Columns[1] * b.Columns[2].Y + a.Columns[2] * b.Columns[2].Z;
    const Vector3 t = a.Columns[0] * b.Translation.X + a.Columns[1] * b.Translation.Y + a.Columns[2] * b.Translation.Z +
                      a.Translation;
    return Affine3{{lb0, lb1, lb2}, t};
}
[[nodiscard]] constexpr bool IsFinite(const Affine3& a) noexcept
{
    return IsFinite(a.Columns[0]) && IsFinite(a.Columns[1]) && IsFinite(a.Columns[2]) && IsFinite(a.Translation);
}

// Linear block of an Affine3 as a Matrix3 (helper for conversions/queries).
[[nodiscard]] constexpr Matrix3 LinearBlock(const Affine3& a) noexcept
{
    return Matrix3{{a.Columns[0], a.Columns[1], a.Columns[2]}};
}

// --- Checked affine inverse / normal transform ------------------------------
// Inverse(A) = { inverse(A.L), -inverse(A.L) * A.t }, evaluating the LINEAR
// block's condition separately (singular scale -> IllConditioned/Degenerate).
// Translation computed in double and narrow-checked. In-place supported.
[[nodiscard]] MathStatus TryInverse(const Affine2& a, InversePolicy policy, Affine2& out) noexcept;
[[nodiscard]] MathStatus TryInverse(const Affine3& a, InversePolicy policy, Affine3& out) noexcept;
[[nodiscard]] MathStatus TryInverse(const Affine2& a, Affine2& out) noexcept;
[[nodiscard]] MathStatus TryInverse(const Affine3& a, Affine3& out) noexcept;

// NormalMatrix(A) = transpose(inverse(A.L)); singular L fails.
[[nodiscard]] MathStatus TryNormalMatrix(const Affine3& a, InversePolicy policy, Matrix3& out) noexcept;

// TryTransformNormal applies the normal matrix then robustly normalises. A
// zero-length input normal fails Degenerate; a singular transform fails the
// inverse. Do NOT use TransformVector for normals under non-uniform scale/shear.
[[nodiscard]] MathStatus TryTransformNormal(const Affine3& a, Vector3 normal, Vector3& out) noexcept;

// ===========================================================================
// TransformTRS (authoring)
// ===========================================================================
struct TransformTRS
{
    Vector3 Translation = {0, 0, 0};
    Quaternion Rotation = QuaternionIdentity();
    Vector3 Scale = {1, 1, 1};
};

// Trusted conversion: accepts previously-validated authoring values (unit
// rotation assumed). L = Rotation(q) * Diagonal(scale).
[[nodiscard]] Affine3 ToAffine3(const TransformTRS& trs) noexcept;

// Checked conversion for imported data: validates finite fields and a unit
// rotation (within rotationUnitTol). Output unchanged on failure.
[[nodiscard]] MathStatus TryToAffine3(const TransformTRS& trs, float32 rotationUnitTol, Affine3& out) noexcept;

// ===========================================================================
// Matrix4 bridges
// ===========================================================================
// Expand an Affine3 to a Matrix4 with the implicit last row (0,0,0,1).
[[nodiscard]] constexpr Matrix4 ToMatrix4(const Affine3& a) noexcept
{
    return Matrix4{{
        Vector4{a.Columns[0].X, a.Columns[0].Y, a.Columns[0].Z, 0.0f},
        Vector4{a.Columns[1].X, a.Columns[1].Y, a.Columns[1].Z, 0.0f},
        Vector4{a.Columns[2].X, a.Columns[2].Y, a.Columns[2].Z, 0.0f},
        Vector4{a.Translation.X, a.Translation.Y, a.Translation.Z, 1.0f},
    }};
}

// Transform a homogeneous point: produces a Vector4; it never silently divides.
[[nodiscard]] constexpr Vector4 TransformHomogeneous(const Matrix4& m, Vector4 p) noexcept
{
    return m * p;
}

// TryPerspectiveDivide: divide xyz by w. Rejects non-finite input, w == 0, or
// abs(w) <= minAbsW (caller-supplied finite non-negative threshold), and a
// non-representable result (OutOfRange). Negative w is permitted (visibility is
// a separate operation). Output unchanged on failure.
[[nodiscard]] MathStatus TryPerspectiveDivide(Vector4 clip, float32 minAbsW, Vector3& out) noexcept;
} // namespace ludus::foundation::math
