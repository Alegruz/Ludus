#include "internal/lifecycle.h"
#include "reference_raster.h"

#include <ludus/foundation/base/compiler.h>

#include <ludus/foundation/containers/static_array.hpp>
#include <ludus/graphics/renderer/render_graph.hpp>
#include <ludus/graphics/rhi/device.h>

#include <catch2/catch_test_macros.hpp>

using namespace ludus::foundation;
namespace renderer = ludus::graphics::renderer;
namespace rhi = ludus::graphics::rhi;
namespace format = ludus::content::render_graph_format;
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
constexpr usize INPUT_COUNT = 6;
constexpr usize COMPOSITE_INPUT_COUNT = 1;
constexpr usize COMPOSITE_BINDING_COUNT = 2;
constexpr usize RESOURCE_COUNT = 2;
constexpr usize PASS_COUNT = 1;
constexpr usize VERTEX_COUNT = 3;
constexpr usize INDEX_COUNT = 3;
constexpr usize MESH_COUNT = 1;
constexpr usize ITEM_COUNT = 1;
constexpr usize CAMERA_COUNT = 1;
struct GraphFixture final
{
    rhi::DeviceHandle Device{};
    rhi::SurfaceHandle Surface{};
    renderer::RenderGraph Graph;
    StaticArray<format::Resource, RESOURCE_COUNT> Resources{
        {{96, 64, format::Aspect::Color, 0, 0, 1}, {96, 64, format::Aspect::Depth, 0, 0, 0}}};
    StaticArray<format::Pass, PASS_COUNT> Passes{{{format::Technique::ColorAndDepth,
                                                   0,
                                                   0,
                                                   1,
                                                   format::Load::Clear,
                                                   format::Store::Store,
                                                   format::Load::Clear,
                                                   format::Store::Store,
                                                   0}}};
    StaticArray<format::Camera, CAMERA_COUNT> Targets{{{0}}};
    StaticArray<rhi::RasterShaderInput, INPUT_COUNT> Inputs{};
    StaticArray<rhi::RasterShaderInput, COMPOSITE_INPUT_COUNT> CompositeInputs{{{0, rhi::RasterVertexFormat::Float4}}};
    StaticArray<rhi::RasterBinding, COMPOSITE_BINDING_COUNT> CompositeBindings{
        {{0, rhi::RasterBindingKind::Texture2D, rhi::RasterVisibility::Fragment, 0},
         {1, rhi::RasterBindingKind::Sampler, rhi::RasterVisibility::Fragment, 0}}};
    StaticArray<math::Vector3, VERTEX_COUNT> Positions{{{-.1F, -.1F, .5F}, {.1F, -.1F, .5F}, {0, .1F, .5F}}};
    StaticArray<uint32, INDEX_COUNT> Indices{{0, 1, 2}};
    StaticArray<renderer::RenderGraphMesh, MESH_COUNT> Meshes{};
    StaticArray<renderer::RenderGraphItem, ITEM_COUNT> Items{};
    StaticArray<renderer::ViewDescription, CAMERA_COUNT> Cameras{};
    explicit GraphFixture(bool hasRasterProfile = true)
    {
        rhi::Shutdown();
        rhi::reference::Reset();
        rhi::reference::ProfileAvailable = hasRasterProfile;
        rhi::backend::ImmediateError = rhi::StartupError::None;
        rhi::backend::NextFrame = rhi::FrameStatus::Ready;
        rhi::backend::ActiveKind = rhi::Backend::WebGPU;
        rhi::backend::SelectionSupported = true;
        REQUIRE(rhi::CreateDevice({}, {}, {}, Device, Surface) == rhi::DeviceStatus::Pending);
        rhi::internal::Complete(rhi::backend::PendingToken, rhi::StartupError::None, {4096, 16384});
        for (uint32 i = 0; i < INPUT_COUNT; ++i)
        {
            Inputs[i] = {i, i == 0 ? rhi::RasterVertexFormat::Float3 : rhi::RasterVertexFormat::Float4};
        }
        Meshes[0].Geometry.Positions = Positions.GetData();
        Meshes[0].Geometry.VertexCount = Positions.GetSize();
        Meshes[0].Geometry.Indices = Indices.GetData();
        Meshes[0].Geometry.IndexCount = Indices.GetSize();
        Items[0].SourceId = 42;
        Items[0].Material.Color = {1, 1, 1, 1};
    }
    ~GraphFixture()
    {
        Graph.Reset();
        rhi::Shutdown();
    }
    [[nodiscard]] LUDUS_INLINE renderer::RenderGraphPlan MakePlan() const noexcept
    {
        return {{format::VERSION, RESOURCE_COUNT, PASS_COUNT, CAMERA_COUNT},
                {Resources.GetData(), Resources.GetSize()},
                {Targets.GetData(), Targets.GetSize()},
                {Passes.GetData(), Passes.GetSize()}};
    }
    [[nodiscard]] renderer::RenderGraphPrograms MakePrograms() const
    {
        renderer::RenderGraphPrograms programs;
        programs.ColorVertex.Artifact.Stage = rhi::ShaderStage::Vertex;
        programs.ColorVertex.Artifact.Wgsl = "flat vertex fixture";
        programs.ColorVertex.Artifact.WgslEntry = "VertexMain";
        programs.ColorVertex.Inputs = Inputs.AsSpan();
        programs.ColorFragment.Artifact.Stage = rhi::ShaderStage::Fragment;
        programs.ColorFragment.Artifact.Wgsl = "flat fragment fixture";
        programs.ColorFragment.Artifact.WgslEntry = "FragmentMain";
        programs.DepthVertex = programs.ColorVertex;
        programs.DepthFragment = programs.ColorFragment;
        programs.CompositeVertex.Artifact.Stage = rhi::ShaderStage::Vertex;
        programs.CompositeVertex.Artifact.Wgsl = "composite vertex fixture";
        programs.CompositeVertex.Artifact.WgslEntry = "VertexMain";
        programs.CompositeVertex.Inputs = CompositeInputs.AsSpan();
        programs.CompositeFragment.Artifact.Stage = rhi::ShaderStage::Fragment;
        programs.CompositeFragment.Artifact.Wgsl = "composite fragment fixture";
        programs.CompositeFragment.Artifact.WgslEntry = "FragmentMain";
        programs.CompositeFragment.Bindings = CompositeBindings.AsSpan();
        return programs;
    }
    rhi::RasterStatus Initialize()
    {
        return Graph.Initialize(Device, MakePlan(), MakePrograms());
    }
    rhi::RasterStatus
    Update(renderer::RenderGraphReport& outReport,
           renderer::RenderGraphCoveragePolicy policy = renderer::RenderGraphCoveragePolicy::RejectIncomplete,
           uint64 revision = 1)
    {
        return Graph.Update({Meshes.GetData(), Meshes.GetSize()},
                            {Items.GetData(), Items.GetSize()},
                            {Cameras.GetData(), Cameras.GetSize()},
                            revision,
                            policy,
                            outReport);
    }
};
} // namespace
TEST_CASE("RG2 renders a retained extraction without creating shaders or pipelines during draw", "[render-graph]")
{
    GraphFixture fixture;
    REQUIRE(fixture.Initialize() == rhi::RasterStatus::Ready);
    renderer::RenderGraphReport report;
    REQUIRE(fixture.Update(report) == rhi::RasterStatus::Ready);
    CHECK(report.Covered == 2);
    CHECK(report.IsReady == true);
    const usize shaderCreates = rhi::reference::Creates[static_cast<usize>(rhi::internal::RasterKind::Shader)];
    const usize pipelineCreates = rhi::reference::Creates[static_cast<usize>(rhi::internal::RasterKind::Pipeline)];
    rhi::SubmissionToken completion;
    REQUIRE(fixture.Graph.Render(fixture.Surface, 96, 64, completion) == rhi::RasterStatus::Ready);
    CHECK(completion.IsNull() == false);
    CHECK(rhi::reference::Draws == 1);
    CHECK(rhi::reference::Creates[static_cast<usize>(rhi::internal::RasterKind::Shader)] == shaderCreates);
    CHECK(rhi::reference::Creates[static_cast<usize>(rhi::internal::RasterKind::Pipeline)] == pipelineCreates);
    rhi::TextureHandle output;
    rhi::TextureAspect aspect;
    CHECK(fixture.Graph.GetExport(0, output, aspect) == rhi::RasterStatus::NotReady);
    rhi::reference::Completed = rhi::reference::Submitted;
    REQUIRE(fixture.Graph.GetExport(0, output, aspect) == rhi::RasterStatus::Ready);
    CHECK(aspect == rhi::TextureAspect::Color);
}
TEST_CASE("RG2 unsupported material coverage retains the previous drawable scene", "[render-graph]")
{
    GraphFixture fixture;
    REQUIRE(fixture.Initialize() == rhi::RasterStatus::Ready);
    renderer::RenderGraphReport report;
    REQUIRE(fixture.Update(report) == rhi::RasterStatus::Ready);
    fixture.Items[0].Coverage = renderer::RenderGraphMaterial::Masked;
    CHECK(fixture.Update(report, renderer::RenderGraphCoveragePolicy::RejectIncomplete, 2) ==
          rhi::RasterStatus::Unsupported);
    CHECK(report.Unsupported == 2);
    CHECK(report.IsAccepted == false);
    CHECK(fixture.Graph.GetCoverage()[0].Reason == renderer::RenderGraphReason::Material);
    rhi::SubmissionToken completion;
    REQUIRE(fixture.Graph.Render(fixture.Surface, 96, 64, completion) == rhi::RasterStatus::Ready);
    CHECK(rhi::reference::Draws == 1);
}
TEST_CASE("RG2 diagnostic color renders without claiming required depth coverage", "[render-graph]")
{
    GraphFixture fixture;
    REQUIRE(fixture.Initialize() == rhi::RasterStatus::Ready);
    fixture.Items[0].Coverage = renderer::RenderGraphMaterial::Unsupported;
    renderer::RenderGraphReport report;
    REQUIRE(fixture.Update(report, renderer::RenderGraphCoveragePolicy::DiagnosticColor) == rhi::RasterStatus::Ready);
    CHECK(report.Covered == 0);
    CHECK(report.Unsupported == 2);
    const BorrowedView<const renderer::RenderGraphCoverageRecord> records = fixture.Graph.GetCoverage();
    CHECK(records[0].HasDiagnosticRoute == true);
    CHECK(records[1].HasDiagnosticRoute == false);
    rhi::SubmissionToken completion;
    REQUIRE(fixture.Graph.Render(fixture.Surface, 96, 64, completion) == rhi::RasterStatus::Ready);
    CHECK(rhi::reference::Draws == 1);
}
TEST_CASE("RG2 authored exclusion is different from unsupported or culled geometry", "[render-graph]")
{
    GraphFixture fixture;
    REQUIRE(fixture.Initialize() == rhi::RasterStatus::Ready);
    fixture.Items[0].IsIntentionallyExcluded = true;
    renderer::RenderGraphReport report;
    REQUIRE(fixture.Update(report) == rhi::RasterStatus::Ready);
    CHECK(report.Excluded == 2);
    CHECK(report.Covered == 0);
    CHECK(report.Unsupported == 0);
    rhi::SubmissionToken completion;
    REQUIRE(fixture.Graph.Render(fixture.Surface, 96, 64, completion) == rhi::RasterStatus::Ready);
    CHECK(rhi::reference::Draws == 0);
}
TEST_CASE("RG2 rejects undefined exported depth and malformed numeric plans before GPU setup", "[render-graph]")
{
    GraphFixture fixture;
    fixture.Resources[1].IsExported = 1;
    fixture.Targets[0].Target = 1;
    fixture.Passes[0].DepthStore = format::Store::Discard;
    CHECK(fixture.Initialize() == rhi::RasterStatus::InvalidDescription);
    fixture.Passes[0].DepthStore = format::Store::Store;
    fixture.Resources[1].Backing = 31;
    CHECK(fixture.Initialize() == rhi::RasterStatus::InvalidDescription);
}
TEST_CASE("RG2 pending and failed replacements retain the last drawable extraction", "[render-graph]")
{
    GraphFixture fixture;
    REQUIRE(fixture.Initialize() == rhi::RasterStatus::Ready);
    renderer::RenderGraphReport report;
    REQUIRE(fixture.Update(report) == rhi::RasterStatus::Ready);
    rhi::reference::Next = rhi::RasterStatus::Pending;
    fixture.Items[0].MeshToWorld.Translation.X = 100;
    REQUIRE(fixture.Update(report, renderer::RenderGraphCoveragePolicy::RejectIncomplete, 2) ==
            rhi::RasterStatus::Pending);
    CHECK(report.IsAccepted == true);
    CHECK(report.IsReady == false);
    rhi::SubmissionToken completion;
    REQUIRE(fixture.Graph.Render(fixture.Surface, 96, 64, completion) == rhi::RasterStatus::Ready);
    CHECK(rhi::reference::Draws == 1);
    for (usize i = 0; i < rhi::reference::PendingCount; ++i)
    {
        rhi::internal::RasterComplete(rhi::reference::PendingRequests[i], rhi::RasterStatus::Failed);
    }
    rhi::reference::Next = rhi::RasterStatus::Ready;
    CHECK(fixture.Graph.Poll() == rhi::RasterStatus::Failed);
    rhi::reference::Completed = rhi::reference::Submitted;
    completion = {};
    REQUIRE(fixture.Graph.Render(fixture.Surface, 96, 64, completion) == rhi::RasterStatus::Ready);
    CHECK(rhi::reference::Draws == 2);
}
TEST_CASE("RG2 camera edits and streaming source changes publish only after readiness", "[render-graph]")
{
    GraphFixture fixture;
    REQUIRE(fixture.Initialize() == rhi::RasterStatus::Ready);
    renderer::RenderGraphReport report;
    REQUIRE(fixture.Update(report) == rhi::RasterStatus::Ready);
    rhi::reference::Next = rhi::RasterStatus::Pending;
    fixture.Items[0].MeshToWorld.Translation.X = 100;
    REQUIRE(fixture.Update(report, renderer::RenderGraphCoveragePolicy::RejectIncomplete, 2) ==
            rhi::RasterStatus::Pending);
    for (usize i = 0; i < rhi::reference::PendingCount; ++i)
    {
        rhi::internal::RasterComplete(rhi::reference::PendingRequests[i], rhi::RasterStatus::Ready);
    }
    rhi::reference::Next = rhi::RasterStatus::Ready;
    REQUIRE(fixture.Graph.Poll() == rhi::RasterStatus::Ready);
    rhi::SubmissionToken completion;
    REQUIRE(fixture.Graph.Render(fixture.Surface, 96, 64, completion) == rhi::RasterStatus::Ready);
    CHECK(rhi::reference::Draws == 0);
    fixture.Items[0].MeshToWorld.Translation.X = 0;
    REQUIRE(fixture.Update(report, renderer::RenderGraphCoveragePolicy::RejectIncomplete, 3) ==
            rhi::RasterStatus::Ready);
    rhi::reference::Completed = rhi::reference::Submitted;
    completion = {};
    REQUIRE(fixture.Graph.Render(fixture.Surface, 96, 64, completion) == rhi::RasterStatus::Ready);
    CHECK(rhi::reference::Draws == 1);
}
TEST_CASE("RG2 unmatched layouts can use explicit bounds proxies without reading unknown vertex data", "[render-graph]")
{
    GraphFixture fixture;
    REQUIRE(fixture.Initialize() == rhi::RasterStatus::Ready);
    fixture.Meshes[0].Layout = renderer::RenderGraphLayout::Unsupported;
    fixture.Meshes[0].Geometry = {};
    fixture.Meshes[0].HasDiagnosticBounds = true;
    fixture.Meshes[0].BoundsMesh = {{-.1F, -.1F, .4F}, {.1F, .1F, .6F}};
    renderer::RenderGraphReport report;
    REQUIRE(fixture.Update(report, renderer::RenderGraphCoveragePolicy::DiagnosticColor) == rhi::RasterStatus::Ready);
    CHECK(report.Unsupported == 2);
    CHECK(fixture.Graph.GetCoverage()[0].Reason == renderer::RenderGraphReason::Layout);
    rhi::SubmissionToken completion;
    REQUIRE(fixture.Graph.Render(fixture.Surface, 96, 64, completion) == rhi::RasterStatus::Ready);
    CHECK(rhi::reference::Draws == 1);
}
TEST_CASE("RG2 duplicate draw-item identities are conflicting even under diagnostic policy", "[render-graph]")
{
    GraphFixture fixture;
    REQUIRE(fixture.Initialize() == rhi::RasterStatus::Ready);
    constexpr usize DUPLICATE_COUNT = 2;
    StaticArray<renderer::RenderGraphItem, DUPLICATE_COUNT> items{{fixture.Items[0], fixture.Items[0]}};
    renderer::RenderGraphReport report;
    CHECK(fixture.Graph.Update({fixture.Meshes.GetData(), fixture.Meshes.GetSize()},
                               {items.GetData(), items.GetSize()},
                               {fixture.Cameras.GetData(), fixture.Cameras.GetSize()},
                               1,
                               renderer::RenderGraphCoveragePolicy::DiagnosticColor,
                               report) == rhi::RasterStatus::Unsupported);
    CHECK(report.Conflicting == 4);
    CHECK(report.IsAccepted == false);
}
TEST_CASE("RG2 standalone depth output is authored, initialized and completion gated", "[render-graph]")
{
    GraphFixture fixture;
    fixture.Resources[0].IsExported = 0;
    fixture.Resources[1].IsExported = 1;
    fixture.Targets[0].Target = 1;
    fixture.Passes[0].Program = format::Technique::DepthOnly;
    fixture.Passes[0].Color = format::ABSENT;
    fixture.Passes[0].ColorLoad = format::Load::Discard;
    fixture.Passes[0].ColorStore = format::Store::Discard;
    REQUIRE(fixture.Initialize() == rhi::RasterStatus::Ready);
    renderer::RenderGraphReport report;
    REQUIRE(fixture.Update(report) == rhi::RasterStatus::Ready);
    CHECK(report.Covered == 1);
    rhi::SubmissionToken completion;
    REQUIRE(fixture.Graph.Render(fixture.Surface, 96, 64, completion) == rhi::RasterStatus::Ready);
    rhi::reference::Completed = rhi::reference::Submitted;
    rhi::TextureHandle texture;
    rhi::TextureAspect aspect;
    REQUIRE(fixture.Graph.GetExport(1, texture, aspect) == rhi::RasterStatus::Ready);
    CHECK(aspect == rhi::TextureAspect::Depth);
    rhi::RasterTextureState textureState;
    REQUIRE(rhi::GetTextureState(fixture.Device, texture, textureState) == rhi::RasterStatus::Ready);
    CHECK(textureState.DepthDefined == true);
}
TEST_CASE("RG2 rejects active cameras with no route, independent depth backing and unsupported interfaces",
          "[render-graph]")
{
    GraphFixture fixture;
    renderer::RenderGraphPlan plan = fixture.MakePlan();
    plan.Description.CameraCount = 2;
    CHECK(fixture.Graph.Initialize(fixture.Device, plan, fixture.MakePrograms()) ==
          rhi::RasterStatus::InvalidDescription);
    fixture.Resources[1].Backing = 1;
    CHECK(fixture.Initialize() == rhi::RasterStatus::Unsupported);
    fixture.Resources[1].Backing = 0;
    renderer::RenderGraphPrograms programs = fixture.MakePrograms();
    programs.ColorVertex.Inputs = {};
    CHECK(fixture.Graph.Initialize(fixture.Device, fixture.MakePlan(), programs) == rhi::RasterStatus::Unsupported);
}
TEST_CASE("RG2 a rejected newer report stays distinct from an older pending publication", "[render-graph]")
{
    GraphFixture fixture;
    REQUIRE(fixture.Initialize() == rhi::RasterStatus::Ready);
    renderer::RenderGraphReport report;
    REQUIRE(fixture.Update(report) == rhi::RasterStatus::Ready);
    CHECK(fixture.Graph.GetDrawableSceneRevision() == 1);
    rhi::reference::Next = rhi::RasterStatus::Pending;
    REQUIRE(fixture.Update(report, renderer::RenderGraphCoveragePolicy::RejectIncomplete, 2) ==
            rhi::RasterStatus::Pending);
    fixture.Items[0].Coverage = renderer::RenderGraphMaterial::Masked;
    REQUIRE(fixture.Update(report, renderer::RenderGraphCoveragePolicy::RejectIncomplete, 3) ==
            rhi::RasterStatus::Unsupported);
    for (usize i = 0; i < rhi::reference::PendingCount; ++i)
    {
        rhi::internal::RasterComplete(rhi::reference::PendingRequests[i], rhi::RasterStatus::Ready);
    }
    rhi::reference::Next = rhi::RasterStatus::Ready;
    REQUIRE(fixture.Graph.Poll() == rhi::RasterStatus::Ready);
    CHECK(fixture.Graph.GetDrawableSceneRevision() == 2);
    CHECK(fixture.Graph.GetReport().SceneRevision == 3);
    CHECK(fixture.Graph.GetReport().IsAccepted == false);
    CHECK(fixture.Graph.GetReport().IsReady == false);
}
TEST_CASE("RG2 resize preflight rejects before frame acquisition and keeps the drawable revision", "[render-graph]")
{
    GraphFixture fixture;
    fixture.Resources[0].IsSurface = 1;
    fixture.Resources[0].IsExported = 0;
    fixture.Resources[1].IsSurface = 1;
    REQUIRE(fixture.Initialize() == rhi::RasterStatus::Ready);
    renderer::RenderGraphReport report;
    REQUIRE(fixture.Update(report) == rhi::RasterStatus::Ready);
    rhi::SubmissionToken completion;
    CHECK(fixture.Graph.Render(fixture.Surface, 192, 128, completion) == rhi::RasterStatus::NotReady);
    CHECK(completion.IsNull() == true);
    CHECK(fixture.Graph.GetDrawableSceneRevision() == 1);
    rhi::DeviceInfo information;
    REQUIRE(rhi::GetDeviceInfo(fixture.Device, information) == rhi::DeviceStatus::Ready);
    CHECK(information.FrameOpen == false);
    REQUIRE(fixture.Graph.Render(fixture.Surface, 96, 64, completion) == rhi::RasterStatus::Ready);
    CHECK(rhi::reference::Draws == 1);
}
TEST_CASE("RG2 culling never hides incompatible streamed material slots", "[render-graph]")
{
    GraphFixture fixture;
    REQUIRE(fixture.Initialize() == rhi::RasterStatus::Ready);
    constexpr usize MIXED_ITEM_COUNT = 2;
    StaticArray<renderer::RenderGraphItem, MIXED_ITEM_COUNT> items{{fixture.Items[0], fixture.Items[0]}};
    items[1].SourceId = 43;
    items[1].Coverage = renderer::RenderGraphMaterial::Transparent;
    items[1].MeshToWorld.Translation.X = 100;
    renderer::RenderGraphReport report;
    CHECK(fixture.Graph.Update({fixture.Meshes.GetData(), fixture.Meshes.GetSize()},
                               {items.GetData(), items.GetSize()},
                               {fixture.Cameras.GetData(), fixture.Cameras.GetSize()},
                               1,
                               renderer::RenderGraphCoveragePolicy::RejectIncomplete,
                               report) == rhi::RasterStatus::Unsupported);
    CHECK(report.Covered == 2);
    CHECK(report.Unsupported == 2);
    CHECK(fixture.Graph.GetCoverage().GetSize() == 4);
}
TEST_CASE("RG2 required camera target cannot be replaced by an unrelated export", "[render-graph]")
{
    GraphFixture fixture;
    fixture.Resources[1].IsExported = 1;
    fixture.Targets[0].Target = 0;
    fixture.Passes[0].Program = format::Technique::DepthOnly;
    fixture.Passes[0].Color = format::ABSENT;
    fixture.Passes[0].ColorLoad = format::Load::Discard;
    fixture.Passes[0].ColorStore = format::Store::Discard;
    fixture.Resources[0].IsExported = 0;
    CHECK(fixture.Initialize() == rhi::RasterStatus::InvalidDescription);
    fixture.Targets[0].Target = 1;
    REQUIRE(fixture.Initialize() == rhi::RasterStatus::Ready);
}

