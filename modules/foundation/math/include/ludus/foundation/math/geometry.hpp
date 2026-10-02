#pragma once

// Primitive geometry values and result records (design §9). Plain aggregates;
// query algorithms live in queries.cpp. There is NO BVH/kD-tree, scene traversal,
// collision world or exact-topology claim in v1.
//
// Conventions:
//  - Plane: dot(normal, p) + D = 0; positive distance on the normal side. A plane
//    is "trusted" (unit normal) only after TryMakePlane; raw-edited fields are
//    validated by the query APIs.
//  - Aabb: a box with any min > max is the canonical EMPTY box (use
//    EmptyAabb*()); it is NOT a zero-size box at the origin. Any non-finite field
//    is INVALID, not empty.
//  - Ray3: p(t) = origin + t*direction; direction need not be unit, so t is a
//    distance only for a unit direction. Individual zero direction components are
//    valid; a wholly zero or non-finite direction is invalid.
//  - RayInterval: float64 [minT, maxT]; maxT may be +infinity; requires finite
//    minT >= 0 and maxT >= minT.

#include <ludus/foundation/base/core.h>

#include <ludus/foundation/math/vector.hpp>

namespace ludus::foundation::math
{
using core::float32;
using core::float64;
using core::uint8;

struct Plane
{
    Vector3 Normal = {0, 0, 1};
    float32 D = 0.0f;
};

struct Aabb2
{
    Vector2 Min = {0, 0};
    Vector2 Max = {0, 0};
};

struct Aabb3
{
    Vector3 Min = {0, 0, 0};
    Vector3 Max = {0, 0, 0};
};

struct Sphere
{
    Vector3 Center = {0, 0, 0};
    float32 Radius = 0.0f;
};

struct Ray3
{
    Vector3 Origin = {0, 0, 0};
    Vector3 Direction = {0, 0, -1};
};

struct RayInterval
{
    float64 MinT = 0.0;
    float64 MaxT = 0.0;
};

// --- Query result classification --------------------------------------------
enum class HitResult : uint8
{
    Miss = 0,
    Hit = 1,
};

// Ray/primitive hit: on Hit, EntryT/ExitT are the parametric interval (float64).
// On Miss the fields are neutral zeros and must not be interpreted.
struct RayHit
{
    HitResult Result = HitResult::Miss;
    float64 EntryT = 0.0;
    float64 ExitT = 0.0;
};

// Closest-points-between-segments result: parameters S,T in [0,1], the two
// closest points, and the squared distance. All meaningful only on Success.
struct SegmentClosest
{
    float64 S = 0.0;
    float64 T = 0.0;
    Vector3 PointOnFirst = {0, 0, 0};
    Vector3 PointOnSecond = {0, 0, 0};
    float64 SquaredDistance = 0.0;
};

// --- Frustum ----------------------------------------------------------------
enum class FrustumMode : uint8
{
    FinitePerspectiveOrOrthographic = 0,
    InfiniteReverseZPerspective = 1,
};

// Six inward-facing normalized planes plus an active mask. For an infinite
// reverse-Z perspective frustum the far plane (depthLower) is inactive. Plane
// order: Left, Right, Bottom, Top, Near, Far (physical labels under reverse-Z).
struct Frustum
{
    Plane Planes[6] = {};
    uint8 ActiveMask = 0x3f; // bit i set => Planes[i] is active
    FrustumMode Mode = FrustumMode::FinitePerspectiveOrOrthographic;
};

enum class FrustumRelation : uint8
{
    Outside = 0,
    Intersecting = 1,
    Inside = 2,
};

// --- AABB factories / predicates --------------------------------------------
[[nodiscard]] constexpr Aabb2 EmptyAabb2() noexcept
{
    // min = +largest finite, max = -largest finite (a finite sentinel).
    constexpr float32 kBig = 3.4028234663852886e+38f;
    return Aabb2{Vector2{kBig, kBig}, Vector2{-kBig, -kBig}};
}
[[nodiscard]] constexpr Aabb3 EmptyAabb3() noexcept
{
    constexpr float32 kBig = 3.4028234663852886e+38f;
    return Aabb3{Vector3{kBig, kBig, kBig}, Vector3{-kBig, -kBig, -kBig}};
}

[[nodiscard]] constexpr bool IsEmpty(const Aabb2& b) noexcept
{
    return b.Min.X > b.Max.X || b.Min.Y > b.Max.Y;
}
[[nodiscard]] constexpr bool IsEmpty(const Aabb3& b) noexcept
{
    return b.Min.X > b.Max.X || b.Min.Y > b.Max.Y || b.Min.Z > b.Max.Z;
}
[[nodiscard]] constexpr bool IsFinite(const Aabb2& b) noexcept
{
    return IsFinite(b.Min) && IsFinite(b.Max);
}
[[nodiscard]] constexpr bool IsFinite(const Aabb3& b) noexcept
{
    return IsFinite(b.Min) && IsFinite(b.Max);
}
} // namespace ludus::foundation::math
