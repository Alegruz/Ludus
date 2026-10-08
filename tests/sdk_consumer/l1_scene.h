#pragma once
#include <ludus/foundation/base/types.h>

#include <ludus/foundation/math/scalar.hpp>
#include <ludus/foundation/math/vector.hpp>
#include <ludus/graphics/renderer/renderer.hpp>
#include <ludus/graphics/renderer/views.hpp>
#include <ludus/graphics/rhi/graph.h>

namespace ludus::qa
{
using namespace foundation;
namespace rr = graphics::renderer;
namespace rhi = graphics::rhi;
inline constexpr math::Vector3 L1_QUAD[]{{-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, 1, 0}};
inline constexpr uint32 L1_INDICES[]{0, 1, 2, 0, 2, 3};
inline rhi::RasterStatus BuildL1Snapshot(rr::Renderer& renderer, rr::Mesh mesh, rr::Snapshot& snapshot) noexcept
{
    rr::SceneItem items[5];
    for (usize i = 0; i < 5; ++i)
    {
        items[i].Geometry = mesh;
        items[i].SourceId = i + 1;
    }
    items[0].Transform.Columns[0].X = items[0].Transform.Columns[1].Y = 8;
    items[0].Transform.Translation.Z = -8;
    items[0].Material.Color = {0, 0, 1, 1};
    items[1].Transform.Translation.Z = -2;
    items[1].Material.Color = {1, 0, 0, 1};
    // Farther green geometry is deliberately submitted AFTER the nearer red quad.
    items[2].Transform.Columns[0].X = items[2].Transform.Columns[1].Y = 2;
    items[2].Transform.Translation.Z = -4;
    items[2].Material.Color = {0, 1, 0, 1};
    items[3].Transform.Translation = {100, 0, -2};
    items[4].Pass = rr::Layer::Overlay;
    items[4].Transform.Columns[0].X = items[4].Transform.Columns[1].Y = .15F;
    items[4].Transform.Translation = {.65F, .65F, .5F};
    items[4].Material.Color = {1, 0, 1, 1};
    return renderer.CreateSnapshot(items, 5, snapshot);
}
inline rhi::RasterStatus BuildL1View(rr::Renderer& renderer,
                                     rr::Snapshot snapshot,
                                     bool perspective,
                                     bool infinite,
                                     bool reference,
                                     rr::PreparedView& output,
                                     rr::ViewReport& report) noexcept
{
    rr::ViewMapping mapping;
    auto status = rr::ResolveViewMapping({48, 48}, {0, 0, 48, 48}, 48, 48, mapping);
    if (status != rhi::RasterStatus::Ready)
    {
        return status;
    }
    rr::ProjectionDescription projection;
    projection.Kind = perspective
                          ? (infinite ? rr::ProjectionKind::PerspectiveInfinite : rr::ProjectionKind::Perspective)
                          : rr::ProjectionKind::Orthographic;
    projection.VerticalFovRadians = math::kHalfPiF;
    projection.OrthographicHeight = 4;
    projection.Near = 1;
    projection.Far = 10;
    rr::ViewDescription description;
    status = rr::BuildViewDescription(projection, mapping, description);
    if (status != rhi::RasterStatus::Ready)
    {
        return status;
    }
    description.Reference = reference;
    return renderer.PrepareView(snapshot, description, output, report);
}
inline rhi::RasterStatus PrepareL1Presentations(rr::Renderer& renderer,
                                                const rhi::TextureHandle (&targets)[2],
                                                uint32 width,
                                                uint32 height,
                                                rr::Presentation (&output)[2],
                                                rr::ScreenFit fit = rr::ScreenFit::Fit) noexcept
{
    for (uint32 i = 0; i < 2; ++i)
    {
        rhi::RasterRectangle region;
        auto status = rr::SplitViewRectangle({0, 0, width, height}, 2, 1, i, 0, region);
        if (status != rhi::RasterStatus::Ready)
        {
            return status;
        }
        rr::ViewMapping mapping;
        status = rr::ResolveViewMapping({48, 48, fit}, region, width, height, mapping);
        if (status != rhi::RasterStatus::Ready)
        {
            return status;
        }
        status = renderer.PreparePresentation(targets[i], mapping, output[i]);
        if (status != rhi::RasterStatus::Ready && status != rhi::RasterStatus::Pending)
        {
            return status;
        }
    }
    return rhi::RasterStatus::Ready;
}
inline rhi::RasterStatus DrawL1Frame(rr::Renderer& renderer,
                                     const rr::PreparedView (&views)[2],
                                     const rhi::TextureHandle (&targets)[2],
                                     const rr::Presentation (&presentations)[2]) noexcept
{
    for (usize i = 0; i < 2; ++i)
    {
        rhi::RasterPassDescription pass;
        pass.Color = targets[i];
        auto status = renderer.DrawView(views[i], pass);
        if (status != rhi::RasterStatus::Ready)
        {
            return status;
        }
        // Interleave render/composite to stress preserving the already composed neighbor.
        status = renderer.DrawPresentation(presentations[i]);
        if (status != rhi::RasterStatus::Ready)
        {
            return status;
        }
    }
    return rhi::RasterStatus::Ready;
}
} // namespace ludus::qa