TEST_CASE("RG2 device capability rejection happens before shader or pipeline creation", "[render-graph]")
{
    GraphFixture fixture{false};
    CHECK(fixture.Initialize() == rhi::RasterStatus::Unsupported);
    CHECK(rhi::reference::Creates[static_cast<usize>(rhi::internal::RasterKind::Shader)] == 0);
    CHECK(rhi::reference::Creates[static_cast<usize>(rhi::internal::RasterKind::Pipeline)] == 0);
}

TEST_CASE("RG2 ready candidates never relabel prior exported pixels", "[render-graph]")
{
    GraphFixture fixture;
    REQUIRE(fixture.Initialize() == rhi::RasterStatus::Ready);
    renderer::RenderGraphReport report;
    REQUIRE(fixture.Update(report) == rhi::RasterStatus::Ready);
    rhi::SubmissionToken completion;
    REQUIRE(fixture.Graph.Render(fixture.Surface, 96, 64, completion) == rhi::RasterStatus::Ready);
    CHECK(fixture.Graph.GetOutputSceneRevision() == 0);
    rhi::reference::Completed = rhi::reference::Submitted;
    CHECK(fixture.Graph.GetOutputSceneRevision() == 1);
    fixture.Items[0].MeshToWorld.Translation.X = 100;
    REQUIRE(fixture.Update(report, renderer::RenderGraphCoveragePolicy::RejectIncomplete, 2) ==
            rhi::RasterStatus::Ready);
    CHECK(fixture.Graph.GetReport().SceneRevision == 2);
    CHECK(fixture.Graph.GetDrawableSceneRevision() == 2);
    CHECK(fixture.Graph.GetOutputSceneRevision() == 1);
    completion = {};
    REQUIRE(fixture.Graph.Render(fixture.Surface, 96, 64, completion) == rhi::RasterStatus::Ready);
    CHECK(fixture.Graph.GetOutputSceneRevision() == 0);
    rhi::reference::Completed = rhi::reference::Submitted;
    CHECK(fixture.Graph.GetOutputSceneRevision() == 2);
}

