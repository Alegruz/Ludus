#include "internal/lifecycle.h"
#include "reference_raster.h"

#include <ludus/graphics/rhi/graph.h>
#include <ludus/graphics/rhi/measurement.h>

#include <catch2/catch_test_macros.hpp>

namespace ludus::graphics::rhi::backend
{
extern foundation::uint32 PendingToken;
extern StartupError ImmediateError;
extern FrameStatus NextFrame;
extern Backend ActiveKind;
extern bool SelectionSupported;
} // namespace ludus::graphics::rhi::backend
using namespace ludus::foundation;
using namespace ludus::graphics::rhi;
namespace
{
constexpr usize UNIFORM_BYTES = 512;
constexpr usize TEXTURE_COUNT = 4;
struct BindingSession final
{
    DeviceHandle Device;
    SurfaceHandle Surface;
    explicit BindingSession(uint32 descriptorLimit = 512, Backend backendKind = Backend::Metal)
    {
        Shutdown();
        reference::Reset();
        backend::ImmediateError = StartupError::None;
        backend::NextFrame = FrameStatus::Ready;
        backend::ActiveKind = backendKind;
        backend::SelectionSupported = true;
        DeviceDescription description
        {
            .Limits = {},
            .Required = { .PortableRaster = true },
            .Preferred = { .BindlessTextureIndexing = true },
            .Storage = { .BindingDescriptors = descriptorLimit },
            .RequiredProfile = {},
            .PreferredProfile = {},
        };
        REQUIRE(CreateDevice({}, {}, description, Device, Surface) == DeviceStatus::Pending);
        internal::Complete(backend::PendingToken, StartupError::None, {4096, 16384});
    }
    ~BindingSession() noexcept
    {
        Shutdown();
    }
};
struct BindingScene final
{
    BufferHandle Uniform;
    BufferHandle Vertices;
    BufferHandle Indices;
    RasterShaderHandle Vertex;
    RasterShaderHandle Fragment;
    BindingLayoutHandle Layout;
    BindingSetHandle Set;
    RasterPipelineHandle Pipeline;
    RasterVertexSlice Slice;
    explicit BindingScene(DeviceHandle device)
    {
        const uint8 bytes[UNIFORM_BYTES]{};
        REQUIRE(CreateBuffer(device, { .Role = BufferRole::Uniform, .Size = sizeof(bytes) }, bytes, Uniform) ==
                RasterStatus::Ready);
        const RasterBinding binding
        {
            .Kind = RasterBindingKind::UniformBuffer,
            .Visibility = RasterVisibility::Both,
            .MinSize = 16,
            .HasDynamicOffset = true,
        };
        const RasterShaderInput input{ .Format = RasterVertexFormat::Float3 };
        RasterShaderDescription vertex
        {
            .Artifact =
            {
                .Stage = ShaderStage::Vertex,
                .Spirv = {},
                .Wgsl = "fixture",
                .SpirvEntry = {},
                .WgslEntry = "VertexMain",
                .Msl = "fixture",
                .MslEntry = "VertexMain",
            },
            .Inputs = {&input, 1},
            .Bindings = {&binding, 1},
        };
        REQUIRE(CreateRasterShader(device, vertex, Vertex) == RasterStatus::Ready);
        const RasterShaderDescription fragment
        {
            .Artifact =
            {
                .Stage = ShaderStage::Fragment,
                .Spirv = {},
                .Wgsl = "fixture",
                .SpirvEntry = {},
                .WgslEntry = "FragmentMain",
                .Msl = "fixture",
                .MslEntry = "FragmentMain",
            },
            .Inputs = {},
            .Bindings = {&binding, 1},
        };
        REQUIRE(CreateRasterShader(device, fragment, Fragment) == RasterStatus::Ready);
        REQUIRE(CreateBindingLayout(device, {&binding, 1}, Layout) == RasterStatus::Ready);
        const RasterBindingResource resource{ .Buffer = Uniform, .Size = 16 };
        REQUIRE(CreateBindingSet(device, Layout, {&resource, 1}, Set) == RasterStatus::Ready);
        const RasterVertexStream stream{ .Stride = 12 };
        const RasterVertexAttribute attribute{ .Format = RasterVertexFormat::Float3 };
        REQUIRE(CreateRasterPipeline(device,
                                     {
                                         .Vertex = Vertex,
                                         .Fragment = Fragment,
                                         .Layout = Layout,
                                         .Streams = {&stream, 1},
                                         .Attributes = {&attribute, 1},
                                     },
                                     Pipeline) == RasterStatus::Ready);
        const uint8 vertices[36]{};
        const uint16 indices[3]{0, 1, 2};
        REQUIRE(CreateBuffer(device, { .Role = BufferRole::Vertex, .Size = sizeof(vertices) }, vertices, Vertices) ==
                RasterStatus::Ready);
        REQUIRE(CreateBuffer(device,
                             { .Role = BufferRole::Index16, .Size = sizeof(indices) },
                             {reinterpret_cast<const uint8*>(indices), sizeof(indices)},
                             Indices) == RasterStatus::Ready);
        Slice = { .Buffer = Vertices, .Size = sizeof(vertices) };
    }
    [[nodiscard]] RasterDraw GetDraw() const noexcept
    {
        return
        {
            .Pipeline = Pipeline,
            .Bindings = Set,
            .Vertices = {&Slice, 1},
            .Indices = Indices,
            .IndexCount = 3,
            .VertexCount = 3,
        };
    }
};
struct TextureTable final
{
    TextureHandle Textures[TEXTURE_COUNT];
    TextureViewHandle Views[TEXTURE_COUNT];
    RasterBindingResource Entries[TEXTURE_COUNT];
    BindingLayoutHandle Layout;
    BindingSetHandle Set;
    explicit TextureTable(DeviceHandle device)
    {
        const RasterBinding binding
        {
            .Binding = 1,
            .Kind = RasterBindingKind::Texture2D,
            .Visibility = RasterVisibility::Fragment,
            .Count = static_cast<uint32>(TEXTURE_COUNT),
            .Indexing = RasterBindingIndexing::NonUniform,
        };
        REQUIRE(CreateBindingLayout(device, {&binding, 1}, Layout) == RasterStatus::Ready);
        for (usize element = 0; element < TEXTURE_COUNT; ++element)
        {
            const uint8 pixel[4]{255, 0, 0, 255};
            REQUIRE(CreateTexture(device,
                                  {
                                      .Width = 1,
                                      .Height = 1,
                                  },
                                  {
                                      .Bytes = pixel,
                                      .RowPitch = 4,
                                  },
                                  Textures[element]) == RasterStatus::Ready);
            REQUIRE(CreateTextureView(device, Textures[element], Views[element]) == RasterStatus::Ready);
            Entries[element] =
            {
                .Binding = 1,
                .Texture = Views[element],
                .ArrayElement = static_cast<uint32>(element),
            };
        }
        REQUIRE(CreateBindingSet(device, Layout, Entries, Set) == RasterStatus::Ready);
    }
};
} // namespace

