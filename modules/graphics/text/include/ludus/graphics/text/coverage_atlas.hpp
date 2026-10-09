#pragma once

#include <ludus/foundation/base/types.h>

#include <ludus/foundation/base/pointer.hpp>
#include <ludus/foundation/math/vector.hpp>
#include <ludus/graphics/renderer/overlays.hpp>
#include <ludus/graphics/rhi/raster.h>

namespace ludus::graphics::text
{
/// Exact glyph-cache key, independent of the CPU font owner's storage addresses.
struct GlyphKey final
{
    /// Caller-assigned nonzero font-content incarnation; change after reload/restart.
    foundation::uint64 FontVersion = 0;
    /// Outline glyph ID produced by shaping, including zero for .notdef.
    foundation::uint32 Glyph = 0;
    /// Physical raster EM height in pixels, 1..256.
    foundation::uint32 PixelHeight = 0;
};
/// Cached coverage placement with transparent one-texel gutters and no mipmaps.
struct AtlasGlyph final
{
    /// Normalized top-left UV edges (left, top, right, bottom), excluding gutters.
    foundation::math::Vector4 Uv;
    /// Raster width in physical pixels; zero for whitespace.
    foundation::uint32 Width = 0;
    /// Raster height in physical pixels.
    foundation::uint32 Height = 0;
    /// Signed pen-relative left bearing in physical pixels.
    foundation::int32 BearingLeft = 0;
    /// Signed distance above baseline to bitmap top in physical pixels.
    foundation::int32 BearingTop = 0;
};
/// A copied shaped placement; Renderer never interprets Unicode or font objects.
struct TextQuad final
{
    /// Baseline-relative physical-pixel rectangle, top-left edges.
    foundation::math::Vector4 Rectangle;
    /// Exact coverage cell from the atlas version prepared for this run.
    foundation::math::Vector4 Uv;
    /// Original UTF-8 byte cluster for diagnostic provenance, not a caret API.
    foundation::uint32 Cluster = 0;
};
/// Bounded append-only CPU R8 atlas and cache. Single owner thread. Atlas coordinates
/// never move until Reset; no implicit eviction or glyph fallback. Capacity errors
/// are explicit and preserve the failed insertion. Publish creates immutable whole
/// texture versions: callers retain earlier versions through prepared/GPU uses.
/// This trades bounded old/new overlap for portable, auditable update hazards.
class CoverageAtlas final
{
public:
    /// Physical texels per axis; one fixed page, no mipmaps.
    static constexpr foundation::uint32 EXTENT = 256;
    /// Maximum independently cached sized glyphs, including whitespace.
    static constexpr foundation::usize MAX_GLYPHS = 512;
    /// Construct without allocation.
    CoverageAtlas() noexcept;
    /// Release CPU shadows; published RHI texture owners are independent.
    ~CoverageAtlas();
    /// Reserve the CPU page/cache once. Ready if already initialized.
    [[nodiscard]] rhi::RasterStatus Initialize() noexcept;
    /// Clear cells and keys; callers must rebuild lists against newly published page.
    void Reset() noexcept;
    /// Find an exact key; NotReady on cache miss, preserving output.
    [[nodiscard]] rhi::RasterStatus Find(GlyphKey key, AtlasGlyph& output) const noexcept;
    /// Copy normalized top-down tightly packed R8 coverage and signed bearings.
    /// byteCount must equal width*height; zero-area glyphs accept null/zero bytes.
    /// First insertion fixes that key's metrics/content. Repeated keys return it.
    /// Invalid data/capacity failure preserve output and the existing cache/page.
    [[nodiscard]] rhi::RasterStatus Insert(GlyphKey key,
                                           const foundation::uint8* bytes,
                                           foundation::usize byteCount,
                                           foundation::uint32 width,
                                           foundation::uint32 height,
                                           foundation::int32 bearingLeft,
                                           foundation::int32 bearingTop,
                                           AtlasGlyph& output) noexcept;
    /// Copy the complete page into a new sampled R8 resource between frames.
    /// Null output required; Ready/Pending owns a version. GetStatus on the RHI
    /// texture polls native/browser validation. No old texture or atlas cell changes.
    [[nodiscard]] rhi::RasterStatus Publish(rhi::DeviceHandle device, rhi::TextureHandle& output) const noexcept;
    /// Number of cached sized glyphs since Reset.
    [[nodiscard]] foundation::usize GlyphCount() const noexcept;
    /// Texels occupied including filtering gutters; zero-area glyphs use none.
    [[nodiscard]] foundation::usize OccupiedTexels() const noexcept;
    /// Borrow complete top-down R8 shadow until insertion/reset/destruction.
    [[nodiscard]] const foundation::uint8* Coverage() const noexcept;

private:
    struct State;
    foundation::UniquePtr<State> mState;
};
/// Append copied text quads into the generic overlay list in supplied painter order.
/// Baseline is in physical top-left pixels; no hidden DPI scaling. This validates
/// all quads and available arena/runs before appending; failure preserves geometry.
/// Use the matching published atlas version when preparing the overlay. Empty ink
/// glyphs generate no triangles. All coordinates/colors must be finite.
[[nodiscard]] rhi::RasterStatus AppendText(renderer::OverlayList& list,
                                           const TextQuad* quads,
                                           foundation::usize count,
                                           foundation::math::Vector2 baseline,
                                           foundation::math::Vector4 color,
                                           foundation::uint32 width,
                                           foundation::uint32 height,
                                           const renderer::OverlayStyle& style = {}) noexcept;
} // namespace ludus::graphics::text
