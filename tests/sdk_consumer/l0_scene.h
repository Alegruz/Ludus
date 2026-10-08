#pragma once
#include <ludus/foundation/base/types.h>

#include <ludus/foundation/math/vector.hpp>
#include <ludus/graphics/renderer/renderer.hpp>
#include <ludus/graphics/rhi/device.h>
#include <ludus/graphics/rhi/lifetime.h>
#include <ludus/graphics/rhi/raster.h>
namespace ludus::qa
{
using namespace foundation;
namespace rr = graphics::renderer;
namespace rhi = graphics::rhi;
inline constexpr math::Vector3 L0_QUAD[]{{-.2F, -.2F, 0}, {.2F, -.2F, 0}, {.2F, .2F, 0}, {-.2F, .2F, 0}};
inline constexpr uint32 L0_INDICES[]{0, 1, 2, 0, 2, 3};
inline constexpr math::Vector3 L0_TRIANGLE[]{{-.2F, -.2F, 0}, {.2F, -.2F, 0}, {0, .2F, 0}};
inline rhi::RasterStatus BuildL0Scene(rr::Renderer& renderer,
                                      rr::Mesh quad,
                                      rr::Mesh triangle,
                                      bool reference,
                                      rr::Snapshot& snapshot,
                                      rr::PreparedView& view,
                                      rr::ViewReport& report) noexcept
{
    rr::SceneItem items[8];
    for (usize i = 0; i < 8; ++i)
    {
        items[i].Geometry = quad;
        items[i].SourceId = i + 1;
    }
    items[0].Transform.Columns[0].X = items[0].Transform.Columns[1].Y = 4;
    items[0].Transform.Translation.Z = .8F;
    items[0].Material.Color = {.1F, .1F, .2F, 1};
    items[1].Transform.Translation = {-.5F, .35F, .2F};
    items[1].Material.Color = {1, 0, 0, 1};
    items[2].Geometry = triangle;
    items[2].Transform.Translation = {0, .35F, .2F};
    items[2].Material.Color = {0, 0, 1, 1};
    items[3].Transform.Translation = {.5F, .35F, .2F};
    items[3].Material.Color = {0, 1, 0, 1};
    items[4].Transform.Translation = {3, 0, .2F};
    items[5].Transform.Translation = {0, -.4F, .3F}; // default error material
    items[6].Pass = items[7].Pass = rr::Layer::Overlay;
    items[6].Material.Color = {1, 0, 0, .5F};
    items[7].Material.Color = {0, 1, 0, .5F};
    auto status = renderer.CreateSnapshot(items, 8, snapshot);
    if (status != rhi::RasterStatus::Ready)
        return status;
    rr::ViewDescription description;
    description.Reference = reference;
    return renderer.PrepareView(snapshot, description, view, report);
}
} // namespace ludus::qa
