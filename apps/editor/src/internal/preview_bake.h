#pragma once

#include <ludus/foundation/base/types.h>

#include <QString>

namespace ludus::editor
{
/// Lifecycle of the Lit static-lighting bake for the current scene and shading.
enum class PreviewBakeState : foundation::uint8
{
    Idle,      ///< No bake for this view: not Lit, not started yet, or no lit scene.
    Running,   ///< The CPU bake is evaluating; CancelBake stops it.
    Baked,     ///< Baked lighting is published in the viewport.
    Failed,    ///< The bake or its upload failed; direct lighting is retained.
    Cancelled, ///< The user cancelled; direct lighting is retained and RestartBake can resume.
};
/// Input problems the running or completed bake tolerated, for the user to review.
struct PreviewBakeNotice final
{
    foundation::uint32 SkippedTriangleCount = 0; ///< Zero-area triangles skipped across all objects.
    foundation::uint32 AffectedObjectCount = 0;  ///< Objects with at least one skipped triangle.
    QString AffectedObjects;                     ///< Display names with per-object counts, comma separated.
};
} // namespace ludus::editor