TEST_CASE("RG2 invalid surfaces report ownership failure without pretending preparation is pending", "[render-graph]")
{
    GraphFixture fixture;
    REQUIRE(fixture.Initialize() == rhi::RasterStatus::Ready);
    renderer::RenderGraphReport report;
    REQUIRE(fixture.Update(report) == rhi::RasterStatus::Ready);
    rhi::SubmissionToken completion;
    CHECK(fixture.Graph.Render({}, 96, 64, completion) == rhi::RasterStatus::InvalidHandle);
    CHECK(completion.IsNull() == true);
    CHECK(fixture.Graph.GetDrawableSceneRevision() == 1);
    REQUIRE(fixture.Graph.Render(fixture.Surface, 96, 64, completion) == rhi::RasterStatus::Ready);
}

TEST_CASE("RG2 offscreen encoding shares one caller-owned frame and tracks its actual producer", "[render-graph]")
{
    GraphFixture fixture;
    REQUIRE(fixture.Initialize() == rhi::RasterStatus::Ready);
    rhi::TextureHandle texture;
    rhi::TextureAspect aspect;
    REQUIRE(fixture.Graph.GetExportAllocation(0, texture, aspect) == rhi::RasterStatus::Ready);
    CHECK(fixture.Graph.GetExport(0, texture, aspect) == rhi::RasterStatus::NotReady);
    renderer::RenderGraphReport report;
    REQUIRE(fixture.Update(report) == rhi::RasterStatus::Ready);
    CHECK(fixture.Graph.Encode(fixture.Surface) == rhi::RasterStatus::InvalidState);
    REQUIRE(rhi::BeginFrame(fixture.Device, fixture.Surface) == rhi::DeviceStatus::Ready);
    REQUIRE(fixture.Graph.Encode(fixture.Surface) == rhi::RasterStatus::Ready);
    CHECK(fixture.Graph.HasUnconfirmedEncoding() == true);
    CHECK(fixture.Graph.Poll() == rhi::RasterStatus::InvalidState);
    CHECK(fixture.Graph.GetOutputSceneRevision() == 0);
    CHECK(fixture.Graph.Encode(fixture.Surface) == rhi::RasterStatus::InvalidState);
    rhi::SubmissionToken completion;
    REQUIRE(rhi::EndFrame(fixture.Device, completion) == rhi::RasterStatus::Ready);
    CHECK(fixture.Update(report) == rhi::RasterStatus::InvalidState);
    REQUIRE(fixture.Graph.ConfirmSubmission(completion) == rhi::RasterStatus::Pending);
    CHECK(fixture.Graph.HasUnconfirmedEncoding() == false);
    CHECK(fixture.Graph.GetOutputSceneRevision() == 0);
    rhi::reference::Completed = rhi::reference::Submitted;
    CHECK(fixture.Graph.GetOutputSceneRevision() == 1);
    REQUIRE(fixture.Graph.GetExport(0, texture, aspect) == rhi::RasterStatus::Ready);
}
TEST_CASE("RG2 untracked external submission invalidates output without replacing its drawable scene", "[render-graph]")
{
    GraphFixture fixture;
    REQUIRE(fixture.Initialize() == rhi::RasterStatus::Ready);
    renderer::RenderGraphReport report;
    REQUIRE(fixture.Update(report) == rhi::RasterStatus::Ready);
    REQUIRE(rhi::BeginFrame(fixture.Device, fixture.Surface) == rhi::DeviceStatus::Ready);
    REQUIRE(fixture.Graph.Encode(fixture.Surface) == rhi::RasterStatus::Ready);
    REQUIRE(rhi::EndFrame(fixture.Device) == rhi::DeviceStatus::Ready);
    CHECK(fixture.Graph.ConfirmSubmission({}) == rhi::RasterStatus::Failed);
    CHECK(fixture.Graph.GetOutputSceneRevision() == 0);
    CHECK(fixture.Graph.GetDrawableSceneRevision() == 1);
}
TEST_CASE("RG2 independent logical depths cannot silently share one private allocation", "[render-graph]")
{
    GraphFixture fixture;
    constexpr usize ALIASED_RESOURCE_COUNT = 3;
    StaticArray<format::Resource, ALIASED_RESOURCE_COUNT> resources{
        {fixture.Resources[0], fixture.Resources[1], fixture.Resources[1]}};
    resources[2].IsExported = 1;
    renderer::RenderGraphPlan plan = fixture.MakePlan();
    plan.Description.ResourceCount = ALIASED_RESOURCE_COUNT;
    plan.Resources = {resources.GetData(), resources.GetSize()};
    CHECK(fixture.Graph.Initialize(fixture.Device, plan, fixture.MakePrograms()) == rhi::RasterStatus::Unsupported);
    CHECK(rhi::reference::Creates[static_cast<usize>(rhi::internal::RasterKind::Shader)] == 0);
}
TEST_CASE("RG2 excluded items do not require their unneeded geometry uploads", "[render-graph]")
{
    GraphFixture fixture;
    REQUIRE(fixture.Initialize() == rhi::RasterStatus::Ready);
    fixture.Meshes[0].Geometry = {};
    fixture.Items[0].IsIntentionallyExcluded = true;
    renderer::RenderGraphReport report;
    REQUIRE(fixture.Update(report) == rhi::RasterStatus::Ready);
    CHECK(report.Excluded == 2);
    rhi::SubmissionToken completion;
    REQUIRE(fixture.Graph.Render(fixture.Surface, 96, 64, completion) == rhi::RasterStatus::Ready);
    CHECK(rhi::reference::Draws == 0);
}
