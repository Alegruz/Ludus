#include "internal/lifecycle.h"
#include "reference_raster.h"
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <ludus/foundation/math/scalar.hpp>
#include <ludus/foundation/math/vector.hpp>
#include <ludus/graphics/renderer/overlays.hpp>
#include <ludus/graphics/renderer/renderer.hpp>
#include <ludus/graphics/renderer/views.hpp>
#include <ludus/graphics/rhi/device.h>
#include <ludus/graphics/rhi/lifetime.h>
#include <ludus/graphics/rhi/raster.h>
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
    void EnableViews()
    {
        const rhi::RasterShaderInput inputs[]{{0, rhi::RasterVertexFormat::Float4}};
        const rhi::RasterBinding bindings[]{{0, rhi::RasterBindingKind::Texture2D, rhi::RasterVisibility::Fragment, 0},
                                            {1, rhi::RasterBindingKind::Sampler, rhi::RasterVisibility::Fragment, 0}};
        rhi::RasterShaderDescription vertex, fragment;
        vertex.Artifact.Stage = rhi::ShaderStage::Vertex;
        vertex.Artifact.Wgsl = "composite vertex fixture";
        vertex.Artifact.WgslEntry = "vertexMain";
        vertex.Inputs = inputs;
        fragment.Artifact.Stage = rhi::ShaderStage::Fragment;
        fragment.Artifact.Wgsl = "composite fragment fixture";
        fragment.Artifact.WgslEntry = "fragmentMain";
        fragment.Bindings = bindings;
        REQUIRE(Renderer.InitializeViews(vertex, fragment) == rhi::RasterStatus::Ready);
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
TEST_CASE("L1 explicit passes pair reverse-Z depth and retain detached offscreen presentations", "[renderer][views]")
{
    Session session;
    session.EnableViews();
    const auto item = session.Item(1);
    rr::Snapshot snapshot;
    rr::PreparedView view;
    rr::ViewReport report;
    REQUIRE(session.Renderer.CreateSnapshot(&item, 1, snapshot) == rhi::RasterStatus::Ready);
    rr::ViewDescription description;
    description.Depth = rr::DepthConvention::ReverseZ;
    REQUIRE(session.Renderer.PrepareView(snapshot, description, view, report) == rhi::RasterStatus::Ready);
    rhi::SubmissionToken completion;
    CHECK(session.Renderer.Submit(view, session.Surface, completion) == rhi::RasterStatus::InvalidDescription);
    rhi::TextureHandle target;
    REQUIRE(rhi::CreateTexture(session.Device, {32, 32, rhi::RasterFormat::Rgba8Unorm, true}, {}, target) ==
            rhi::RasterStatus::Ready);
    rr::ViewMapping mapping;
    REQUIRE(rr::ResolveViewMapping({32, 32}, {0, 0, 48, 64}, 96, 64, mapping) == rhi::RasterStatus::Ready);
    rr::Presentation presentation;
    REQUIRE(session.Renderer.PreparePresentation(target, mapping, presentation) == rhi::RasterStatus::Ready);
    REQUIRE(rhi::BeginFrame(session.Device, session.Surface) == rhi::DeviceStatus::Ready);
    rhi::RasterPassDescription pass;
    pass.Color = target;
    REQUIRE(session.Renderer.DrawView(view, pass) == rhi::RasterStatus::Ready);
    CHECK(rhi::reference::Pass.Description.ClearDepth == 0);
    CHECK(rhi::reference::Pipelines[rhi::reference::Packet.Pipeline].DepthCompare == rhi::RasterDepthCompare::Greater);
    REQUIRE(session.Renderer.DrawPresentation(presentation) == rhi::RasterStatus::Ready);
    CHECK(rhi::reference::Pass.Description.ColorLoad == rhi::RasterLoad::Load);
    CHECK(rhi::reference::Pass.Description.Viewport.Width == 48);
    CHECK(rhi::reference::Pass.Description.Viewport.Y == 8);
    REQUIRE(rhi::EndFrame(session.Device, completion) == rhi::RasterStatus::Ready);
    REQUIRE(rhi::Destroy(session.Device, target) == rhi::RasterStatus::Ready);
    const auto stale = presentation;
    REQUIRE(session.Renderer.Release(presentation) == rhi::RasterStatus::Ready);
    CHECK(session.Renderer.GetStatus(stale) == rhi::RasterStatus::InvalidHandle);
    CHECK(rhi::reference::Destroys[static_cast<usize>(rhi::internal::RasterKind::Texture)] == 0);
    rhi::reference::Completed = rhi::reference::Submitted;
    REQUIRE(rhi::PollLifetime(session.Device) == rhi::RasterStatus::Ready);
    CHECK(rhi::reference::Destroys[static_cast<usize>(rhi::internal::RasterKind::Texture)] == 1);
}
TEST_CASE("L1 undefined targets, stale identities and overflowing rectangles reject explicitly", "[renderer][views]")
{
    Session session;
    session.EnableViews();
    rhi::TextureHandle target;
    REQUIRE(rhi::CreateTexture(session.Device, {32, 32, rhi::RasterFormat::Rgba8Unorm, true}, {}, target) ==
            rhi::RasterStatus::Ready);
    rr::ViewMapping mapping;
    REQUIRE(rr::ResolveViewMapping({32, 32}, {0, 0, 96, 64}, 96, 64, mapping) == rhi::RasterStatus::Ready);
    rr::Presentation presentation;
    REQUIRE(session.Renderer.PreparePresentation(target, mapping, presentation) == rhi::RasterStatus::Ready);
    rr::ViewMapping resizedMapping;
    REQUIRE(rr::ResolveViewMapping({32, 32}, {0, 0, 120, 80}, 120, 80, resizedMapping) == rhi::RasterStatus::Ready);
    rr::Presentation resized;
    REQUIRE(session.Renderer.PreparePresentation(target, resizedMapping, resized) == rhi::RasterStatus::Ready);
    REQUIRE(rhi::BeginFrame(session.Device, session.Surface) == rhi::DeviceStatus::Ready);
    const auto passesBeforeResize = rhi::reference::Passes;
    CHECK(session.Renderer.DrawPresentation(resized) == rhi::RasterStatus::NotReady);
    CHECK(rhi::reference::Passes == passesBeforeResize);
    CHECK(session.Renderer.DrawPresentation(presentation) == rhi::RasterStatus::InvalidDescription);
    rhi::RasterPassDescription pass;
    pass.UseViewport = true;
    pass.Viewport = {95, 0, ~uint32{0}, 1};
    const auto previous = rhi::reference::Passes;
    CHECK(rhi::BeginRasterPass(session.Device, pass) == rhi::RasterStatus::InvalidDescription);
    CHECK(rhi::reference::Passes == previous);
    pass.Viewport = {0, 0, 96, 64};
    pass.UseScissor = true;
    pass.Scissor = {~uint32{0}, 0, ~uint32{0}, 1};
    CHECK(rhi::BeginRasterPass(session.Device, pass) == rhi::RasterStatus::Ready);
    rhi::SubmissionToken completion;
    REQUIRE(rhi::EndFrame(session.Device, completion) == rhi::RasterStatus::Ready);
    REQUIRE(session.Renderer.Release(presentation) == rhi::RasterStatus::Ready);
    REQUIRE(rhi::Destroy(session.Device, target) == rhi::RasterStatus::Ready);
}
TEST_CASE("L1 infinite reverse-Z culling keeps distant geometry and reports capacity without publishing",
          "[renderer][views]")
{
    Session session;
    session.EnableViews();
    auto item = session.Item(11, {0, 0, -100000});
    rr::Snapshot snapshot;
    REQUIRE(session.Renderer.CreateSnapshot(&item, 1, snapshot) == rhi::RasterStatus::Ready);
    rr::ViewMapping mapping;
    REQUIRE(rr::ResolveViewMapping({96, 64}, {0, 0, 96, 64}, 96, 64, mapping) == rhi::RasterStatus::Ready);
    rr::ProjectionDescription projection;
    projection.Kind = rr::ProjectionKind::PerspectiveInfinite;
    rr::ViewDescription description;
    REQUIRE(rr::BuildViewDescription(projection, mapping, description) == rhi::RasterStatus::Ready);
    rr::PreparedView view;
    rr::ViewReport report;
    REQUIRE(session.Renderer.PrepareView(snapshot, description, view, report) == rhi::RasterStatus::Ready);
    CHECK(report.Visible == 1);
    rhi::TextureHandle target;
    REQUIRE(rhi::CreateTexture(session.Device, {96, 64, rhi::RasterFormat::Rgba8Unorm, true}, {}, target) ==
            rhi::RasterStatus::Ready);
    rr::Presentation presentations[4];
    for (auto& presentation : presentations)
    {
        REQUIRE(session.Renderer.PreparePresentation(target, mapping, presentation) == rhi::RasterStatus::Ready);
    }
    rr::Presentation overflow;
    CHECK(session.Renderer.PreparePresentation(target, mapping, overflow) == rhi::RasterStatus::CapacityExceeded);
    REQUIRE(session.Renderer.Release(presentations[0]) == rhi::RasterStatus::Ready);
    REQUIRE(session.Renderer.PreparePresentation(target, mapping, overflow) == rhi::RasterStatus::Ready);
}
TEST_CASE("Scheduled dynamic snapshots preserve older instance bytes through GPU completion", "[renderer][l2]")
{
    Session session;
    auto item = session.Item(1);
    rr::Snapshot snapshot;
    REQUIRE(session.Renderer.CreateSnapshot(&item, 1, snapshot) == rhi::RasterStatus::Ready);
    rr::ViewDescription description;
    description.ScheduledUpload = true;
    rr::PreparedView oldView;
    rr::ViewReport report;
    REQUIRE(session.Renderer.PrepareView(snapshot, description, oldView, report) == rhi::RasterStatus::Pending);
    CHECK(session.Renderer.GetStatus(oldView) == rhi::RasterStatus::Pending);
    item.Transform.Translation.X = .5F;
    REQUIRE(session.Renderer.ReplaceSnapshot(&item, 1, snapshot) == rhi::RasterStatus::Ready);
    rr::PreparedView newView;
    REQUIRE(session.Renderer.PrepareView(snapshot, description, newView, report) == rhi::RasterStatus::Pending);
    // Complete both copies independently of the still outstanding frame ordinal.
    for (auto& transfer : rhi::reference::Transfers[0])
    {
        transfer = rhi::RasterStatus::Ready;
    }
    REQUIRE(rhi::PollLifetime(session.Device) == rhi::RasterStatus::Ready);
    REQUIRE(session.Renderer.GetStatus(oldView) == rhi::RasterStatus::Ready);
    REQUIRE(session.Renderer.GetStatus(newView) == rhi::RasterStatus::Ready);
    rhi::SubmissionToken oldSubmission;
    REQUIRE(session.Renderer.Submit(oldView, session.Surface, oldSubmission) == rhi::RasterStatus::Ready);
    const auto oldSlot = rhi::reference::Packet.Vertices[1];
    uint8 oldBytes[80];
    for (usize i = 0; i < 80; ++i)
    {
        oldBytes[i] = rhi::reference::BufferBytes[oldSlot][i];
    }
    REQUIRE(session.Renderer.Release(oldView) == rhi::RasterStatus::Ready);
    rhi::SubmissionToken newSubmission;
    REQUIRE(session.Renderer.Submit(newView, session.Surface, newSubmission) == rhi::RasterStatus::Ready);
    CHECK(rhi::reference::Packet.Vertices[1] != oldSlot);
    for (usize i = 0; i < 80; ++i)
    {
        CHECK(rhi::reference::BufferBytes[oldSlot][i] == oldBytes[i]);
    }
    CHECK(rhi::GetStatus(session.Device, oldSubmission) == rhi::RasterStatus::Pending);
    REQUIRE(session.Renderer.Release(newView) == rhi::RasterStatus::Ready);
    rhi::reference::Completed = rhi::reference::Submitted;
    REQUIRE(rhi::PollLifetime(session.Device) == rhi::RasterStatus::Ready);
    CHECK(rhi::GetStatus(session.Device, oldSubmission) == rhi::RasterStatus::Ready);
}
TEST_CASE("Portable line expansion clips before divide and handles degenerate and invalid segments", "[renderer][l2]")
{
    rr::OverlayList list;
    REQUIRE(list.Initialize() == rhi::RasterStatus::Ready);
    REQUIRE(list.Line({0, 0, -.5F, -1}, {.5F, 0, .5F, 1}, {1, 1, 1, 1}, 2, 96, 64) == rhi::RasterStatus::Ready);
    CHECK(list.VertexCount() == 6);
    for (usize i = 0; i < 6; ++i)
    {
        CHECK(math::IsFinite(list.Vertices()[i].Clip));
        CHECK(list.Vertices()[i].Clip.Z >= 0);
        CHECK(list.Vertices()[i].Clip.Z <= 1);
    }
    REQUIRE(list.Line({0, 0, .5F, 1}, {0, 0, .5F, 1}, {1, 0, 0, 1}, .1F, 96, 64) == rhi::RasterStatus::Ready);
    CHECK(list.VertexCount() == 12);
    CHECK(list.Vertices()[6].Clip.X != list.Vertices()[7].Clip.X);
    CHECK(list.Vertices()[6].Clip.Y != list.Vertices()[8].Clip.Y);
    REQUIRE(list.Line({0, 0, -1, 1}, {1, 0, -2, 1}, {1, 1, 1, 1}, 2, 96, 64) == rhi::RasterStatus::Ready);
    CHECK(list.VertexCount() == 12);
    CHECK(list.Line({}, {}, {1, 1, 1, 1}, 0, 96, 64) == rhi::RasterStatus::InvalidDescription);
    CHECK(list.VertexCount() == 12);
    rr::OverlayStyle clip;
    clip.Clip = true;
    clip.Scissor = {5, 7, 10, 10};
    REQUIRE(list.Quad({0, 0, 10, 10}, {}, {0, 1, 0, .5F}, 96, 64, false, clip) == rhi::RasterStatus::Ready);
    CHECK(list.CommandCount() == 2);
    REQUIRE(list.Quad({0, 0, 10, 10}, {}, {0, 0, 1, .5F}, 96, 64) == rhi::RasterStatus::Ready);
    CHECK(list.CommandCount() == 3);
    while (list.VertexCount() < rr::OverlayList::MAX_VERTICES)
    {
        REQUIRE(list.Quad({0, 0, 1, 1}, {}, {1, 1, 1, 1}, 96, 64) == rhi::RasterStatus::Ready);
    }
    CHECK(list.Quad({0, 0, 1, 1}, {}, {1, 1, 1, 1}, 96, 64) == rhi::RasterStatus::CapacityExceeded);
    CHECK(list.DroppedCommands() == 1);
    list.Clear();
    CHECK(list.VertexCount() == 0);
    CHECK(list.DroppedCommands() == 0);
}
TEST_CASE("Prepared overlays copy commands, cancel uploads safely and retain exact atlas versions", "[renderer][l2]")
{
    Session session;
    rr::OverlayRenderer overlays;
    const rhi::RasterShaderInput inputs[]{{0, rhi::RasterVertexFormat::Float4},
                                          {1, rhi::RasterVertexFormat::Float4},
                                          {2, rhi::RasterVertexFormat::Float4}};
    const rhi::RasterBinding bindings[]{{0, rhi::RasterBindingKind::Texture2D, rhi::RasterVisibility::Fragment, 0},
                                        {1, rhi::RasterBindingKind::Sampler, rhi::RasterVisibility::Fragment, 0}};
    rhi::RasterShaderDescription vertex, fragment;
    vertex.Artifact.Stage = rhi::ShaderStage::Vertex;
    vertex.Artifact.Wgsl = "overlay vertex fixture";
    vertex.Artifact.WgslEntry = "vertexMain";
    vertex.Inputs = inputs;
    fragment.Artifact.Stage = rhi::ShaderStage::Fragment;
    fragment.Artifact.Wgsl = "overlay fragment fixture";
    fragment.Artifact.WgslEntry = "fragmentMain";
    fragment.Bindings = bindings;
    REQUIRE(overlays.Initialize(session.Device, vertex, fragment) == rhi::RasterStatus::Ready);
    rr::OverlayList list;
    REQUIRE(list.Initialize() == rhi::RasterStatus::Ready);
    rr::OverlayStyle clip;
    clip.Clip = true;
    clip.Scissor = {5, 5, 20, 20};
    REQUIRE(list.Quad({0, 0, 30, 30}, {0, 0, 1, 1}, {1, 1, 1, 1}, 96, 64, true, clip) == rhi::RasterStatus::Ready);
    const uint8 coverage[4]{255, 128, 0, 0};
    rhi::TextureHandle atlas;
    REQUIRE(rhi::CreateTexture(session.Device, {2, 1, rhi::RasterFormat::R8Unorm}, {coverage, 4}, atlas) ==
            rhi::RasterStatus::Ready);
    rr::PreparedOverlay prepared, cancelled;
    REQUIRE(overlays.Prepare(list, atlas, prepared) == rhi::RasterStatus::Pending);
    REQUIRE(overlays.Prepare(list, atlas, cancelled) == rhi::RasterStatus::Pending);
    REQUIRE(overlays.Release(cancelled) == rhi::RasterStatus::Ready);
    CHECK(rhi::reference::TransferOccupied[0][1]);
    list.Clear();
    REQUIRE(rhi::Destroy(session.Device, atlas) == rhi::RasterStatus::Ready);
    CHECK(rhi::reference::Destroys[1] == 0);
    for (auto& transfer : rhi::reference::Transfers[0])
    {
        transfer = rhi::RasterStatus::Ready;
    }
    REQUIRE(overlays.Poll() == rhi::RasterStatus::Ready);
    REQUIRE(overlays.GetStatus(prepared) == rhi::RasterStatus::Ready);
    REQUIRE(rhi::BeginFrame(session.Device, session.Surface) == rhi::DeviceStatus::Ready);
    rhi::RasterPassDescription pass;
    pass.DepthStore = rhi::RasterStore::Store;
    REQUIRE(rhi::BeginRasterPass(session.Device, pass) == rhi::RasterStatus::Ready);
    pass.UseScissor = true;
    pass.Scissor = {10, 0, 20, 30};
    REQUIRE(overlays.Draw(prepared, pass) == rhi::RasterStatus::Ready);
    CHECK(rhi::reference::Pass.Description.Scissor.X == 10);
    CHECK(rhi::reference::Pass.Description.Scissor.Width == 15);
    CHECK(rhi::reference::Pass.Description.DepthReadOnly);
    rhi::SubmissionToken completion;
    REQUIRE(rhi::EndFrame(session.Device, completion) == rhi::RasterStatus::Ready);
    const auto stale = prepared;
    REQUIRE(overlays.Release(prepared) == rhi::RasterStatus::Ready);
    CHECK(overlays.GetStatus(stale) == rhi::RasterStatus::InvalidHandle);
    CHECK(rhi::reference::Destroys[1] == 0);
    rhi::reference::Completed = rhi::reference::Submitted;
    REQUIRE(overlays.Poll() == rhi::RasterStatus::Ready);
    CHECK(rhi::reference::Destroys[1] == 1);
    REQUIRE(list.Quad({0, 0, 10, 10}, {}, {1, 1, 1, 1}, 96, 64) == rhi::RasterStatus::Ready);
    rr::PreparedOverlay versions[4], extra;
    for (auto& version : versions)
    {
        REQUIRE(overlays.Prepare(list, {}, version) == rhi::RasterStatus::Pending);
    }
    CHECK(overlays.Prepare(list, {}, extra) == rhi::RasterStatus::CapacityExceeded);
    const auto previous = versions[0];
    rhi::internal::Fail(rhi::backend::PendingToken, rhi::StartupError::DeviceLost);
    CHECK(overlays.GetStatus(previous) == rhi::RasterStatus::DeviceLost);
    overlays.Reset();
    session.Renderer.Reset();
    rhi::Shutdown();
    session.Device = {};
    session.Surface = {};
    REQUIRE(rhi::CreateDevice({}, {}, {}, session.Device, session.Surface) == rhi::DeviceStatus::Pending);
    rhi::internal::Complete(rhi::backend::PendingToken, rhi::StartupError::None, {4096, 16384});
    REQUIRE(overlays.Initialize(session.Device, vertex, fragment) == rhi::RasterStatus::Ready);
    CHECK(overlays.GetStatus(previous) == rhi::RasterStatus::InvalidHandle);
    REQUIRE(overlays.Prepare(list, {}, extra) == rhi::RasterStatus::Pending);
    REQUIRE(overlays.Release(extra) == rhi::RasterStatus::Ready);
    overlays.Reset();
}
