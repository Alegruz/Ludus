// Affine inverse / normal transform, TRS conversion, perspective divide
// (design §7). Precise FP flags; <cmath>/<limits> permitted (ADR 0003/0010).

#include <ludus/foundation/math/transform.hpp>

#include <cmath>

namespace ludus::foundation::math
{
namespace
{
// 2x2 checked inverse of an Affine2's linear block, reusing the matrix policy.
// Returns the status; on success fills the 2x2 inverse (column-major doubles).
[[nodiscard]] MathStatus Invert2x2(const Affine2& a, InversePolicy policy, float64 inv[2][2]) noexcept
{
    const float64 m00 = a.Columns[0].X, m10 = a.Columns[0].Y;
    const float64 m01 = a.Columns[1].X, m11 = a.Columns[1].Y;
    if (!std::isfinite(m00) || !std::isfinite(m10) || !std::isfinite(m01) || !std::isfinite(m11))
    {
        return MathStatus::NonFiniteInput;
    }
    const float64 det = m00 * m11 - m01 * m10;
    if (det == 0.0 || !std::isfinite(det))
    {
        return MathStatus::Degenerate;
    }
    const float64 invDet = 1.0 / det;
    // inverse (column-major: inv[row][col])
    inv[0][0] = m11 * invDet;
    inv[1][0] = -m10 * invDet;
    inv[0][1] = -m01 * invDet;
    inv[1][1] = m00 * invDet;
    // rcond via inf norms
    const float64 na = std::fmax(std::fabs(m00) + std::fabs(m01), std::fabs(m10) + std::fabs(m11));
    const float64 ni =
        std::fmax(std::fabs(inv[0][0]) + std::fabs(inv[0][1]), std::fabs(inv[1][0]) + std::fabs(inv[1][1]));
    const float64 rcond = (na > 0.0 && ni > 0.0) ? 1.0 / (na * ni) : 0.0;
    if (!(rcond >= static_cast<float64>(policy.MinReciprocalCondition)))
    {
        return MathStatus::IllConditioned;
    }
    for (int r = 0; r < 2; ++r)
    {
        for (int c = 0; c < 2; ++c)
        {
            if (!std::isfinite(inv[r][c]))
            {
                return MathStatus::OutOfRange;
            }
        }
    }
    return MathStatus::Success;
}
} // namespace

MathStatus TryInverse(const Affine2& a, InversePolicy policy, Affine2& out) noexcept
{
    float64 inv[2][2];
    const MathStatus status = Invert2x2(a, policy, inv);
    if (status != MathStatus::Success)
    {
        return status;
    }
    // t' = -inv(L) * t, in double.
    const float64 tx = a.Translation.X, ty = a.Translation.Y;
    const float64 ntx = -(inv[0][0] * tx + inv[0][1] * ty);
    const float64 nty = -(inv[1][0] * tx + inv[1][1] * ty);
    const Vector2 c0{static_cast<float32>(inv[0][0]), static_cast<float32>(inv[1][0])};
    const Vector2 c1{static_cast<float32>(inv[0][1]), static_cast<float32>(inv[1][1])};
    const Vector2 t{static_cast<float32>(ntx), static_cast<float32>(nty)};
    if (!IsFinite(c0) || !IsFinite(c1) || !IsFinite(t))
    {
        return MathStatus::OutOfRange;
    }
    out = Affine2{{c0, c1}, t};
    return MathStatus::Success;
}

MathStatus TryInverse(const Affine3& a, InversePolicy policy, Affine3& out) noexcept
{
    Matrix3 invL{};
    const MathStatus status = TryInverse(LinearBlock(a), policy, invL);
    if (status != MathStatus::Success)
    {
        return status;
    }
    // t' = -invL * t, in double.
    const float64 tx = a.Translation.X, ty = a.Translation.Y, tz = a.Translation.Z;
    const float64 ntx = -((float64)invL.At(0, 0) * tx + (float64)invL.At(0, 1) * ty + (float64)invL.At(0, 2) * tz);
    const float64 nty = -((float64)invL.At(1, 0) * tx + (float64)invL.At(1, 1) * ty + (float64)invL.At(1, 2) * tz);
    const float64 ntz = -((float64)invL.At(2, 0) * tx + (float64)invL.At(2, 1) * ty + (float64)invL.At(2, 2) * tz);
    const Vector3 t{static_cast<float32>(ntx), static_cast<float32>(nty), static_cast<float32>(ntz)};
    if (!IsFinite(t))
    {
        return MathStatus::OutOfRange;
    }
    out = Affine3{{invL.Columns[0], invL.Columns[1], invL.Columns[2]}, t};
    return MathStatus::Success;
}

MathStatus TryInverse(const Affine2& a, Affine2& out) noexcept
{
    return TryInverse(a, InversePolicy{}, out);
}
MathStatus TryInverse(const Affine3& a, Affine3& out) noexcept
{
    return TryInverse(a, InversePolicy{}, out);
}

MathStatus TryNormalMatrix(const Affine3& a, InversePolicy policy, Matrix3& out) noexcept
{
    Matrix3 invL{};
    const MathStatus status = TryInverse(LinearBlock(a), policy, invL);
    if (status != MathStatus::Success)
    {
        return status;
    }
    out = Transpose(invL);
    return MathStatus::Success;
}

MathStatus TryTransformNormal(const Affine3& a, Vector3 normal, Vector3& out) noexcept
{
    if (!IsFinite(a) || !IsFinite(normal))
    {
        return MathStatus::NonFiniteInput;
    }
    Matrix3 normalMatrix{};
    const MathStatus status = TryNormalMatrix(a, InversePolicy{}, normalMatrix);
    if (status != MathStatus::Success)
    {
        return status;
    }
    const Vector3 transformed = normalMatrix * normal;
    Vector3 normalized{};
    const MathStatus normStatus = TryNormalize(transformed, normalized);
    if (normStatus != MathStatus::Success)
    {
        return normStatus; // zero/degenerate normal -> Degenerate
    }
    out = normalized;
    return MathStatus::Success;
}

Affine3 ToAffine3(const TransformTRS& trs) noexcept
{
    LUDUS_ASSERT(IsFinite(trs.Rotation) && IsFinite(trs.Scale) && IsFinite(trs.Translation));
    const Matrix3 r = ToMatrix3(trs.Rotation);
    // L = R * Diagonal(scale): scale column c of R by scale[c].
    const Vector3 c0 = r.Columns[0] * trs.Scale.X;
    const Vector3 c1 = r.Columns[1] * trs.Scale.Y;
    const Vector3 c2 = r.Columns[2] * trs.Scale.Z;
    return Affine3{{c0, c1, c2}, trs.Translation};
}

MathStatus TryToAffine3(const TransformTRS& trs, float32 rotationUnitTol, Affine3& out) noexcept
{
    if (!IsFinite(trs.Rotation) || !IsFinite(trs.Scale) || !IsFinite(trs.Translation) ||
        !std::isfinite(rotationUnitTol))
    {
        return MathStatus::NonFiniteInput;
    }
    if (rotationUnitTol < 0.0f)
    {
        return MathStatus::InvalidArgument;
    }
    // Validate unit rotation.
    const float64 qlen2 = (float64)trs.Rotation.X * trs.Rotation.X + (float64)trs.Rotation.Y * trs.Rotation.Y +
                          (float64)trs.Rotation.Z * trs.Rotation.Z + (float64)trs.Rotation.W * trs.Rotation.W;
    if (std::fabs(std::sqrt(qlen2) - 1.0) > static_cast<float64>(rotationUnitTol))
    {
        return MathStatus::Degenerate;
    }
    out = ToAffine3(trs);
    return MathStatus::Success;
}

MathStatus TryPerspectiveDivide(Vector4 clip, float32 minAbsW, Vector3& out) noexcept
{
    if (!IsFinite(clip) || !std::isfinite(minAbsW))
    {
        return MathStatus::NonFiniteInput;
    }
    if (minAbsW < 0.0f)
    {
        return MathStatus::InvalidArgument;
    }
    const float32 w = clip.W;
    if (w == 0.0f || std::fabs(w) <= minAbsW)
    {
        return MathStatus::InvalidArgument;
    }
    const float64 iw = 1.0 / static_cast<float64>(w);
    const float32 x = static_cast<float32>(clip.X * iw);
    const float32 y = static_cast<float32>(clip.Y * iw);
    const float32 z = static_cast<float32>(clip.Z * iw);
    if (!IsFinite(x) || !IsFinite(y) || !IsFinite(z))
    {
        return MathStatus::OutOfRange;
    }
    out = Vector3{x, y, z};
    return MathStatus::Success;
}
} // namespace ludus::foundation::math
