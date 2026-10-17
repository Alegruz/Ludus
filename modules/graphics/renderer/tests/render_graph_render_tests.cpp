#include "internal/readback.h"
#include "renderer_composite.h"
#include "rg2_depth.h"
#include "rg2_depth_display.h"
#include "rg2_offscreen.h"
#include "rg2_surface.h"

#include <ludus/foundation/base/compiler.h>

#include <ludus/foundation/containers/array.hpp>
#include <ludus/foundation/containers/borrowed_view.hpp>
#include <ludus/foundation/containers/static_array.hpp>
#include <ludus/foundation/filesystem/filesystem.hpp>
#include <ludus/graphics/renderer/render_graph.hpp>
#include <ludus/graphics/renderer/render_graph_activation.hpp>
#include <ludus/graphics/renderer/views.hpp>
#include <ludus/graphics/rhi/device.h>

#include <catch2/catch_test_macros.hpp>

#include <new>

using namespace ludus::foundation;
namespace renderer = ludus::graphics::renderer;
namespace rhi = ludus::graphics::rhi;
namespace packages = ludus::render_graph_packages;
namespace shaders = ludus::shaders;
namespace format = ludus::content::render_graph_format;
namespace
{
constexpr uint32 WIDTH = 96;
constexpr uint32 HEIGHT = 64;
constexpr usize PIXEL_BYTES = static_cast<usize>(WIDTH) * HEIGHT * 4;
constexpr usize VERTEX_COUNT = 3;
constexpr usize INDEX_COUNT = 3;
constexpr usize TABLE_COUNT = 1;
constexpr usize STREAM_COUNT = 1;
constexpr usize ATTRIBUTE_COUNT = 1;
struct RhiGuard final
{
    LUDUS_INLINE RhiGuard() noexcept
    {
        rhi::Shutdown();
    }
    LUDUS_INLINE ~RhiGuard() noexcept
    {
        rhi::Shutdown();
    }
};
struct GraphGpuFixture final
{
    RhiGuard Session;
    rhi::DeviceHandle Device{};
    rhi::SurfaceHandle Surface{};
    renderer::RenderGraph Graph;
    StaticArray<math::Vector3, VERTEX_COUNT> Positions{{{-.5F, -.5F, .5F}, {.5F, -.5F, .5F}, {0, .5F, .5F}}};
    StaticArray<uint32, INDEX_COUNT> Indices{{0, 1, 2}};
    StaticArray<renderer::RenderGraphMesh, TABLE_COUNT> Meshes{};
    StaticArray<renderer::RenderGraphItem, TABLE_COUNT> Items{};
    StaticArray<renderer::ViewDescription, TABLE_COUNT> Cameras{};
    StaticArray<uint8, PIXEL_BYTES> Pixels{};
    GraphGpuFixture()
    {
        rhi::DeviceDescription description;
        description.Required.PortableRaster = true;
        rhi::StartupInfo failure;
        const rhi::DeviceStatus started =
            rhi::CreateDevice({}, { .Width = WIDTH, .Height = HEIGHT }, description, Device, Surface, &failure);
        if (started != rhi::DeviceStatus::Ready && failure.Error == rhi::StartupError::AdapterUnavailable)
        {
            SKIP("No GPU adapter on this host");
        }
        REQUIRE(started == rhi::DeviceStatus::Ready);
        Meshes[0].Geometry = {Positions.GetData(), Positions.GetSize(), Indices.GetData(), Indices.GetSize()};
        Items[0].SourceId = 1;
        Items[0].Material.Color = {1, 0, 0, 1};
    }
    void Update(uint64 revision)
    {
        renderer::RenderGraphReport report;
        REQUIRE(Graph.Update({Meshes.GetData(), Meshes.GetSize()},
                             {Items.GetData(), Items.GetSize()},
                             {Cameras.GetData(), Cameras.GetSize()},
                             revision,
                             renderer::RenderGraphCoveragePolicy::RejectIncomplete,
                             report) == rhi::RasterStatus::Ready);
    }
    void Render()
    {
        rhi::SubmissionToken completion;
        REQUIRE(Graph.Render(Surface, WIDTH, HEIGHT, completion) == rhi::RasterStatus::Ready);
        REQUIRE(rhi::backend::ReadHeadlessPixels(Pixels.AsSpan()) == true);
    }
};
/// Read the build-tree runtime generation cooked by ludus_compile_render_graph(... RUNTIME_PACKAGE).
core::UniquePtr<renderer::RenderGraphPackage> LoadSurfacePackage()
{
    filesystem::Directory directory;
    REQUIRE(directory.Open(LUDUS_RG3_SURFACE_DIRECTORY).Succeeded() == true);
    filesystem::File file;
    REQUIRE(directory.OpenRead("rg2_surface.lurgrt", file).Succeeded() == true);
    Array<uint8> bytes;
    REQUIRE(bytes.TryResize(static_cast<usize>(file.Size())) == true);
    const filesystem::ReadResult read = file.ReadAt(0, bytes.AsSpan());
    REQUIRE(read.Outcome.Succeeded() == true);
    REQUIRE(read.BytesRead == bytes.GetSize());
    renderer::RenderGraphPackage* allocationOrNull = new (std::nothrow) renderer::RenderGraphPackage;
    core::UniquePtr<renderer::RenderGraphPackage> package{allocationOrNull};
    REQUIRE(package.Get() != nullptr);
    REQUIRE(package->Decode({bytes.GetData(), bytes.GetSize()}) == renderer::RenderGraphPackageStatus::Ready);
    return package;
}
/// Compare one runtime stage description with the compiled header's, target by target.
void CheckSameStage(const rhi::RasterShaderDescription& runtime, const rhi::RasterShaderDescription& compiled)
{
    CHECK(runtime.Artifact.Stage == compiled.Artifact.Stage);
    REQUIRE(runtime.Artifact.Spirv.size() == compiled.Artifact.Spirv.size());
    for (usize word = 0; word < compiled.Artifact.Spirv.size(); ++word)
    {
        REQUIRE(runtime.Artifact.Spirv[word] == compiled.Artifact.Spirv[word]);
    }
    CHECK(runtime.Artifact.SpirvEntry == compiled.Artifact.SpirvEntry);
    CHECK(runtime.Artifact.Msl == compiled.Artifact.Msl);
    CHECK(runtime.Artifact.MslEntry == compiled.Artifact.MslEntry);
    CHECK(runtime.Artifact.Wgsl == compiled.Artifact.Wgsl);
    CHECK(runtime.Artifact.WgslEntry == compiled.Artifact.WgslEntry);
    CHECK(runtime.Artifact.GlslEs == compiled.Artifact.GlslEs);
    CHECK(runtime.Artifact.GlslEsEntry == compiled.Artifact.GlslEsEntry);
    REQUIRE(runtime.Inputs.size() == compiled.Inputs.size());
    for (usize input = 0; input < compiled.Inputs.size(); ++input)
    {
        CHECK(runtime.Inputs[input].Location == compiled.Inputs[input].Location);
        CHECK(runtime.Inputs[input].Format == compiled.Inputs[input].Format);
    }
    CHECK(runtime.Bindings.size() == compiled.Bindings.size());
}
} // namespace
TEST_CASE("RG2 cooked graph renders extracted geometry and follows camera/instance changes", "[render-graph][gpu]")
{
    GraphGpuFixture fixture;
    REQUIRE(fixture.Graph.Initialize(fixture.Device,
                                     packages::rg2_surface::MakePlan(),
                                     packages::rg2_surface::MakePrograms(shaders::renderer_composite::Vertex(),
                                                                         shaders::renderer_composite::Fragment())) ==
            rhi::RasterStatus::Ready);
    fixture.Update(1);
    fixture.Render();
    const usize centerPixel = (usize{32} * WIDTH + 48) * 4;
    CHECK(fixture.Pixels[centerPixel] == 255);
    CHECK(fixture.Pixels[centerPixel + 1] == 0);
    fixture.Items[0].MeshToWorld.Translation.X = 1;
    fixture.Update(2);
    fixture.Render();
    CHECK(fixture.Pixels[centerPixel] == 0);
    fixture.Cameras[0].WorldToClip.Columns[3].X = -1;
    fixture.Update(3);
    fixture.Render();
    CHECK(fixture.Pixels[centerPixel] == 255);
    CHECK(fixture.Graph.GetDrawableSceneRevision() == 3);
}
TEST_CASE("RG2 exported offscreen color matches the same direct raster graph", "[render-graph][gpu]")
{
    GraphGpuFixture fixture;
    fixture.Items[0].Material.Color = {.25F, .5F, .75F, 1};
    REQUIRE(fixture.Graph.Initialize(fixture.Device,
                                     packages::rg2_surface::MakePlan(),
                                     packages::rg2_surface::MakePrograms(shaders::renderer_composite::Vertex(),
                                                                         shaders::renderer_composite::Fragment())) ==
            rhi::RasterStatus::Ready);
    fixture.Update(1);
    fixture.Render();
    const StaticArray<uint8, PIXEL_BYTES> direct = fixture.Pixels;
    fixture.Graph.Reset();
    REQUIRE(fixture.Graph.Initialize(fixture.Device,
                                     packages::rg2_offscreen::MakePlan(),
                                     packages::rg2_offscreen::MakePrograms(shaders::renderer_composite::Vertex(),
                                                                           shaders::renderer_composite::Fragment())) ==
            rhi::RasterStatus::Ready);
    fixture.Update(2);
    rhi::TextureHandle texture;
    rhi::TextureAspect aspect;
    REQUIRE(fixture.Graph.GetExportAllocation(0, texture, aspect) == rhi::RasterStatus::Ready);
    REQUIRE(aspect == rhi::TextureAspect::Color);
    renderer::Renderer compositor;
    REQUIRE(compositor.Initialize(fixture.Device,
                                  packages::rg2_offscreen::MakePrograms(shaders::renderer_composite::Vertex(),
                                                                        shaders::renderer_composite::Fragment())
                                      .ColorVertex,
                                  packages::rg2_offscreen::MakePrograms(shaders::renderer_composite::Vertex(),
                                                                        shaders::renderer_composite::Fragment())
                                      .ColorFragment) == rhi::RasterStatus::Ready);
    REQUIRE(compositor.InitializeViews(shaders::renderer_composite::Vertex(),
                                       shaders::renderer_composite::Fragment()) == rhi::RasterStatus::Ready);
    renderer::ViewMapping mapping;
    REQUIRE(renderer::ResolveViewMapping({WIDTH, HEIGHT, renderer::ScreenFit::Stretch},
                                         {0, 0, WIDTH, HEIGHT},
                                         WIDTH,
                                         HEIGHT,
                                         mapping) == rhi::RasterStatus::Ready);
    renderer::Presentation presentation;
    REQUIRE(compositor.PreparePresentation(texture, mapping, presentation) == rhi::RasterStatus::Ready);
    REQUIRE(rhi::BeginFrame(fixture.Device, fixture.Surface) == rhi::DeviceStatus::Ready);
    REQUIRE(fixture.Graph.Encode(fixture.Surface) == rhi::RasterStatus::Ready);
    REQUIRE(compositor.DrawPresentation(presentation) == rhi::RasterStatus::Ready);
    rhi::SubmissionToken completion;
    REQUIRE(rhi::EndFrame(fixture.Device, completion) == rhi::RasterStatus::Ready);
    const rhi::RasterStatus confirmed = fixture.Graph.ConfirmSubmission(completion);
    REQUIRE((confirmed == rhi::RasterStatus::Ready || confirmed == rhi::RasterStatus::Pending));
    REQUIRE(rhi::backend::ReadHeadlessPixels(fixture.Pixels.AsSpan()) == true);
    for (usize i = 0; i < PIXEL_BYTES; ++i)
    {
        REQUIRE(fixture.Pixels[i] == direct[i]);
    }
}
TEST_CASE("RG2 depth-only export samples actual opaque geometry depth", "[render-graph][gpu]")
{
    GraphGpuFixture fixture;
    REQUIRE(fixture.Graph.Initialize(fixture.Device,
                                     packages::rg2_depth::MakePlan(),
                                     packages::rg2_depth::MakePrograms(shaders::renderer_composite::Vertex(),
                                                                       shaders::renderer_composite::Fragment())) ==
            rhi::RasterStatus::Ready);
    fixture.Update(1);
    fixture.Render();
    rhi::TextureHandle texture;
    rhi::TextureAspect aspect;
    REQUIRE(fixture.Graph.GetExport(0, texture, aspect) == rhi::RasterStatus::Ready);
    REQUIRE(aspect == rhi::TextureAspect::Depth);
    rhi::TextureViewHandle depth;
    REQUIRE(rhi::CreateTextureView(fixture.Device, texture, { .Aspect = aspect }, depth) == rhi::RasterStatus::Ready);
    const rhi::RasterShaderDescription vertex = shaders::rg2_depth_display::Vertex();
    const rhi::RasterShaderDescription fragment = shaders::rg2_depth_display::Fragment();
    rhi::RasterShaderHandle vertexHandle;
    rhi::RasterShaderHandle fragmentHandle;
    REQUIRE(rhi::CreateRasterShader(fixture.Device, vertex, vertexHandle) == rhi::RasterStatus::Ready);
    REQUIRE(rhi::CreateRasterShader(fixture.Device, fragment, fragmentHandle) == rhi::RasterStatus::Ready);
    rhi::BindingLayoutHandle layout;
    REQUIRE(rhi::CreateBindingLayout(fixture.Device, fragment.Bindings, layout) == rhi::RasterStatus::Ready);
    const rhi::RasterBindingResource resource{ .Texture = depth };
    rhi::BindingSetHandle set;
    REQUIRE(rhi::CreateBindingSet(fixture.Device, layout, {&resource, 1}, set) == rhi::RasterStatus::Ready);
    const StaticArray<rhi::RasterVertexStream, STREAM_COUNT> streams{{{sizeof(math::Vector4), false}}};
    const StaticArray<rhi::RasterVertexAttribute, ATTRIBUTE_COUNT> attributes{
        {{0, 0, 0, rhi::RasterVertexFormat::Float4}}};
    rhi::RasterPipelineDescription description;
    description.Vertex = vertexHandle;
    description.Fragment = fragmentHandle;
    description.Layout = layout;
    description.Streams = streams.AsSpan();
    description.Attributes = attributes.AsSpan();
    rhi::RasterPipelineHandle pipeline;
    REQUIRE(rhi::CreateRasterPipeline(fixture.Device, description, pipeline) == rhi::RasterStatus::Ready);
    const StaticArray<math::Vector4, VERTEX_COUNT> positions{{{-1, -1, 0, 1}, {3, -1, 0, 1}, {-1, 3, 0, 1}}};
    rhi::BufferHandle vertices;
    rhi::BufferHandle indices;
    REQUIRE(rhi::CreateBuffer(fixture.Device,
                              {rhi::BufferRole::Vertex, sizeof(positions)},
                              {reinterpret_cast<const uint8*>(positions.GetData()), sizeof(positions)},
                              vertices) == rhi::RasterStatus::Ready);
    REQUIRE(rhi::CreateBuffer(fixture.Device,
                              {rhi::BufferRole::Index32, sizeof(fixture.Indices)},
                              {reinterpret_cast<const uint8*>(fixture.Indices.GetData()), sizeof(fixture.Indices)},
                              indices) == rhi::RasterStatus::Ready);
    const rhi::RasterVertexSlice slice{vertices, 0, sizeof(positions)};
    rhi::RasterDraw drawing;
    drawing.Pipeline = pipeline;
    drawing.Bindings = set;
    drawing.Vertices = {&slice, 1};
    drawing.Indices = indices;
    drawing.IndexCount = INDEX_COUNT;
    drawing.VertexCount = VERTEX_COUNT;
    const auto displayDepth = [&]() {
        REQUIRE(rhi::BeginFrame(fixture.Device, fixture.Surface) == rhi::DeviceStatus::Ready);
        REQUIRE(rhi::BeginRasterPass(fixture.Device, {}) == rhi::RasterStatus::Ready);
        REQUIRE(rhi::DrawIndexed(fixture.Device, drawing) == rhi::RasterStatus::Ready);
        rhi::SubmissionToken completion;
        REQUIRE(rhi::EndFrame(fixture.Device, completion) == rhi::RasterStatus::Ready);
        REQUIRE(rhi::backend::ReadHeadlessPixels(fixture.Pixels.AsSpan()) == true);
    };
    displayDepth();
    const usize centerPixel = (usize{32} * WIDTH + 48) * 4;
    CHECK(fixture.Pixels[centerPixel] >= 127);
    CHECK(fixture.Pixels[centerPixel] <= 128);
    CHECK(fixture.Pixels[0] == 255);
    fixture.Items[0].MeshToWorld.Translation.X = 1;
    fixture.Update(2);
    fixture.Render();
    displayDepth();
    CHECK(fixture.Pixels[centerPixel] == 255);
    fixture.Cameras[0].WorldToClip.Columns[3].X = -1;
    fixture.Update(3);
    fixture.Render();
    displayDepth();
    CHECK(fixture.Pixels[centerPixel] >= 127);
    CHECK(fixture.Pixels[centerPixel] <= 128);
}
TEST_CASE("RG3 cooked runtime generation decodes the compiled header's plan and stage artifacts",
          "[render-graph][activation]")
{
    const core::UniquePtr<renderer::RenderGraphPackage> package = LoadSurfacePackage();
    const BorrowedView<const uint8> identity = package->GetInterfaceIdentity();
    REQUIRE(identity.GetSize() == packages::rg2_surface::INTERFACE_IDENTITY.GetSize());
    for (usize i = 0; i < identity.GetSize(); ++i)
    {
        CHECK(identity[i] == packages::rg2_surface::INTERFACE_IDENTITY[i]);
    }
    const renderer::RenderGraphPlan runtimePlan = package->GetPlan();
    const renderer::RenderGraphPlan compiledPlan = packages::rg2_surface::MakePlan();
    CHECK(runtimePlan.Description.Version == compiledPlan.Description.Version);
    CHECK(runtimePlan.Description.CameraCount == compiledPlan.Description.CameraCount);
    REQUIRE(runtimePlan.Resources.GetSize() == compiledPlan.Resources.GetSize());
    for (usize i = 0; i < compiledPlan.Resources.GetSize(); ++i)
    {
        const format::Resource& runtime = runtimePlan.Resources[i];
        const format::Resource& compiled = compiledPlan.Resources[i];
        CHECK(runtime.Width == compiled.Width);
        CHECK(runtime.Height == compiled.Height);
        CHECK(runtime.Kind == compiled.Kind);
        CHECK(runtime.Backing == compiled.Backing);
        CHECK(runtime.IsSurface == compiled.IsSurface);
        CHECK(runtime.IsExported == compiled.IsExported);
    }
    REQUIRE(runtimePlan.Cameras.GetSize() == compiledPlan.Cameras.GetSize());
    for (usize i = 0; i < compiledPlan.Cameras.GetSize(); ++i)
    {
        CHECK(runtimePlan.Cameras[i].Target == compiledPlan.Cameras[i].Target);
    }
    REQUIRE(runtimePlan.Passes.GetSize() == compiledPlan.Passes.GetSize());
    for (usize i = 0; i < compiledPlan.Passes.GetSize(); ++i)
    {
        const format::Pass& runtime = runtimePlan.Passes[i];
        const format::Pass& compiled = compiledPlan.Passes[i];
        CHECK(runtime.Program == compiled.Program);
        CHECK(runtime.Camera == compiled.Camera);
        CHECK(runtime.Color == compiled.Color);
        CHECK(runtime.Depth == compiled.Depth);
        CHECK(runtime.ColorLoad == compiled.ColorLoad);
        CHECK(runtime.ColorStore == compiled.ColorStore);
        CHECK(runtime.DepthLoad == compiled.DepthLoad);
        CHECK(runtime.DepthStore == compiled.DepthStore);
        CHECK(runtime.AllowsRepeatedCoverage == compiled.AllowsRepeatedCoverage);
    }
    const renderer::RenderGraphPrograms runtime = package->GetPrograms({}, {});
    const renderer::RenderGraphPrograms compiled = packages::rg2_surface::MakePrograms({}, {});
    CheckSameStage(runtime.ColorVertex, compiled.ColorVertex);
    CheckSameStage(runtime.ColorFragment, compiled.ColorFragment);
    CheckSameStage(runtime.DepthVertex, compiled.DepthVertex);
    CheckSameStage(runtime.DepthFragment, compiled.DepthFragment);
    // The fixture is cooked for this host's native target and WGSL; neither may be silently absent.
#if defined(LUDUS_TEST_METAL)
    CHECK(runtime.ColorVertex.Artifact.Msl.empty() == false);
    CHECK(runtime.ColorFragment.Artifact.Msl.empty() == false);
#else
    CHECK(runtime.ColorVertex.Artifact.Spirv.empty() == false);
    CHECK(runtime.ColorFragment.Artifact.Spirv.empty() == false);
#endif
    CHECK(runtime.ColorVertex.Artifact.Wgsl.empty() == false);
    CHECK(runtime.ColorFragment.Artifact.Wgsl.empty() == false);
}
TEST_CASE("RG3 runtime generation renders the same pixels as its compiled header across activation",
          "[render-graph][gpu][activation]")
{
    GraphGpuFixture fixture;
    fixture.Items[0].Material.Color = {.25F, .5F, .75F, 1};
    REQUIRE(fixture.Graph.Initialize(fixture.Device,
                                     packages::rg2_surface::MakePlan(),
                                     packages::rg2_surface::MakePrograms(shaders::renderer_composite::Vertex(),
                                                                         shaders::renderer_composite::Fragment())) ==
            rhi::RasterStatus::Ready);
    fixture.Update(1);
    fixture.Render();
    const StaticArray<uint8, PIXEL_BYTES> compiled = fixture.Pixels;
    fixture.Graph.Reset();
    renderer::RenderGraphActivation activation;
    REQUIRE(activation.Initialize(fixture.Device,
                                  {packages::rg2_surface::INTERFACE_IDENTITY.GetData(),
                                   packages::rg2_surface::INTERFACE_IDENTITY.GetSize()},
                                  shaders::renderer_composite::Vertex(),
                                  shaders::renderer_composite::Fragment()) == rhi::RasterStatus::Ready);
    const rhi::RasterStatus staged = activation.Stage(LoadSurfacePackage());
    REQUIRE((staged == rhi::RasterStatus::Ready || staged == rhi::RasterStatus::Pending));
    rhi::RasterStatus polled = activation.Poll();
    for (usize attempt = 0; polled == rhi::RasterStatus::Pending && attempt < 1000; ++attempt)
    {
        polled = activation.Poll();
    }
    REQUIRE(polled == rhi::RasterStatus::Ready);
    REQUIRE(activation.GetReport().ActiveGeneration == 1);
    // Stage a second decoded generation, then activate it at a frame boundary.
    renderer::RenderGraphReport report;
    REQUIRE(activation.Update({fixture.Meshes.GetData(), fixture.Meshes.GetSize()},
                              {fixture.Items.GetData(), fixture.Items.GetSize()},
                              {fixture.Cameras.GetData(), fixture.Cameras.GetSize()},
                              1,
                              renderer::RenderGraphCoveragePolicy::RejectIncomplete,
                              report) == rhi::RasterStatus::Ready);
    const rhi::RasterStatus restaged = activation.Stage(LoadSurfacePackage());
    REQUIRE((restaged == rhi::RasterStatus::Ready || restaged == rhi::RasterStatus::Pending));
    for (usize attempt = 0; activation.GetReport().ActiveGeneration != 2 && attempt < 1000; ++attempt)
    {
        REQUIRE(activation.Poll() == rhi::RasterStatus::Ready);
        if (activation.GetReport().Candidate == renderer::RenderGraphCandidateState::NeedsUpdate)
        {
            REQUIRE(activation.Update({fixture.Meshes.GetData(), fixture.Meshes.GetSize()},
                                      {fixture.Items.GetData(), fixture.Items.GetSize()},
                                      {fixture.Cameras.GetData(), fixture.Cameras.GetSize()},
                                      1,
                                      renderer::RenderGraphCoveragePolicy::RejectIncomplete,
                                      report) == rhi::RasterStatus::Ready);
        }
    }
    REQUIRE(activation.GetReport().ActiveGeneration == 2);
    renderer::RenderGraph* activeOrNull = activation.GetActiveOrNull();
    REQUIRE(activeOrNull != nullptr);
    rhi::SubmissionToken completion;
    REQUIRE(activeOrNull->Render(fixture.Surface, WIDTH, HEIGHT, completion) == rhi::RasterStatus::Ready);
    REQUIRE(rhi::backend::ReadHeadlessPixels(fixture.Pixels.AsSpan()) == true);
    for (usize i = 0; i < PIXEL_BYTES; ++i)
    {
        REQUIRE(fixture.Pixels[i] == compiled[i]);
    }
    activation.Reset();
}