TEST_CASE("R11 dynamic offsets reject alignment range and undeclared bindings before draws", "[rhi][binding]")
{
    BindingSession session;
    BindingScene scene(session.Device);
    RasterDraw draw = scene.GetDraw();
    REQUIRE(BeginFrame(session.Device, session.Surface) == DeviceStatus::Ready);
    draw.BindingOffsets.Bytes[0] = 1;
    CHECK(DrawIndexed(session.Device, draw) == RasterStatus::InvalidDescription);
    draw.BindingOffsets.Bytes[0] = static_cast<uint32>(UNIFORM_BYTES);
    CHECK(DrawIndexed(session.Device, draw) == RasterStatus::InvalidDescription);
    draw.BindingOffsets.Bytes[0] = 256;
    draw.BindingOffsets.Bytes[7] = 256;
    CHECK(DrawIndexed(session.Device, draw) == RasterStatus::InvalidDescription);
    CHECK(reference::Draws == 0);
    draw.BindingOffsets.Bytes[7] = 0;
    REQUIRE(DrawIndexed(session.Device, draw) == RasterStatus::Ready);
    CHECK(reference::Packet.DynamicOffsetCount == 1);
    CHECK(reference::Packet.DynamicOffsets[0] == 256);
    REQUIRE(EndFrame(session.Device) == DeviceStatus::Ready);
}

