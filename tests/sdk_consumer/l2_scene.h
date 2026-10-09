#pragma once
#include <ludus/graphics/renderer/overlays.hpp>
#include <ludus/graphics/text/coverage_atlas.hpp>
namespace ludus::qa
{
/// Public-only coverage fixture, equivalent to a copied CPU raster result.
inline graphics::rhi::RasterStatus BuildL2List(graphics::renderer::OverlayList& list,
                                               graphics::text::CoverageAtlas& atlas) noexcept
{
    using namespace foundation;
    using Status = graphics::rhi::RasterStatus;
    auto status = list.Initialize();
    if (status != Status::Ready)
    {
        return status;
    }
    status = atlas.Initialize();
    if (status != Status::Ready)
    {
        return status;
    }
    uint8 mask[64];
    for (usize i = 0; i < 64; ++i)
    {
        mask[i] = i < 32 ? 255 : 0;
    }
    graphics::text::AtlasGlyph glyph;
    status = atlas.Insert({1, 1, 16}, mask, 64, 8, 8, 0, 8, glyph);
    if (status != Status::Ready)
    {
        return status;
    }
    list.Clear();
    status = list.Quad({0, 32, 32, 64}, {}, {1, 0, 0, 1}, 96, 64);
    if (status != Status::Ready)
    {
        return status;
    }
    graphics::renderer::OverlayStyle clipped;
    clipped.Clip = true;
    clipped.Scissor = {16, 32, 16, 32};
    status = list.Quad({0, 32, 48, 64}, {}, {0, 1, 0, .5F}, 96, 64, false, clipped);
    if (status != Status::Ready)
    {
        return status;
    }
    const graphics::text::TextQuad quad{{0, 0, 32, 32}, glyph.Uv, 7};
    status = graphics::text::AppendText(list, &quad, 1, {64, 0}, {1, 1, 1, 1}, 96, 64);
    if (status != Status::Ready)
    {
        return status;
    }
    return list.Line({-.5F, .5F, -.5F, 1}, {.5F, .5F, .5F, 1}, {0, 0, 1, 1}, 4, 96, 64);
}
} // namespace ludus::qa
