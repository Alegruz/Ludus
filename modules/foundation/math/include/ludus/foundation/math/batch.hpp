#pragma once

// Scalar batch operations (design §12). Simple caller-owned span loops with full
// prevalidation, output preflight and all-or-nothing (transactional) writes.
// This is the stable public layout a future SIMD kernel can optimize behind
// without changing the API; v1 ships only the scalar baseline.
//
// Overlap rules:
//  - TryTransformPoints: exact in-place (input and output are the SAME buffer)
//    is supported; any PARTIAL overlap is rejected (InvalidArgument).
//  - TryClassifySpheres: the sphere input and relation output must be DISJOINT.
// Overlap is detected with a validated address-range helper (overflow-checked),
// not by comparing unrelated typed pointers.

#include <ludus/foundation/base/core.h>

#include <ludus/foundation/math/geometry.hpp>
#include <ludus/foundation/math/status.hpp>
#include <ludus/foundation/math/transform.hpp>
#include <ludus/foundation/math/vector.hpp>

#include <span>

namespace ludus::foundation::math
{
using core::float32;

// Transform a batch of points by an Affine3. Spans must match in length
// (SizeMismatch otherwise). Validates the affine and every input point before
// writing anything; if any transformed point would be non-finite, the whole
// batch is rejected (OutOfRange) with outputs unchanged. Empty matching spans
// succeed. Exact in-place supported; partial overlap rejected (InvalidArgument).
[[nodiscard]] MathStatus
TryTransformPoints(const Affine3& transform, std::span<const Vector3> input, std::span<Vector3> output) noexcept;

// Classify a batch of spheres against a frustum with a caller margin. Spans must
// match in length. Validates the margin and every sphere before writing; input
// and output must be disjoint. Empty matching spans succeed.
[[nodiscard]] MathStatus TryClassifySpheres(const Frustum& frustum,
                                            std::span<const Sphere> input,
                                            float32 margin,
                                            std::span<FrustumRelation> output) noexcept;
} // namespace ludus::foundation::math