TEST_CASE("R11 graph declarations cover the effective dynamic range", "[rhi][binding][graph]")
{
    BindingSession session;
    BindingScene scene(session.Device);
    for (bool hasCorrectRange : {false, true})
    {
        OrderedGraph graph;
        GraphVersion vertices;
        GraphVersion indices;
        GraphVersion uniform;
        REQUIRE(CreateOrderedGraph(session.Device, graph) == RasterStatus::Ready);
        REQUIRE(ImportGraphBuffer(session.Device, graph, scene.Vertices, vertices) == RasterStatus::Ready);
        REQUIRE(ImportGraphBuffer(session.Device, graph, scene.Indices, indices) == RasterStatus::Ready);
        REQUIRE(ImportGraphBuffer(session.Device, graph, scene.Uniform, uniform) == RasterStatus::Ready);
        const GraphUse uses[]{
            { .Resource = vertices, .Access = GraphAccessMode::Vertex, .Size = 36 },
            { .Resource = indices, .Access = GraphAccessMode::Index, .Size = 6 },
            {
                .Resource = uniform,
                .Access = GraphAccessMode::Uniform,
                .Offset = hasCorrectRange == true ? 256U : 0U,
                .Size = 16,
            },
        };
        RasterDraw draw = scene.GetDraw();
        draw.BindingOffsets.Bytes[0] = 256;
        const GraphPassDescription pass
        {
            .Name = "dynamic",
            .Attachment = {},
            .Uses = uses,
            .UseCount = 3,
            .Draws = &draw,
            .DrawCount = 1,
        };
        REQUIRE(AddGraphPass(session.Device, graph, pass) == RasterStatus::Ready);
        GraphReport report;
        CHECK(CompileOrderedGraph(session.Device, graph, report) ==
              (hasCorrectRange == true ? RasterStatus::Ready : RasterStatus::InvalidDescription));
        REQUIRE(DiscardOrderedGraph(session.Device, graph) == RasterStatus::Ready);
    }
}

TEST_CASE("R11 descriptor exhaustion survives failed creation and in-flight replacement", "[rhi][binding][lifetime]")
{
    BindingSession session(1);
    BindingScene scene(session.Device);
    const RasterBindingResource resource{ .Buffer = scene.Uniform, .Size = 16 };
    BindingSetHandle replacement;
    CHECK(CreateBindingSet(session.Device, scene.Layout, {&resource, 1}, replacement) ==
          RasterStatus::CapacityExceeded);
    REQUIRE(BeginFrame(session.Device, session.Surface) == DeviceStatus::Ready);
    REQUIRE(DrawIndexed(session.Device, scene.GetDraw()) == RasterStatus::Ready);
    REQUIRE(EndFrame(session.Device) == DeviceStatus::Ready);
    const BindingSetHandle stale = scene.Set;
    REQUIRE(Destroy(session.Device, scene.Set) == RasterStatus::Ready);
    AllocationTelemetry telemetry;
    REQUIRE(GetAllocationTelemetry(session.Device, telemetry) == RasterStatus::Ready);
    CHECK(telemetry.BindingDescriptors == 1);
    CHECK(telemetry.RetiringBindingDescriptors == 1);
    CHECK(CreateBindingSet(session.Device, scene.Layout, {&resource, 1}, replacement) ==
          RasterStatus::CapacityExceeded);
    reference::Completed = reference::Submitted;
    REQUIRE(PollLifetime(session.Device) == RasterStatus::Ready);
    reference::Next = RasterStatus::OutOfMemory;
    CHECK(CreateBindingSet(session.Device, scene.Layout, {&resource, 1}, replacement) == RasterStatus::OutOfMemory);
    reference::Next = RasterStatus::Ready;
    REQUIRE(CreateBindingSet(session.Device, scene.Layout, {&resource, 1}, replacement) == RasterStatus::Ready);
    CHECK(GetStatus(session.Device, stale) == RasterStatus::InvalidHandle);
}

TEST_CASE("R11 complete texture tables retain every element and reject holes and stale indices", "[rhi][binding]")
{
    BindingSession session;
    TextureTable table(session.Device);
    RasterBindingIndex index;
    REQUIRE(GetBindingIndex(session.Device, table.Set, 1, 3, index) == RasterStatus::Ready);
    uint32 element = 99;
    REQUIRE(GetBindingArrayElement(session.Device, table.Set, index, element) == RasterStatus::Ready);
    CHECK(element == 3);
    CHECK(GetBindingIndex(session.Device, table.Set, 1, 4, index) == RasterStatus::InvalidDescription);
    BindingSetHandle replacement;
    CHECK(CreateBindingSet(session.Device, table.Layout, {table.Entries, 3}, replacement) ==
          RasterStatus::InvalidDescription);
    table.Entries[3].ArrayElement = 2;
    CHECK(CreateBindingSet(session.Device, table.Layout, table.Entries, replacement) ==
          RasterStatus::InvalidDescription);
    table.Entries[3].ArrayElement = 3;
    REQUIRE(CreateBindingSet(session.Device, table.Layout, table.Entries, replacement) == RasterStatus::Ready);
    element = 99;
    CHECK(GetBindingArrayElement(session.Device, replacement, index, element) == RasterStatus::InvalidHandle);
    CHECK(element == 99);
    for (usize item = 0; item < TEXTURE_COUNT; ++item)
    {
        REQUIRE(Destroy(session.Device, table.Views[item]) == RasterStatus::Ready);
        REQUIRE(Destroy(session.Device, table.Textures[item]) == RasterStatus::Ready);
    }
    CHECK(reference::Destroys[static_cast<usize>(internal::RasterKind::Texture)] == 0);
    const BindingSetHandle stale = table.Set;
    REQUIRE(Destroy(session.Device, table.Set) == RasterStatus::Ready);
    CHECK(GetBindingArrayElement(session.Device, stale, index, element) == RasterStatus::InvalidHandle);
    REQUIRE(Destroy(session.Device, replacement) == RasterStatus::Ready);
    CHECK(reference::Destroys[static_cast<usize>(internal::RasterKind::Texture)] == TEXTURE_COUNT);
}

