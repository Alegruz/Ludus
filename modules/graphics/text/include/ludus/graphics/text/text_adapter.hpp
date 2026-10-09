#pragma once

#include <ludus/foundation/base/types.h>

#include <ludus/graphics/text/coverage_atlas.hpp>
#include <ludus/text/font_system.h>

namespace ludus::graphics::text
{
/// Cold native CPU Text bridge. Copies shaped glyph IDs, 26.6 positions and original
/// clusters; immediately copies FreeType's temporary coverage into GraphicsText's
/// sized cache. Never reshapes, itemizes paragraphs, or retains the FontSystem/layout.
/// @param atlas Initialized sized glyph cache receiving copied coverage.
/// @param system Borrowed live native CPU font owner, used only during this call.
/// @param layout Live immutable shaped run in system.
/// @param fontVersion Nonzero content incarnation assigned by the caller; do not
/// reuse after reloading a font or with another FontSystem/font.
/// @param output Caller storage for at least the layout's GlyphCount placements.
/// @param capacity Number of TextQuad entries, bounded by MAX_GLYPHS.
/// @param count Receives complete placement count only on Ready.
/// Output/count stay unchanged on failure; successfully cached glyphs may remain
/// after a later miss fails. Missing glyph IDs rasterize the face's visible .notdef.
/// Atlas pressure is CapacityExceeded; caller can keep its earlier ready label.
/// Native only: browser CPU font dependencies are not yet an engine target. The
/// generic R8 atlas/quad adapter consumes copied CPU fixtures on every backend.
[[nodiscard]] rhi::RasterStatus PrepareText(CoverageAtlas& atlas,
                                            ludus::text::FontSystem* system,
                                            ludus::text::LayoutId layout,
                                            foundation::uint64 fontVersion,
                                            TextQuad* output,
                                            foundation::usize capacity,
                                            foundation::usize& count) noexcept;
} // namespace ludus::graphics::text
