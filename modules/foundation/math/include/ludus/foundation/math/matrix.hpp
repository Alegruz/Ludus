#pragma once

// Matrix3 / Matrix4: column-major storage (an array of columns), column-vector
// action pOut = M * pIn, composition A*B applies B then A (ADR 0010 §3). Default
// construction is the ZERO matrix; use Identity() for identity. Element access
// is At(row, column). The column arrays are real arrays indexed [0..N).
//
// Cheap algebra (multiply, action, transpose) is inline. The checked inverse
// (fixed-stack scaled-pivot Gauss-Jordan with condition/residual policy, design
// §7) and the quaternion<->matrix conversions live in matrix.cpp.

#include <ludus/foundation/base/core.h>

#include <ludus/foundation/math/quaternion.hpp>
#include <ludus/foundation/math/status.hpp>
#include <ludus/foundation/math/vector.hpp>

namespace ludus::foundation::math
{
using core::float32;
using core::usize;

// Policy for checked matrix inversion. Dimensionless thresholds (design §4):
// minReciprocalCondition rejects ill-conditioned matrices; maxResidual rejects
// a narrowed inverse whose two-sided residual is too large. These are rejection
// policies, not forward-error guarantees.
struct InversePolicy
{
    float32 MinReciprocalCondition = 1e-6f;
    float32 MaxResidual = 1e-5f;
};

// ===========================================================================
// Matrix3 — three Vector3 columns.
// ===========================================================================
struct Matrix3
{
    Vector3 Columns[3] = {};

    [[nodiscard]] static constexpr Matrix3 Zero() noexcept
    {
        return Matrix3{};
    }
    [[nodiscard]] static constexpr Matrix3 Identity() noexcept
    {
        return Matrix3{{Vector3{1, 0, 0}, Vector3{0, 1, 0}, Vector3{0, 0, 1}}};
    }

    // At(row, column): element in [0,3). Direct caller indexing has ordinary C++
    // bounds preconditions (asserted in development).
    [[nodiscard]] constexpr float32 At(usize row, usize column) const noexcept
    {
        LUDUS_ASSERT(row < 3 && column < 3);
        const Vector3& c = Columns[column];
        return row == 0 ? c.X : (row == 1 ? c.Y : c.Z);
    }

    [[nodiscard]] friend constexpr bool operator==(const Matrix3&, const Matrix3&) = default;
};

[[nodiscard]] constexpr Vector3 operator*(const Matrix3& m, Vector3 v) noexcept
{
    return m.Columns[0] * v.X + m.Columns[1] * v.Y + m.Columns[2] * v.Z;
}
[[nodiscard]] constexpr Matrix3 operator*(const Matrix3& a, const Matrix3& b) noexcept
{
    return Matrix3{{a * b.Columns[0], a * b.Columns[1], a * b.Columns[2]}};
}
[[nodiscard]] constexpr Matrix3 Transpose(const Matrix3& m) noexcept
{
    return Matrix3{{
        Vector3{m.Columns[0].X, m.Columns[1].X, m.Columns[2].X},
        Vector3{m.Columns[0].Y, m.Columns[1].Y, m.Columns[2].Y},
        Vector3{m.Columns[0].Z, m.Columns[1].Z, m.Columns[2].Z},
    }};
}
[[nodiscard]] constexpr bool IsFinite(const Matrix3& m) noexcept
{
    return IsFinite(m.Columns[0]) && IsFinite(m.Columns[1]) && IsFinite(m.Columns[2]);
}

// ===========================================================================
// Matrix4 — four Vector4 columns.
// ===========================================================================
struct Matrix4
{
    Vector4 Columns[4] = {};

    [[nodiscard]] static constexpr Matrix4 Zero() noexcept
    {
        return Matrix4{};
    }
    [[nodiscard]] static constexpr Matrix4 Identity() noexcept
    {
        return Matrix4{{Vector4{1, 0, 0, 0}, Vector4{0, 1, 0, 0}, Vector4{0, 0, 1, 0}, Vector4{0, 0, 0, 1}}};
    }

    [[nodiscard]] constexpr float32 At(usize row, usize column) const noexcept
    {
        LUDUS_ASSERT(row < 4 && column < 4);
        const Vector4& c = Columns[column];
        return row == 0 ? c.X : (row == 1 ? c.Y : (row == 2 ? c.Z : c.W));
    }

    [[nodiscard]] friend constexpr bool operator==(const Matrix4&, const Matrix4&) = default;
};

[[nodiscard]] constexpr Vector4 operator*(const Matrix4& m, Vector4 v) noexcept
{
    return m.Columns[0] * v.X + m.Columns[1] * v.Y + m.Columns[2] * v.Z + m.Columns[3] * v.W;
}
[[nodiscard]] constexpr Matrix4 operator*(const Matrix4& a, const Matrix4& b) noexcept
{
    return Matrix4{{a * b.Columns[0], a * b.Columns[1], a * b.Columns[2], a * b.Columns[3]}};
}
[[nodiscard]] constexpr Matrix4 Transpose(const Matrix4& m) noexcept
{
    return Matrix4{{
        Vector4{m.Columns[0].X, m.Columns[1].X, m.Columns[2].X, m.Columns[3].X},
        Vector4{m.Columns[0].Y, m.Columns[1].Y, m.Columns[2].Y, m.Columns[3].Y},
        Vector4{m.Columns[0].Z, m.Columns[1].Z, m.Columns[2].Z, m.Columns[3].Z},
        Vector4{m.Columns[0].W, m.Columns[1].W, m.Columns[2].W, m.Columns[3].W},
    }};
}
[[nodiscard]] constexpr bool IsFinite(const Matrix4& m) noexcept
{
    return IsFinite(m.Columns[0]) && IsFinite(m.Columns[1]) && IsFinite(m.Columns[2]) && IsFinite(m.Columns[3]);
}

// --- Rotation <-> matrix ----------------------------------------------------
// Matrix3 from a UNIT quaternion (trusted unit input). Matches Rotate(q,v).
[[nodiscard]] Matrix3 ToMatrix3(Quaternion q) noexcept;

// TryToRotation: convert an approximately-orthonormal, positive-determinant
// Matrix3 to a unit quaternion using the largest-component branch (design §6).
// Rejects scale, shear, reflection and zero bases (Degenerate), using the
// orthogonality/length/determinant tolerance in `policy`. The result's rotation
// action is validated before success. Output unchanged on failure.
struct RotationExtractionPolicy
{
    float32 OrthoTolerance = 1e-5f; // dimensionless length/orthogonality/det slack
};
[[nodiscard]] MathStatus TryToRotation(const Matrix3& basis, RotationExtractionPolicy policy, Quaternion& out) noexcept;
[[nodiscard]] MathStatus TryToRotation(const Matrix3& basis, Quaternion& out) noexcept;

// --- Checked inverse --------------------------------------------------------
// Fixed-stack scaled-pivot Gauss-Jordan with condition/residual policy. Returns
// output unchanged on failure (IllConditioned / Degenerate / OutOfRange /
// NonFiniteInput). In-place inversion is supported.
[[nodiscard]] MathStatus TryInverse(const Matrix3& m, InversePolicy policy, Matrix3& out) noexcept;
[[nodiscard]] MathStatus TryInverse(const Matrix4& m, InversePolicy policy, Matrix4& out) noexcept;
[[nodiscard]] MathStatus TryInverse(const Matrix3& m, Matrix3& out) noexcept;
[[nodiscard]] MathStatus TryInverse(const Matrix4& m, Matrix4& out) noexcept;
} // namespace ludus::foundation::math
