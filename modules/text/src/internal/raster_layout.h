#pragma once

#include <ludus/foundation/base/types.h>

#include <ludus/text/status.h>

namespace ludus::text::internal
{
struct RasterLayout final
{
    ludus::foundation::usize Pitch = 0;
    ludus::foundation::usize CoverageBytes = 0;
};

// No allocation or buffer access. Failure preserves out; empty glyphs succeed.
[[nodiscard]] Status TryRasterLayout(ludus::foundation::uint32 width,
                                     ludus::foundation::uint32 height,
                                     ludus::foundation::int32 pitch,
                                     RasterLayout& out) noexcept;
} // namespace ludus::text::internal
