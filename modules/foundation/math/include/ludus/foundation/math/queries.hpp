#pragma once

// Checked geometric queries (design §9). All compute through float64
// intermediates and distinguish a normal geometric miss (Success + Miss) from
// invalid input (a failing status with output unchanged). None of these claim
// exact topological predicates.

#include <ludus/foundation/base/core.h>

#include <ludus/foundation/math/geometry.hpp>
#include <ludus/foundation/math/matrix.hpp>
#include <ludus/foundation/math/projection.hpp>
#include <ludus/foundation/math/status.hpp>
#include <ludus/foundation/math/transform.hpp>
#include <ludus/foundation/math/vector.hpp>

namespace ludus::foundation::math
{
using core::float32;
using core::float64;

// --- Plane ------------------------------------------------------------------
// TryMakePlane: normalize a finite non-zero `normal`, D = -dot(normal, point),
// narrow-checked. Zero normal -> Degenerate. Output unchanged on failure.
[[nodiscard]] MathStatus TryMakePlane(Vector3 normal, Vector3 point, Plane& out) noexcept;

// Signed distance of p to a plane with a UNIT normal (validated here, since the
// public fields can be edited): dot(normal, p) + D. Non-unit/non-finite plane ->
// a failing status, output unchanged.
[[nodiscard]] MathStatus TryPlaneSignedDistance(const Plane& plane, Vector3 p, float32& out) noexcept;

// Project p onto the plane (p - distance*normal). Reflect a vector across the
// plane normal (v - 2*dot(n,v)*n). Both validate a unit-normal finite plane.
[[nodiscard]] MathStatus TryProjectOntoPlane(const Plane& plane, Vector3 p, Vector3& out) noexcept;
[[nodiscard]] MathStatus TryReflectVector(const Plane& plane, Vector3 v, Vector3& out) noexcept;

// --- AABB -------------------------------------------------------------------
// Merge two boxes (empty merges as identity). Containment of a point / box.
[[nodiscard]] Aabb3 Merge(const Aabb3& a, const Aabb3& b) noexcept;
[[nodiscard]] Aabb2 Merge(const Aabb2& a, const Aabb2& b) noexcept;
[[nodiscard]] bool Contains(const Aabb3& box, Vector3 point) noexcept;
[[nodiscard]] bool Contains(const Aabb2& box, Vector2 point) noexcept;

// Conservative affine transform of a box: the result encloses the affine image
// of all eight corners (four in 2D), including reflection/shear, with outward
// rounding. Empty input stays empty. Non-finite input -> NonFiniteInput; an
// endpoint that would round to infinity -> OutOfRange. Output unchanged on
// failure.
[[nodiscard]] MathStatus TryTransformAabb(const Affine3& a, const Aabb3& box, Aabb3& out) noexcept;
[[nodiscard]] MathStatus TryTransformAabb(const Affine2& a, const Aabb2& box, Aabb2& out) noexcept;

// --- Ray queries ------------------------------------------------------------
// Validate a RayInterval: finite minT >= 0, maxT >= minT (maxT may be +inf).
[[nodiscard]] bool IsValidInterval(const RayInterval& interval) noexcept;

// Ray/AABB slab test. direction must be finite and non-zero (individual zero
// components are valid). Returns Success with Hit/Miss, or a failing status for
// invalid ray/interval/box. On Hit, EntryT/ExitT are clipped to the interval;
// an inside start returns EntryT = interval.MinT (no fabricated surface).
[[nodiscard]] MathStatus TryRayAabb(const Ray3& ray, const Aabb3& box, const RayInterval& interval, RayHit& out) noexcept;

// Ray/Sphere. Cancellation-resistant quadratic; tangent counts as a Hit; a start
// inside returns EntryT = interval.MinT. Negative radius / non-finite -> failing
// status. Returns the intersection of the sphere's closed inside interval with
// the query interval.
[[nodiscard]] MathStatus TryRaySphere(const Ray3& ray, const Sphere& sphere, const RayInterval& interval,
                                      RayHit& out) noexcept;

// --- Closest points between two segments ------------------------------------
// Segment 1 = [p0,p1], segment 2 = [q0,q1]. Handles zero-length segments and
// parallel/degenerate cases; evaluates the interior stationary candidate and all
// four endpoint projections, selecting the minimum squared distance (ties: lower
// S then lower T). Non-finite endpoints -> NonFiniteInput. Not an exact topology
// claim; near-parallel locations may jump while distance stays useful.
[[nodiscard]] MathStatus TryClosestPointsSegments(Vector3 p0, Vector3 p1, Vector3 q0, Vector3 q1,
                                                  SegmentClosest& out) noexcept;

// --- Frustum ----------------------------------------------------------------
// Extract six inward normalized planes from a world-to-clip Matrix4 in the named
// mode. Finite perspective/orthographic keeps all six active; infinite reverse-Z
// perspective marks the far (depthLower) plane inactive (it must have zero xyz
// and a positive constant). Invalid/non-finite matrix or a degenerate required
// plane fails with output unchanged.
[[nodiscard]] MathStatus TryExtractFrustum(const Matrix4& worldToClip, FrustumMode mode, Frustum& out) noexcept;

// Conservative classification with a caller margin >= 0. A sphere is Outside only
// if some active inward distance < -(radius + margin); Inside only if every
// active distance > radius + margin; otherwise Intersecting. The computation adds
// a documented roundoff allowance so culling never produces a false negative
// (never culls an object that is actually inside/intersecting) within the tested
// range. Empty boxes are Outside. Non-finite inputs / negative margin / radius
// fail with output unchanged.
[[nodiscard]] MathStatus TryClassifySphere(const Frustum& frustum, const Sphere& sphere, float32 margin,
                                           FrustumRelation& out) noexcept;
[[nodiscard]] MathStatus TryClassifyAabb(const Frustum& frustum, const Aabb3& box, float32 margin,
                                         FrustumRelation& out) noexcept;
} // namespace ludus::foundation::math
