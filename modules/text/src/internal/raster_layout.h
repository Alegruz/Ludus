#pragma once

#include <ludus/foundation/base/types.h>

#include <ludus/text/status.h>

namespace ludus::text::internal
{
struct RasterInput final
{
    ludus::foundation::uint32 Width = 0;
    ludus::foundation::uint32 Height = 0;
    ludus::foundation::int32 Pitch = 0;
};

struct RasterLayout final
{
    ludus::foundation::usize Pitch = 0;
    ludus::foundation::usize CoverageBytes = 0;
};

// No allocation or buffer access. Failure preserves out; empty glyphs succeed.
[[nodiscard]] Status TryRasterLayout(RasterInput input, RasterLayout& out) noexcept;
} // namespace ludus::text::internal
