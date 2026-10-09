#pragma once
#include <ludus/graphics/renderer/renderer.hpp>
namespace ludus::qa
{
inline constexpr foundation::usize L3_MATERIALS = 7;
inline graphics::renderer::MaterialDescription L3Material(foundation::usize index,
                                                          graphics::rhi::TextureHandle texture) noexcept
{
    namespace rr = graphics::renderer;
    rr::MaterialDescription material;
    if (index == 0 || index == 1 || index == 4)
        material.Texture = texture;
    if (index == 0)
    {
        material.Lod = 1;
        material.Sampler.MipFilter = graphics::rhi::RasterMipFilter::Nearest;
    }
    if (index == 2)
        material.Tint = {1, 0, 0, 1};
    if (index == 3)
        material.Tint = {0, 0, 1, 1};
    if (index == 4)
    {
        material.Alpha = rr::MaterialAlpha::Masked;
        material.AlphaCutoff = .5F;
    }
    if (index == 5)
    {
        material.Alpha = rr::MaterialAlpha::Painter;
        material.Tint = {0, 0, 1, .5F};
    }
    if (index == 6)
        material.Tint = {1, 0, 1, 1};
    return material;
}
inline void L3Items(graphics::renderer::Mesh mesh,
                    const graphics::renderer::MaterialVersion* materials,
                    graphics::renderer::SceneItem* items) noexcept
{
    using namespace foundation;
    namespace rr = graphics::renderer;
    const usize versions[]{0, 1, 2, 2, 3, 4, 6, 5};
    const math::Vector4 rectangles[]{{0, 0, 32, 32},
                                     {32, 0, 64, 32},
                                     {0, 32, 32, 64},
                                     {32, 32, 64, 64},
                                     {64, 0, 96, 32},
                                     {64, 0, 96, 32},
                                     {64, 32, 96, 64},
                                     {32, 32, 64, 64}};
    for (usize i = 0; i < 8; ++i)
    {
        auto& item = items[i];
        item = {};
        item.Geometry = mesh;
        item.SourceId = i + 1;
        item.Surface = materials[versions[i]];
        const auto& r = rectangles[i];
        item.Transform.Columns[0].X = (r.Z - r.X) / 96;
        item.Transform.Columns[1].Y = (r.W - r.Y) / 64;
        item.Transform.Translation = {(r.X + r.Z) / 96 - 1, 1 - (r.Y + r.W) / 64, i == 5 ? -.1F : 0};
        if (i == 7)
            item.Pass = rr::Layer::Overlay;
    }
}
inline graphics::rhi::RasterStatus L3Mesh(graphics::renderer::Renderer& renderer,
                                          graphics::renderer::Mesh& output) noexcept
{
    using namespace foundation;
    const math::Vector3 positions[]{{-1, -1, .5F}, {1, -1, .5F}, {1, 1, .5F}, {-1, 1, .5F}};
    const math::Vector2 uv[]{{0, 1}, {1, 1}, {1, 0}, {0, 0}};
    const uint32 indices[]{0, 1, 2, 0, 2, 3};
    return renderer.CreateMesh({positions, 4, indices, 6, uv}, output);
}
} // namespace ludus::qa