TEST_CASE("R11 native indexing requires explicit admission and browser arrays reject", "[rhi][binding]")
{
    BindingSession session(512, Backend::WebGPU);
    const RasterBinding array
    {
        .Binding = 1,
        .Kind = RasterBindingKind::Texture2D,
        .Visibility = RasterVisibility::Fragment,
        .Count = 4,
    };
    BindingLayoutHandle layout;
    CHECK(CreateBindingLayout(session.Device, {&array, 1}, layout) == RasterStatus::Unsupported);
    DeviceInfo info;
    REQUIRE(GetDeviceInfo(session.Device, info) == DeviceStatus::Ready);
    CHECK(info.Profile.Binding.HasBindlessTextureIndexing == false);
    CHECK(info.UnmetPreferences.BindlessTextureIndexing == true);
    CHECK(info.Profile.Binding.MaxTextureArrayElements == 1);
}

TEST_CASE("R11 descriptor payloads exceed 64 without changing configured limits", "[rhi][binding][storage]")
{
    BindingSession session;
    constexpr usize DESCRIPTORS = 72;
    const RasterBinding array
    {
        .Kind = RasterBindingKind::Texture2D,
        .Visibility = RasterVisibility::Fragment,
        .Count = static_cast<uint32>(DESCRIPTORS),
    };
    BindingLayoutHandle layout;
    REQUIRE(CreateBindingLayout(session.Device, {&array, 1}, layout) == RasterStatus::Ready);
    const uint8 pixel[4]{255, 0, 0, 255};
    TextureHandle texture;
    TextureViewHandle view;
    REQUIRE(CreateTexture(session.Device, { .Width = 1, .Height = 1 }, { .Bytes = pixel, .RowPitch = 4 }, texture) ==
            RasterStatus::Ready);
    REQUIRE(CreateTextureView(session.Device, texture, view) == RasterStatus::Ready);
    RasterBindingResource entries[DESCRIPTORS]{};
    for (usize element = 0; element < DESCRIPTORS; ++element)
    {
        entries[element] = { .Texture = view, .ArrayElement = static_cast<uint32>(element) };
    }
    BindingSetHandle set;
    REQUIRE(CreateBindingSet(session.Device, layout, entries, set) == RasterStatus::Ready);
    AllocationTelemetry telemetry;
    REQUIRE(GetAllocationTelemetry(session.Device, telemetry) == RasterStatus::Ready);
    CHECK(telemetry.BindingDescriptors == DESCRIPTORS);
    REQUIRE(Destroy(session.Device, view) == RasterStatus::Ready);
    REQUIRE(Destroy(session.Device, texture) == RasterStatus::Ready);
    CHECK(reference::Destroys[static_cast<usize>(internal::RasterKind::Texture)] == 0);
    REQUIRE(Destroy(session.Device, set) == RasterStatus::Ready);
    CHECK(reference::Destroys[static_cast<usize>(internal::RasterKind::Texture)] == 1);
}

