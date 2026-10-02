#pragma once

// Camera / projection helpers (design §8). Canonical clip volume: NDC X/Y in
// [-1,1], depth Z in [0,1], Y up (matches WebGPU). All projections are
// REVERSE-Z: a near plane maps to depth 1 and far to depth 0, which pairs with a
// future renderer clearing depth to 0 and comparing Greater/GreaterEqual. Math
// supplies the matrices and this contract only; it adds no depth-enabled RHI
// feature and alters no existing smoke pipeline.
//
// Every factory validates its inputs and returns output unchanged on failure.
// Coefficients are computed in double then checked for representability.

#include <ludus/foundation/base/core.h>

#include <ludus/foundation/math/matrix.hpp>
#include <ludus/foundation/math/status.hpp>
#include <ludus/foundation/math/vector.hpp>

namespace ludus::foundation::math
{
using core::float32;

// TryLookAt: right-handed view matrix. backward z = normalize(eye - target),
// right x = normalize(cross(upHint, z)), y = cross(z, x). Coincident eye/target
// or a parallel/zero upHint fails Degenerate. No hidden up-axis repair; for roll
// continuity the caller supplies its previous orientation instead.
[[nodiscard]] MathStatus TryLookAt(Vector3 eye, Vector3 target, Vector3 upHint, Matrix4& outView) noexcept;

// Finite reverse-Z perspective. aspect > 0, near > 0, far > near, verticalFov in
// (0, Pi). Depth 1 at z=-near, 0 at z=-far. Invalid/degenerate inputs fail;
// coefficients that are not representable fail OutOfRange.
[[nodiscard]] MathStatus TryPerspectiveReverseZ(float32 verticalFovRadians,
                                                float32 aspect,
                                                float32 nearPlane,
                                                float32 farPlane,
                                                Matrix4& out) noexcept;

// Infinite reverse-Z perspective (far at infinity). Named factory, NOT a
// far=infinity sentinel. aspect > 0, near > 0, verticalFov in (0, Pi). A finite
// camera point at z<0 has depth near/(-z).
[[nodiscard]] MathStatus
TryPerspectiveReverseZInfinite(float32 verticalFovRadians, float32 aspect, float32 nearPlane, Matrix4& out) noexcept;

// Finite reverse-Z orthographic. left<right, bottom<top, 0<=near<far. All inputs
// finite and correctly ordered; an interval too narrow to produce representable
// coefficients fails OutOfRange. Zero extents are not clamped.
[[nodiscard]] MathStatus TryOrthographicReverseZ(float32 left,
                                                 float32 right,
                                                 float32 bottom,
                                                 float32 top,
                                                 float32 nearPlane,
                                                 float32 farPlane,
                                                 Matrix4& out) noexcept;

// Framebuffer rectangle for NDC<->pixel conversion. Top-left origin, Y down,
// physical pixels. Extents must be positive and finite.
struct Viewport
{
    float32 X = 0.0f;
    float32 Y = 0.0f;
    float32 Width = 0.0f;
    float32 Height = 0.0f;
};

// NDC -> pixel-edge coordinates: px = x + (ndc.x+1)*w/2, py = y + (1-ndc.y)*h/2.
// Pixel centres are at +0.5; this does not snap to centres or apply DPR. NDC
// outside [-1,1] maps outside the rectangle (callers clip). Fails on non-finite
// NDC or a non-positive/non-finite viewport.
[[nodiscard]] MathStatus TryNdcToFramebuffer(Vector3 ndc, Viewport viewport, Vector2& out) noexcept;

// Pixel-edge coordinates -> NDC x,y (inverse of the X/Y mapping above). Depth is
// not inverted here (callers that need z supply it). Same validation.
[[nodiscard]] MathStatus TryFramebufferToNdc(Vector2 pixel, Viewport viewport, Vector2& out) noexcept;
} // namespace ludus::foundation::math
