#include "internal/raster_layout.h"

#include <ludus/foundation/base/checked_integer.hpp>

namespace ludus::text::internal
{
// Thanks to the FreeType Project, FreeType API Reference, "FT_Bitmap", for the
// signed pitch/padded-row contract:
// https://freetype.org/freetype2/docs/reference/ft2-basic_types.html#ft_bitmap
// Thanks to Eric Lengyel, "Bit Hacks for Games", Game Engine Gems 2, chapter 24,
// section 24.1, for the signed-minimum counterexample. We widen before negating
// and check byte products/extents; no bit-hack listing is copied.
// Review: docs/architecture/primitive-types.md, "Boundary adoption audit".
Status TryRasterLayout(ludus::foundation::uint32 width,
                       ludus::foundation::uint32 height,
                       ludus::foundation::int32 pitch,
                       RasterLayout& out) noexcept
{
    using namespace ludus::foundation;
    RasterLayout next;
    if (width == 0 || height == 0)
    {
        out = next;
        return Status::Ok;
    }
    const int64 widePitch = pitch;
    const int64 magnitude = widePitch < 0 ? -widePitch : widePitch;
    usize lastRow{};
    usize sourceExtent{};
    if (!TryIntegerCast(magnitude, next.Pitch))
    {
        return Status::ResourceLimit;
    }
    if (next.Pitch < width)
    {
        return Status::BackendFailure;
    }
    if (!TryMultiply(static_cast<usize>(width), static_cast<usize>(height), next.CoverageBytes) ||
        !TryMultiply(static_cast<usize>(height - 1), next.Pitch, lastRow) ||
        !TryAdd(lastRow, static_cast<usize>(width), sourceExtent) || next.CoverageBytes > (~usize{0} >> 1) ||
        sourceExtent > (~usize{0} >> 1))
    {
        return Status::ResourceLimit;
    }
    out = next;
    return Status::Ok;
}
} // namespace ludus::text::internal
