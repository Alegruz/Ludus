#pragma once

#include <ludus/foundation/base/core.h>

#include <ludus/gameplay/camera/camera.hpp>

namespace ludus::gameplay::camera
{
/// Initial C0 recipes; time-dependent recipes are introduced in later phases.
enum class CameraRecipe : foundation::uint8
{
    /// Publish the authored rigid pose and lens.
    Fixed,
    /// Follow an already interpolated target on world XY with fixed eye Z.
    OrthographicFollow,
};

/// Immutable definition borrowed only during evaluation; editing is game-owned.
struct CameraRigDefinition final
{
    /// Placement recipe.
    CameraRecipe Recipe = CameraRecipe::Fixed;
    /// Authored pose; follow uses its orientation and eye Z.
    CameraPose Pose{};
    /// Authored lens; follow requires OrthographicFinite.
    CameraLens Lens{};
    /// World XY offset from the resolved target, in world units, for follow.
    foundation::math::Vector2 FollowOffset{};
    /// Nonzero authored revision, copied into the publication and trace.
    foundation::uint64 Revision = 1;
};

/// Game-resolved presentation target; never retained or interpolated again.
struct CameraTargetSample final
{
    /// Finite position in world units, drawn with the same tick pair and alpha.
    foundation::math::Vector3d Position{};
    /// Nonzero stable game binding key; contains no component pointer.
    foundation::uint64 BindingId = 1;
};

/// Presentation timing and identity; C0 integrates no clock or mutable history.
struct CameraUpdateContext final
{
    /// Nonzero logical output identity.
    foundation::uint64 OutputId = 1;
    /// Previous tick used by the game's target extraction.
    foundation::uint64 PreviousTick = 0;
    /// Completed current tick, greater than or equal to PreviousTick.
    foundation::uint64 CurrentTick = 0;
    /// Target extraction alpha in [0,1], recorded without another interpolation.
    foundation::float32 RenderAlpha = 1.0F;
    /// Presentation sequence; zero is allowed for previews.
    foundation::uint64 PresentationSequence = 0;
    /// Explicit game-owned discontinuity revision.
    foundation::uint64 DiscontinuityRevision = 0;
};

/// Optional copied checkpoints; debugging owns its ring/capture storage.
struct CameraTrace final
{
    /// Evaluated recipe.
    CameraRecipe Recipe = CameraRecipe::Fixed;
    /// Zero for fixed, otherwise the resolved binding identity.
    foundation::uint64 BindingId = 0;
    /// Validated desired placement before publication (no damping/constraints in C0).
    CameraPose Desired{};
    /// Complete accepted sample.
    CameraSample Published{};
};

/// Evaluate fixed or immediate XY orthographic follow, without allocations.
/// @param definition Immutable authored recipe and nonzero revision, borrowed for this call.
/// @param context Presentation metadata from the same target extraction.
/// @param target Required only for follow; borrowed for this call, already interpolated.
/// @param output Replaced only on Success, including when trace is omitted.
/// @param trace Optional checkpoint, replaced only on Success; no diagnostic failure path.
/// @return Explicit validation/range/missing-target result; failure preserves both outputs.
/// @note Stateless and thread independent. Follow requires identity rotation (either sign),
/// has no dead zone, look-ahead, bounds or damping yet, and never changes simulation state.
[[nodiscard]] CameraStatus TryEvaluateCamera(const CameraRigDefinition& definition,
                                             const CameraUpdateContext& context,
                                             const CameraTargetSample* target,
                                             CameraSample& output,
                                             CameraTrace* trace = nullptr) noexcept;
} // namespace ludus::gameplay::camera
