#include "internal/lifecycle.h"
#include "reference_raster.h"
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <ludus/graphics/renderer/renderer.hpp>
using namespace ludus::foundation;
namespace rr = ludus::graphics::renderer;
namespace rhi = ludus::graphics::rhi;
namespace ludus::graphics::rhi::backend
{
extern uint32 PendingToken;
extern StartupError ImmediateError;
extern FrameStatus NextFrame;
extern Backend ActiveKind;
extern bool SelectionSupported;
} // namespace ludus::graphics::rhi::backend
namespace
{
struct Session
{
    rhi::DeviceHandle Device;
    rhi::SurfaceHandle Surface;
    rr::Renderer Renderer;
    rr::Mesh Mesh;
    Session()
    {
        rhi::Shutdown();
        rhi::reference::Reset();
        rhi::backend::ImmediateError = rhi::StartupError::None;
        rhi::backend::NextFrame = rhi::FrameStatus::Ready;
        rhi::backend::ActiveKind = rhi::Backend::WebGPU;
        rhi::backend::SelectionSupported = true;
        REQUIRE(rhi::CreateDevice({}, {}, {}, Device, Surface) == rhi::DeviceStatus::Pending);
        rhi::internal::Complete(rhi::backend::PendingToken, rhi::StartupError::None, {4096, 16384});
        Initialize(Renderer);
        const math::Vector3 positions[]{{-.1F, -.1F, .5F}, {.1F, -.1F, .5F}, {0, .1F, .5F}};
        const uint32 indices[]{0, 1, 2};
        REQUIRE(Renderer.CreateMesh({positions, 3, indices, 3}, Mesh) == rhi::RasterStatus::Ready);
    }
    void Initialize(rr::Renderer& renderer)
    {
        rhi::RasterShaderInput inputs[6];
        for (uint32 i = 0; i < 6; ++i)
        {
            inputs[i] = {i, i == 0 ? rhi::RasterVertexFormat::Float3 : rhi::RasterVertexFormat::Float4};
        }
        rhi::RasterShaderDescription vertex, fragment;
        vertex.Artifact.Stage = rhi::ShaderStage::Vertex;
        vertex.Artifact.Wgsl = "flat vertex fixture";
        vertex.Artifact.WgslEntry = "vertexMain";
        vertex.Inputs = inputs;
        fragment.Artifact.Stage = rhi::ShaderStage::Fragment;
        fragment.Artifact.Wgsl = "flat fragment fixture";
        fragment.Artifact.WgslEntry = "fragmentMain";
        REQUIRE(renderer.Initialize(Device, vertex, fragment) == rhi::RasterStatus::Ready);
    }
    ~Session()
    {
        Renderer.Reset();
        rhi::Shutdown();
    }
    [[nodiscard]] rr::SceneItem Item(uint64 id, math::Vector3 translation = {}) const
    {
        rr::SceneItem item;
        item.Geometry = Mesh;
        item.SourceId = id;
        item.Transform.Translation = translation;
        return item;
    }
};
} // namespace
TEST_CASE("Snapshots copy values and retain detached mesh incarnations through view and GPU leases", "[renderer]")
{
    Session session;
    auto item = session.Item(41);
    rr::Snapshot snapshot;
    REQUIRE(session.Renderer.CreateSnapshot(&item, 1, snapshot) == rhi::RasterStatus::Ready);
    item.Transform.Translation.X = 100;
    item.Material.Color = {};
    const auto stale = session.Mesh;
    REQUIRE(session.Renderer.Release(session.Mesh) == rhi::RasterStatus::Ready);
    CHECK(session.Renderer.GetStatus(stale) == rhi::RasterStatus::InvalidHandle);
    rr::PreparedView view;
    rr::ViewReport report;
    REQUIRE(session.Renderer.PrepareView(snapshot, {}, view, report) == rhi::RasterStatus::Ready);
    CHECK(report.Visible == 1);
    CHECK(report.SourceIds[0] == 41);
    REQUIRE(session.Renderer.Release(snapshot) == rhi::RasterStatus::Ready);
    rhi::SubmissionToken completion;
    REQUIRE(session.Renderer.Submit(view, session.Surface, completion) == rhi::RasterStatus::Ready);
    REQUIRE(session.Renderer.Release(view) == rhi::RasterStatus::Ready);
    CHECK(rhi::reference::Destroys[0] == 0);
    rhi::reference::Completed = rhi::reference::Submitted;
    REQUIRE(rhi::PollLifetime(session.Device) == rhi::RasterStatus::Ready);
    CHECK(rhi::reference::Destroys[0] == 3);
}
TEST_CASE("Visibility retains tangent/sheared bounds and overlays keep painter order", "[renderer]")
{
    Session session;
    rr::SceneItem items[]{session.Item(1, {3, 0, 0}), session.Item(2, {1.1F, 0, 0}), session.Item(3), session.Item(4)};
    items[2].Pass = items[3].Pass = rr::Layer::Overlay;
    items[2].Material.Color = {1, 0, 0, .5F};
    items[3].Material.Color = {0, 1, 0, .5F};
    rr::Snapshot snapshot;
    REQUIRE(session.Renderer.CreateSnapshot(items, 4, snapshot) == rhi::RasterStatus::Ready);
    rr::PreparedView view;
    rr::ViewReport report;
    REQUIRE(session.Renderer.PrepareView(snapshot, {}, view, report) == rhi::RasterStatus::Ready);
    CHECK(report.Visible == 3);
    CHECK(report.Culled == 1);
    CHECK(report.Draws == 2);
    CHECK(report.SourceIds[0] == 2);
    CHECK(report.SourceIds[1] == 3);
    CHECK(report.SourceIds[2] == 4);
    rhi::SubmissionToken completion;
    REQUIRE(session.Renderer.Submit(view, session.Surface, completion) == rhi::RasterStatus::Ready);
    CHECK(rhi::reference::Draws == 2);
}
TEST_CASE("Invalid scene admission preserves outputs and stale identities never resolve", "[renderer]")
{
    Session session;
    rr::Snapshot snapshot;
    rr::SceneItem items[]{session.Item(1), session.Item(1)};
    CHECK(session.Renderer.CreateSnapshot(items, 2, snapshot) == rhi::RasterStatus::InvalidDescription);
    items[1].SourceId = 2;
    items[1].Transform.Translation.X = std::numeric_limits<float32>::infinity();
    CHECK(session.Renderer.CreateSnapshot(items, 2, snapshot) == rhi::RasterStatus::InvalidDescription);
    CHECK(session.Renderer.CreateSnapshot(items, 257, snapshot) == rhi::RasterStatus::CapacityExceeded);
    REQUIRE(session.Renderer.CreateSnapshot(items, 1, snapshot) == rhi::RasterStatus::Ready);
    const auto stale = snapshot;
    REQUIRE(session.Renderer.Release(snapshot) == rhi::RasterStatus::Ready);
    REQUIRE(session.Renderer.CreateSnapshot(items, 1, snapshot) == rhi::RasterStatus::Ready);
    rr::PreparedView view;
    rr::ViewReport report;
    report.Visible = 77;
    CHECK(session.Renderer.PrepareView(stale, {}, view, report) == rhi::RasterStatus::InvalidHandle);
    CHECK(report.Visible == 77);
    rr::ViewDescription invalid;
    invalid.WorldToClip = {};
    CHECK(session.Renderer.PrepareView(snapshot, invalid, view, report) == rhi::RasterStatus::InvalidDescription);
    CHECK(report.Visible == 77);
}
TEST_CASE("Prepared submission retries skipped targets and creates no new resources", "[renderer]")
{
    Session session;
    const auto item = session.Item(1);
    rr::Snapshot snapshot;
    rr::PreparedView view;
    rr::ViewReport report;
    REQUIRE(session.Renderer.CreateSnapshot(&item, 1, snapshot) == rhi::RasterStatus::Ready);
    REQUIRE(session.Renderer.PrepareView(snapshot, {}, view, report) == rhi::RasterStatus::Ready);
    const auto creates = rhi::reference::Creates[0];
    rhi::backend::NextFrame = rhi::FrameStatus::Skipped;
    rhi::SubmissionToken completion;
    CHECK(session.Renderer.Submit(view, session.Surface, completion) == rhi::RasterStatus::NotReady);
    rhi::backend::NextFrame = rhi::FrameStatus::Ready;
    REQUIRE(session.Renderer.Submit(view, session.Surface, completion) == rhi::RasterStatus::Ready);
    for (usize frame = 0; frame < 12; ++frame)
    {
        rhi::reference::Completed = rhi::reference::Submitted;
        REQUIRE(rhi::PollLifetime(session.Device) == rhi::RasterStatus::Ready);
        completion = {};
        REQUIRE(session.Renderer.Submit(view, session.Surface, completion) == rhi::RasterStatus::Ready);
    }
    CHECK(rhi::reference::Creates[0] == creates);
    CHECK(rhi::reference::Draws == 13);
}
TEST_CASE("Snapshot and view capacities recover after release, including clear-only scenes", "[renderer]")
{
    Session session;
    rr::Snapshot snapshots[4], extra;
    for (auto& snapshot : snapshots)
    {
        REQUIRE(session.Renderer.CreateSnapshot(nullptr, 0, snapshot) == rhi::RasterStatus::Ready);
    }
    CHECK(session.Renderer.CreateSnapshot(nullptr, 0, extra) == rhi::RasterStatus::CapacityExceeded);
    rr::PreparedView views[4], extraView;
    rr::ViewReport report;
    for (auto& view : views)
    {
        REQUIRE(session.Renderer.PrepareView(snapshots[0], {}, view, report) == rhi::RasterStatus::Ready);
    }
    CHECK(session.Renderer.PrepareView(snapshots[0], {}, extraView, report) == rhi::RasterStatus::CapacityExceeded);
    REQUIRE(session.Renderer.Release(views[0]) == rhi::RasterStatus::Ready);
    REQUIRE(session.Renderer.PrepareView(snapshots[0], {}, extraView, report) == rhi::RasterStatus::Ready);
    rhi::SubmissionToken completion;
    REQUIRE(session.Renderer.Submit(extraView, session.Surface, completion) == rhi::RasterStatus::Ready);
    CHECK(report.Draws == 0);
    CHECK(rhi::reference::Draws == 0);
}
TEST_CASE("Many objects instance within the portable limit and overlap sorting preserves depth ties", "[renderer]")
{
    Session session;
    rr::SceneItem items[256];
    for (usize i = 0; i < 256; ++i)
    {
        items[i] = session.Item(i + 1);
        items[i].Transform.Columns[0].X = .1F;
        items[i].Transform.Columns[1].Y = .1F;
        items[i].Transform.Columns[0].Y = .05F; // shear, with outward-rounded bounds
        items[i].Transform.Translation.X = -.8F + static_cast<float32>(i % 16) * .1F;
        const usize row = i / 16;
        items[i].Transform.Translation.Y = -.8F + static_cast<float32>(row) * .1F;
    }
    rr::Snapshot snapshot;
    rr::PreparedView view;
    rr::ViewReport report;
    REQUIRE(session.Renderer.CreateSnapshot(items, 256, snapshot) == rhi::RasterStatus::Ready);
    REQUIRE(session.Renderer.PrepareView(snapshot, {}, view, report) == rhi::RasterStatus::Ready);
    CHECK(report.Visible == 256);
    CHECK(report.Draws == 1);
    rhi::SubmissionToken completion;
    REQUIRE(session.Renderer.Submit(view, session.Surface, completion) == rhi::RasterStatus::Ready);
    CHECK(rhi::reference::Packet.InstanceCount == 256);
    REQUIRE(session.Renderer.Release(view) == rhi::RasterStatus::Ready);
    REQUIRE(session.Renderer.Release(snapshot) == rhi::RasterStatus::Ready);
    rhi::reference::Completed = rhi::reference::Submitted;
    REQUIRE(rhi::PollLifetime(session.Device) == rhi::RasterStatus::Ready);
    rr::Mesh second;
    const math::Vector3 positions[]{{-.2F, -.2F, .5F}, {.2F, -.2F, .5F}, {0, .2F, .5F}};
    const uint32 indices[]{0, 1, 2};
    REQUIRE(session.Renderer.CreateMesh({positions, 3, indices, 3}, second) == rhi::RasterStatus::Ready);
    items[0] = session.Item(1);
    items[1] = session.Item(2);
    items[2] = session.Item(3);
    items[0].Geometry = items[2].Geometry = second;
    REQUIRE(session.Renderer.CreateSnapshot(items, 3, snapshot) == rhi::RasterStatus::Ready);
    REQUIRE(session.Renderer.PrepareView(snapshot, {}, view, report) == rhi::RasterStatus::Ready);
    CHECK(report.SourceIds[0] == 1);
    CHECK(report.SourceIds[1] == 2);
    CHECK(report.SourceIds[2] == 3);
    CHECK(report.Draws == 3);
}
TEST_CASE("Pending geometry is not published as a snapshot and foreign meshes reject", "[renderer]")
{
    Session session;
    rhi::reference::Next = rhi::RasterStatus::Pending;
    rr::Mesh pending;
    const math::Vector3 positions[]{{0, 0, .5F}, {.2F, 0, .5F}, {0, .2F, .5F}};
    const uint32 indices[]{0, 1, 2};
    REQUIRE(session.Renderer.CreateMesh({positions, 3, indices, 3}, pending) == rhi::RasterStatus::Pending);
    auto item = session.Item(1);
    item.Geometry = pending;
    rr::Snapshot snapshot;
    CHECK(session.Renderer.CreateSnapshot(&item, 1, snapshot) == rhi::RasterStatus::NotReady);
    item.Geometry = {};
    CHECK(session.Renderer.CreateSnapshot(&item, 1, snapshot) == rhi::RasterStatus::InvalidHandle);
    rhi::reference::Next = rhi::RasterStatus::Ready;
    item.Geometry = session.Mesh;
    REQUIRE(session.Renderer.CreateSnapshot(&item, 1, snapshot) == rhi::RasterStatus::Ready);
    rr::Renderer foreign;
    session.Initialize(foreign);
    CHECK(foreign.GetStatus(session.Mesh) == rhi::RasterStatus::InvalidHandle);
    rr::Snapshot foreignScene;
    CHECK(foreign.CreateSnapshot(&item, 1, foreignScene) == rhi::RasterStatus::InvalidHandle);
    const auto stale = session.Mesh;
    session.Renderer.Reset();
    session.Initialize(session.Renderer);
    CHECK(session.Renderer.GetStatus(stale) == rhi::RasterStatus::InvalidHandle);
}
