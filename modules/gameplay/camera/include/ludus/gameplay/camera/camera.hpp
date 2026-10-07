#pragma once

#include <ludus/foundation/base/core.h>

#include <ludus/foundation/math/quaternion.hpp>
#include <ludus/foundation/math/vector.hpp>

namespace ludus::gameplay::camera
{
/// Result of camera evaluation; failures preserve caller outputs.
enum class CameraStatus : foundation::uint8
{
    /// Complete validated result was published.
    Success,
    /// Nonfinite, unordered, nonunit or unsupported input.
    InvalidInput,
    /// A finite calculation exceeded its representable range.
    OutOfRange,
    /// The follow recipe has no resolved target.
    MissingTarget,
};

/// Rigid camera pose: right handed, +Y up, local forward -Z; no scale/shear.
struct CameraPose final
{
    /// Finite world position in world units; remains foundation::float64 until render extraction.
    foundation::math::Vector3d Position{};
    /// Unit local-to-world rotation; squared-length tolerance is 1e-4.
    foundation::math::Quaternion Orientation{};
};

/// Explicit reverse-Z [0,1] projection policy; infinity is a tag, never a sentinel.
enum class CameraProjection : foundation::uint8
{
    /// Perspective with positive near and finite far greater than near.
    PerspectiveFinite,
    /// Perspective with positive near and no far plane.
    PerspectiveInfinite,
    /// Orthographic with nonnegative near and finite far greater than near.
    OrthographicFinite,
};

/// Authored lens values; the renderer supplies physical viewport aspect.
struct CameraLens final
{
    /// Selects the active FOV/span and far-plane policy.
    CameraProjection Projection = CameraProjection::OrthographicFinite;
    /// Total vertical field of view in radians, in (0, pi), for perspective.
    foundation::float32 VerticalFov = 1.0F;
    /// Full vertical visible span in world units, positive for orthographic.
    foundation::float32 VerticalSpan = 14.0F;
    /// Orthographic layout minimum full width; span = max(VerticalSpan, width/aspect).
    foundation::float32 MinimumVisibleWidth = 0.0F;
    /// Near distance in world units, positive for perspective, nonnegative for ortho.
    foundation::float32 NearPlane = 0.0F;
    /// Finite far distance in world units; ignored only for PerspectiveInfinite.
    foundation::float32 FarPlane = 100.0F;
};

/// Value-only publication; owns no entity, query, surface or GPU references.
struct CameraSample final
{
    /// Final rigid world pose.
    CameraPose Pose{};
    /// Validated authored lens; layout is resolved against the drawable viewport.
    CameraLens Lens{};
    /// Nonzero game-owned logical output identity, independent of rig/entity identity.
    foundation::uint64 OutputId = 1;
    /// Authored definition revision used for this sample.
    foundation::uint64 DefinitionRevision = 1;
    /// Previous completed tick used for target interpolation.
    foundation::uint64 PreviousTick = 0;
    /// Current completed tick, greater than or equal to PreviousTick.
    foundation::uint64 CurrentTick = 0;
    /// Interpolation alpha in [0,1]; targets have already used it once.
    foundation::float32 RenderAlpha = 1.0F;
    /// Game-owned presentation sequence; zero is permitted for previews.
    foundation::uint64 PresentationSequence = 0;
    /// Game-owned reset revision for world replacement, cuts or binding changes.
    foundation::uint64 DiscontinuityRevision = 0;
};

/// Check finite pose, unit orientation, active lens fields and sample metadata.
/// No allocation or thread affinity. Inactive lens fields are ignored.
[[nodiscard]] CameraStatus ValidateCameraSample(const CameraSample& sample) noexcept;
} // namespace ludus::gameplay::camera
