#pragma once

#include <ludus/foundation/base/types.h>

#include <ludus/foundation/math/matrix.hpp>
#include <ludus/foundation/math/vector.hpp>
#include <ludus/graphics/renderer/renderer.hpp>
#include <ludus/graphics/rhi/graph.h>

namespace ludus::graphics::renderer
{
/// How authored logical dimensions map into an output rectangle.
enum class ScreenFit : ludus::foundation::uint8
{
    /// Preserve aspect, leaving centered letterbox bars.
    Fit,
    /// Preserve aspect, cropping centered excess content.
    Fill,
    /// Scale each axis independently.
    Stretch,
    /// Largest integral Fit scale; explicitly fall back to fractional Fit below one.
    IntegerFit,
};
/// Logical authored dimensions; no implicit DPI multiplier.
struct VirtualScreen final
{
    /// Positive finite horizontal logical extent.
    ludus::foundation::float32 Width = 0;
    /// Positive finite vertical logical extent.
    ludus::foundation::float32 Height = 0;
    /// Aspect and scaling policy.
    ScreenFit Fit = ScreenFit::Fit;
};
/// Copied mapping shared by draw preparation and pointer conversion; no GPU ownership.
struct ViewMapping final
{
    /// Actual drawable width against which the mapping was resolved.
    ludus::foundation::uint32 DrawableWidth = 0;
    /// Actual drawable height against which the mapping was resolved.
    ludus::foundation::uint32 DrawableHeight = 0;
    /// Physical region owned by this view, clipped to the actual drawable.
    rhi::RasterRectangle Region{};
    /// Rounded drawing rectangle; Fill uses Region and clips through ClipTransform.
    rhi::RasterRectangle Viewport{};
    /// Physical pixel-edge origin of logical (0,0), including crop/letterbox offsets.
    ludus::foundation::math::Vector2 Origin{};
    /// Physical pixels per logical unit, derived from the rounded content rectangle.
    ludus::foundation::math::Vector2 Scale{};
    /// Validated authored dimensions.
    VirtualScreen Screen{};
    /// Apply to canonical clip coordinates before rasterization; Fill expands for cropping.
    ludus::foundation::math::Matrix4 ClipTransform = ludus::foundation::math::Matrix4::Identity();
    /// True only when IntegerFit could not fit a whole-number scale.
    bool FractionalFallback = false;
};
/// Resolve against actual physical dimensions. Zero drawable/region returns NotReady.
/// Region must fit the drawable; dimensions above 2^24 or malformed inputs reject.
/// Ready publishes a complete mapping; every failure preserves output. Allocation-free.
[[nodiscard]] rhi::RasterStatus ResolveViewMapping(VirtualScreen screen,
                                                   rhi::RasterRectangle region,
                                                   ludus::foundation::uint32 drawableWidth,
                                                   ludus::foundation::uint32 drawableHeight,
                                                   ViewMapping& output) noexcept;
/// Partition a physical rectangle using shared integer edges, without cracks or overlap.
/// Zero-area cells return NotReady; invalid counts/indices and overflowing edges reject.
/// Every failure preserves output. Cell coordinates and counts are zero-based/counts.
[[nodiscard]] rhi::RasterStatus SplitViewRectangle(rhi::RasterRectangle parent,
                                                   ludus::foundation::uint32 columns,
                                                   ludus::foundation::uint32 rows,
                                                   ludus::foundation::uint32 column,
                                                   ludus::foundation::uint32 row,
                                                   rhi::RasterRectangle& output) noexcept;
/// Invert the same drawing mapping from top-left physical pixel-edge coordinates.
/// Reject outside the region or in letterbox bars unless clamp is true; clamping may
/// return a logical boundary. Invalid mapping/nonfinite input preserves output.
[[nodiscard]] rhi::RasterStatus MapPhysicalToVirtual(const ViewMapping& mapping,
                                                     ludus::foundation::math::Vector2 pixel,
                                                     ludus::foundation::math::Vector2& output,
                                                     bool clamp = false) noexcept;
/// Convert window logical input using an explicit positive per-axis physical DPI scale,
/// then invert the drawing mapping. Applies DPI exactly once; failure preserves output.
[[nodiscard]] rhi::RasterStatus MapWindowToVirtual(const ViewMapping& mapping,
                                                   ludus::foundation::math::Vector2 point,
                                                   ludus::foundation::math::Vector2 pixelsPerWindowUnit,
                                                   ludus::foundation::math::Vector2& output,
                                                   bool clamp = false) noexcept;
/// Map a finite logical point to physical pixel-edge coordinates; points may lie outside
/// content for caller clipping. Invalid mapping or overflow preserves output.
[[nodiscard]] rhi::RasterStatus MapVirtualToPhysical(const ViewMapping& mapping,
                                                     ludus::foundation::math::Vector2 point,
                                                     ludus::foundation::math::Vector2& output) noexcept;
/// Explicit right-handed, Y-up, -Z-forward reverse-Z lens profile.
enum class ProjectionKind : ludus::foundation::uint8
{
    /// Centered orthographic projection.
    Orthographic,
    /// Perspective with a finite far plane.
    Perspective,
    /// Perspective with an absent far plane; culling uses five planes.
    PerspectiveInfinite,
};
/// Ordinary lens/pose values; renderer does not depend on gameplay camera ownership.
struct ProjectionDescription final
{
    /// Projection factory to use.
    ProjectionKind Kind = ProjectionKind::Perspective;
    /// Finite world-to-view matrix supplied by a camera/extraction adapter.
    ludus::foundation::math::Matrix4 WorldToView = ludus::foundation::math::Matrix4::Identity();
    /// Vertical angle in radians, in (0,Pi), for perspective lenses.
    ludus::foundation::float32 VerticalFovRadians = 1;
    /// Positive vertical world extent for Orthographic.
    ludus::foundation::float32 OrthographicHeight = 2;
    /// World units; positive for perspective, nonnegative for orthographic.
    ludus::foundation::float32 Near = 0.1F;
    /// World units, greater than Near; ignored for PerspectiveInfinite.
    ludus::foundation::float32 Far = 1000;
};
/// Where the prepared geometry will be rasterized; crop must be applied exactly once.
enum class ProjectionSpace : ludus::foundation::uint8
{
    /// Uncropped logical image in an offscreen target; DrawPresentation applies output crop.
    LogicalScreen,
    /// Direct output-region drawing; apply mapping crop to geometry and overlays here.
    OutputRegion,
};
/// Build a reverse-Z prepared-view description using the logical aspect.
/// LogicalScreen is the default for offscreen composition; OutputRegion applies Fill crop
/// when drawing directly into mapping.Viewport. Do not compose a cropped OutputRegion view.
/// Finite orthographic/perspective and infinite perspective reuse FoundationMath factories.
/// Invalid/nonrepresentable inputs preserve output; Reference/CullMargin use their defaults.
[[nodiscard]] rhi::RasterStatus BuildViewDescription(const ProjectionDescription& projection,
                                                     const ViewMapping& mapping,
                                                     ViewDescription& output,
                                                     ProjectionSpace space = ProjectionSpace::LogicalScreen) noexcept;
} // namespace ludus::graphics::renderer