TEST_CASE("R11 GDI rejects a missing reachable array texture before native execution", "[rhi][binding][graph]")
{
    BindingSession session;
    BindingScene scene(session.Device);
    TextureTable table(session.Device);
    const RasterShaderInput input{ .Format = RasterVertexFormat::Float3 };
    const RasterBinding binding
    {
        .Binding = 1,
        .Kind = RasterBindingKind::Texture2D,
        .Visibility = RasterVisibility::Fragment,
        .Count = static_cast<uint32>(TEXTURE_COUNT),
        .Indexing = RasterBindingIndexing::NonUniform,
    };
    const ShaderDescription vertexArtifact
    {
        .Stage = ShaderStage::Vertex,
        .Spirv = {},
        .Wgsl = "reference-only array vertex",
        .SpirvEntry = {},
        .WgslEntry = "VertexMain",
        .Msl = "reference-only array vertex",
        .MslEntry = "VertexMain",
    };
    const ShaderDescription fragmentArtifact
    {
        .Stage = ShaderStage::Fragment,
        .Spirv = {},
        .Wgsl = "reference-only bounded array load",
        .SpirvEntry = {},
        .WgslEntry = "FragmentMain",
        .Msl = "reference-only bounded array load",
        .MslEntry = "FragmentMain",
    };
    RasterShaderHandle vertex;
    RasterShaderHandle fragment;
    REQUIRE(CreateRasterShader(session.Device,
                               { .Artifact = vertexArtifact, .Inputs = {&input, 1}, .Bindings = {} },
                               vertex) == RasterStatus::Ready);
    CHECK(CreateRasterShader(session.Device,
                             { .Artifact = fragmentArtifact, .Inputs = {}, .Bindings = {&binding, 1} },
                             fragment) == RasterStatus::InvalidDescription);
    REQUIRE(CreateRasterShader(session.Device,
                               {
                                   .Artifact = fragmentArtifact,
                                   .Inputs = {},
                                   .Bindings = {&binding, 1},
                                   .HasVerifiedArrayBounds = true,
                               },
                               fragment) == RasterStatus::Ready);
    const RasterVertexStream stream{ .Stride = 12 };
    const RasterVertexAttribute attribute{ .Format = RasterVertexFormat::Float3 };
    RasterPipelineHandle pipeline;
    REQUIRE(CreateRasterPipeline(session.Device,
                                 {
                                     .Vertex = vertex,
                                     .Fragment = fragment,
                                     .Layout = table.Layout,
                                     .Streams = {&stream, 1},
                                     .Attributes = {&attribute, 1},
                                 },
                                 pipeline) == RasterStatus::Ready);
    RasterBindingIndex reachable[TEXTURE_COUNT];
    for (usize element = 0; element < TEXTURE_COUNT; ++element)
    {
        REQUIRE(GetBindingIndex(session.Device, table.Set, 1, static_cast<uint32>(element), reachable[element]) ==
                RasterStatus::Ready);
    }
    const RasterDraw draw
    {
        .Pipeline = pipeline,
        .Bindings = table.Set,
        .Vertices = {&scene.Slice, 1},
        .Indices = scene.Indices,
        .IndexCount = 3,
        .VertexCount = 3,
        .ReachableBindings = reachable,
    };
    for (bool hasCompleteUses : {false, true})
    {
        OrderedGraph graph;
        GraphVersion vertices;
        GraphVersion indices;
        GraphVersion textures[TEXTURE_COUNT];
        REQUIRE(CreateOrderedGraph(session.Device, graph) == RasterStatus::Ready);
        REQUIRE(ImportGraphBuffer(session.Device, graph, scene.Vertices, vertices) == RasterStatus::Ready);
        REQUIRE(ImportGraphBuffer(session.Device, graph, scene.Indices, indices) == RasterStatus::Ready);
        GraphUse uses[TEXTURE_COUNT + 2]{};
        uses[0] = { .Resource = vertices, .Access = GraphAccessMode::Vertex, .Size = 36 };
        uses[1] = { .Resource = indices, .Access = GraphAccessMode::Index, .Size = 6 };
        for (usize element = 0; element < TEXTURE_COUNT; ++element)
        {
            RasterTextureState state;
            REQUIRE(GetTextureState(session.Device, table.Textures[element], state) == RasterStatus::Ready);
            REQUIRE(ImportGraphTexture(session.Device,
                                       graph,
                                       table.Textures[element],
                                       state,
                                       RasterTextureUse::SampledFragment,
                                       textures[element]) == RasterStatus::Ready);
            uses[element + 2] =
            {
                .Resource = textures[element],
                .Access = GraphAccessMode::Sampled,
                .Visibility = RasterVisibility::Fragment,
            };
        }
        const GraphPassDescription pass
        {
            .Name = "reachable array",
            .Attachment = {},
            .Uses = uses,
            .UseCount = hasCompleteUses == true ? TEXTURE_COUNT + 2 : TEXTURE_COUNT + 1,
            .Draws = &draw,
            .DrawCount = 1,
        };
        REQUIRE(AddGraphPass(session.Device, graph, pass) == RasterStatus::Ready);
        GraphReport report;
        CHECK(CompileOrderedGraph(session.Device, graph, report) ==
              (hasCompleteUses == true ? RasterStatus::Ready : RasterStatus::InvalidDescription));
        CHECK(reference::Draws == 0);
        REQUIRE(DiscardOrderedGraph(session.Device, graph) == RasterStatus::Ready);
    }
}
