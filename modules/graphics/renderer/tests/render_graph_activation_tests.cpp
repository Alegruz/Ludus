#include "internal/lifecycle.h"
#include "reference_raster.h"

#include <ludus/foundation/base/core.h>

#include <ludus/foundation/containers/array.hpp>
#include <ludus/foundation/containers/static_array.hpp>
#include <ludus/graphics/renderer/render_graph_activation.hpp>
#include <ludus/graphics/rhi/device.h>

#include <catch2/catch_test_macros.hpp>

#include <new>
#include <string_view>

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
constexpr usize COMPOSITE_INPUT_COUNT = 1;
constexpr usize COMPOSITE_BINDING_COUNT = 2;
constexpr usize VERTEX_COUNT = 3;
constexpr usize INDEX_COUNT = 3;
constexpr uint32 WIDTH = 96;
constexpr uint32 HEIGHT = 64;
constexpr uint8 HOST_INTERFACE = 0xA5;
constexpr uint8 OTHER_INTERFACE = 0x5A;
constexpr std::string_view VERTEX_TEXT = "flat vertex fixture";
constexpr std::string_view FRAGMENT_TEXT = "flat fragment fixture";

/// Independent little-endian writer for the runtime_package.h layout.
struct PackageWriter final
{
    Array<uint8> Bytes;
    void WriteWord(uint32 value)
    {
        for (usize i = 0; i < sizeof(uint32); ++i)
        {
            Bytes.Add(static_cast<uint8>((value >> (8U * i)) & 0xFFU));
        }
    }
    void WritePadded(std::string_view text)
    {
        for (const char character : text)
        {
            Bytes.Add(static_cast<uint8>(character));
        }
        while (Bytes.GetSize() % sizeof(uint32) != 0)
        {
            Bytes.Add(0);
        }
    }
    void WriteIdentity(uint8 value)
    {
        for (usize i = 0; i < format::IDENTITY_BYTES; ++i)
        {
            Bytes.Add(value);
        }
    }
};
struct PackageOptions final
{
    uint8 Interface = HOST_INTERFACE;
    uint8 Generation = 1;
    uint32 Version = format::PACKAGE_VERSION;
    uint32 Backing = 0;
    bool HasDepthStages = true;
};
void WriteStage(PackageWriter& inoutWriter, format::StageRole role, std::string_view text, std::string_view entry)
{
    const bool isVertex = role == format::StageRole::ColorVertex || role == format::StageRole::DepthVertex;
    inoutWriter.WriteWord(static_cast<uint32>(role));
    inoutWriter.WriteWord(static_cast<uint32>(format::ShaderTarget::Wgsl));
    inoutWriter.WriteWord(isVertex == true ? static_cast<uint32>(format::STATIC_INPUT_COUNT) : 0);
    inoutWriter.WriteWord(static_cast<uint32>(entry.size()));
    inoutWriter.WriteWord(static_cast<uint32>(text.size()));
    for (uint32 i = 0; isVertex == true && i < format::STATIC_INPUT_COUNT; ++i)
    {
        inoutWriter.WriteWord(i);
        inoutWriter.WriteWord(static_cast<uint32>(i == 0 ? format::InputFormat::Float3 : format::InputFormat::Float4));
    }
    inoutWriter.WritePadded(entry);
    inoutWriter.WritePadded(text);
}
/// One exported RGBA8 color plus its private depth, one camera and one ColorAndDepth pass.
Array<uint8> MakePackageBytes(const PackageOptions& options)
{
    PackageWriter writer;
    writer.WriteWord(format::PACKAGE_MAGIC);
    writer.WriteWord(options.Version);
    writer.WriteWord(2);
    writer.WriteWord(1);
    writer.WriteWord(1);
    writer.WriteWord(options.HasDepthStages == true ? 4 : 2);
    writer.WriteIdentity(options.Interface);
    writer.WriteIdentity(options.Generation);
    for (const uint32 aspect : {static_cast<uint32>(format::Aspect::Color), static_cast<uint32>(format::Aspect::Depth)})
    {
        writer.WriteWord(WIDTH);
        writer.WriteWord(HEIGHT);
        writer.WriteWord(aspect);
        writer.WriteWord(options.Backing);
        writer.WriteWord(0);
        writer.WriteWord(aspect == 0 ? 1 : 0);
    }
    writer.WriteWord(0);
    for (const uint32 value : {static_cast<uint32>(format::Technique::ColorAndDepth),
                               0U,
                               0U,
                               1U,
                               static_cast<uint32>(format::Load::Clear),
                               static_cast<uint32>(format::Store::Store),
                               static_cast<uint32>(format::Load::Clear),
                               static_cast<uint32>(format::Store::Store),
                               0U})
    {
        writer.WriteWord(value);
    }
    WriteStage(writer, format::StageRole::ColorVertex, VERTEX_TEXT, "VertexMain");
    WriteStage(writer, format::StageRole::ColorFragment, FRAGMENT_TEXT, "FragmentMain");
    if (options.HasDepthStages == true)
    {
        WriteStage(writer, format::StageRole::DepthVertex, VERTEX_TEXT, "VertexMain");
        WriteStage(writer, format::StageRole::DepthFragment, FRAGMENT_TEXT, "FragmentMain");
    }
    return Move(writer.Bytes);
}
core::UniquePtr<renderer::RenderGraphPackage> MakePackage(const PackageOptions& options)
{
    const Array<uint8> bytes = MakePackageBytes(options);
    renderer::RenderGraphPackage* allocationOrNull = new (std::nothrow) renderer::RenderGraphPackage;
    core::UniquePtr<renderer::RenderGraphPackage> package{allocationOrNull};
    REQUIRE(package.Get() != nullptr);
    REQUIRE(package->Decode({bytes.GetData(), bytes.GetSize()}) == renderer::RenderGraphPackageStatus::Ready);
    return package;
}
renderer::RenderGraphPackageStatus DecodeBytes(const Array<uint8>& bytes)
{
    renderer::RenderGraphPackage package;
    const renderer::RenderGraphPackageStatus status = package.Decode({bytes.GetData(), bytes.GetSize()});
    CHECK(package.IsLoaded() == (status == renderer::RenderGraphPackageStatus::Ready));
    return status;
}
struct ActivationFixture final
{
    rhi::DeviceHandle Device{};
    rhi::SurfaceHandle Surface{};
    renderer::RenderGraphActivation Activation;
    StaticArray<uint8, format::IDENTITY_BYTES> Interface{};
    StaticArray<rhi::RasterShaderInput, COMPOSITE_INPUT_COUNT> CompositeInputs{{{0, rhi::RasterVertexFormat::Float4}}};
    StaticArray<rhi::RasterBinding, COMPOSITE_BINDING_COUNT> CompositeBindings{
        {{0, rhi::RasterBindingKind::Texture2D, rhi::RasterVisibility::Fragment, 0},
         {1, rhi::RasterBindingKind::Sampler, rhi::RasterVisibility::Fragment, 0}}};
    StaticArray<math::Vector3, VERTEX_COUNT> Positions{{{-.1F, -.1F, .5F}, {.1F, -.1F, .5F}, {0, .1F, .5F}}};
    StaticArray<uint32, INDEX_COUNT> Indices{{0, 1, 2}};
    StaticArray<renderer::RenderGraphMesh, 1> Meshes{};
    StaticArray<renderer::RenderGraphItem, 1> Items{};
    StaticArray<renderer::ViewDescription, 1> Cameras{};
    ActivationFixture()
    {
        rhi::Shutdown();
        rhi::reference::Reset();
        rhi::reference::ProfileAvailable = true;
        rhi::backend::ImmediateError = rhi::StartupError::None;
        rhi::backend::NextFrame = rhi::FrameStatus::Ready;
        rhi::backend::ActiveKind = rhi::Backend::WebGPU;
        rhi::backend::SelectionSupported = true;
        REQUIRE(rhi::CreateDevice({}, {}, {}, Device, Surface) == rhi::DeviceStatus::Pending);
        rhi::internal::Complete(rhi::backend::PendingToken, rhi::StartupError::None, {4096, 16384});
        for (usize i = 0; i < format::IDENTITY_BYTES; ++i)
        {
            Interface[i] = HOST_INTERFACE;
        }
        Meshes[0].Geometry.Positions = Positions.GetData();
        Meshes[0].Geometry.VertexCount = Positions.GetSize();
        Meshes[0].Geometry.Indices = Indices.GetData();
        Meshes[0].Geometry.IndexCount = Indices.GetSize();
        Items[0].SourceId = 42;
        Items[0].Material.Color = {1, 1, 1, 1};
        rhi::RasterShaderDescription compositeVertex;
        compositeVertex.Artifact.Stage = rhi::ShaderStage::Vertex;
        compositeVertex.Artifact.Wgsl = "composite vertex fixture";
        compositeVertex.Artifact.WgslEntry = "VertexMain";
        compositeVertex.Inputs = CompositeInputs.AsSpan();
        rhi::RasterShaderDescription compositeFragment;
        compositeFragment.Artifact.Stage = rhi::ShaderStage::Fragment;
        compositeFragment.Artifact.Wgsl = "composite fragment fixture";
        compositeFragment.Artifact.WgslEntry = "FragmentMain";
        compositeFragment.Bindings = CompositeBindings.AsSpan();
        REQUIRE(Activation.Initialize(Device,
                                      {Interface.GetData(), Interface.GetSize()},
                                      compositeVertex,
                                      compositeFragment) == rhi::RasterStatus::Ready);
    }
    ~ActivationFixture()
    {
        Activation.Reset();
        rhi::Shutdown();
    }
    rhi::RasterStatus Update(renderer::RenderGraphReport& outReport, uint64 revision = 1)
    {
        return Activation.Update({Meshes.GetData(), Meshes.GetSize()},
                                 {Items.GetData(), Items.GetSize()},
                                 {Cameras.GetData(), Cameras.GetSize()},
                                 revision,
                                 renderer::RenderGraphCoveragePolicy::RejectIncomplete,
                                 outReport);
    }
    /// Activate a first generation, draw revision 1 and complete it.
    void ActivateFirst()
    {
        REQUIRE(Activation.Stage(MakePackage({})) == rhi::RasterStatus::Ready);
        REQUIRE(Activation.Poll() == rhi::RasterStatus::Ready);
        REQUIRE(Activation.GetReport().ActiveGeneration == 1);
        renderer::RenderGraphReport report;
        REQUIRE(Update(report) == rhi::RasterStatus::Ready);
        Render();
        rhi::reference::Completed = rhi::reference::Submitted;
        REQUIRE(Activation.GetActiveOrNull()->GetOutputSceneRevision() == 1);
    }
    void Render()
    {
        renderer::RenderGraph* graphOrNull = Activation.GetActiveOrNull();
        REQUIRE(graphOrNull != nullptr);
        rhi::SubmissionToken completion;
        REQUIRE(graphOrNull->Render(Surface, WIDTH, HEIGHT, completion) == rhi::RasterStatus::Ready);
    }
    static void CompletePending(rhi::RasterStatus status)
    {
        for (usize i = 0; i < rhi::reference::PendingCount; ++i)
        {
            rhi::internal::RasterComplete(rhi::reference::PendingRequests[i], status);
        }
        rhi::reference::Next = rhi::RasterStatus::Ready;
    }
};
} // namespace

TEST_CASE("RG3 runtime packages decode plan records and copy caller bytes", "[render-graph][activation]")
{
    Array<uint8> bytes = MakePackageBytes({});
    renderer::RenderGraphPackage package;
    REQUIRE(package.Decode({bytes.GetData(), bytes.GetSize()}) == renderer::RenderGraphPackageStatus::Ready);
    // An unloadable module may release or overwrite its buffer immediately.
    for (usize i = 0; i < bytes.GetSize(); ++i)
    {
        bytes[i] = 0;
    }
    const renderer::RenderGraphPlan plan = package.GetPlan();
    REQUIRE(plan.Resources.GetSize() == 2);
    CHECK(plan.Resources[0].Width == WIDTH);
    CHECK(plan.Resources[1].Kind == format::Aspect::Depth);
    CHECK(plan.Passes[0].Program == format::Technique::ColorAndDepth);
    CHECK(plan.Cameras[0].Target == 0);
    const renderer::RenderGraphPrograms programs = package.GetPrograms({}, {});
    CHECK(programs.ColorVertex.Artifact.Wgsl == VERTEX_TEXT);
    CHECK(programs.ColorVertex.Artifact.WgslEntry == "VertexMain");
    CHECK(programs.ColorVertex.Inputs.size() == format::STATIC_INPUT_COUNT);
    CHECK(programs.ColorFragment.Inputs.empty() == true);
    CHECK(programs.DepthFragment.Artifact.Wgsl == FRAGMENT_TEXT);
    CHECK(package.GetInterfaceIdentity()[0] == HOST_INTERFACE);
    CHECK(package.GetGenerationIdentity()[31] == 1);
    CHECK(package.Decode({bytes.GetData(), bytes.GetSize()}) == renderer::RenderGraphPackageStatus::InvalidState);
}

TEST_CASE("RG3 malformed runtime packages reject without a partial generation", "[render-graph][activation]")
{
    const Array<uint8> valid = MakePackageBytes({});
    PackageOptions options;
    options.Version = format::PACKAGE_VERSION + 1;
    CHECK(DecodeBytes(MakePackageBytes(options)) == renderer::RenderGraphPackageStatus::UnsupportedVersion);
    Array<uint8> truncated = Array<uint8>::FromRange(valid.AsSpan());
    truncated.RemoveLast();
    CHECK(DecodeBytes(truncated) == renderer::RenderGraphPackageStatus::Malformed);
    Array<uint8> trailing = Array<uint8>::FromRange(valid.AsSpan());
    for (usize i = 0; i < sizeof(uint32); ++i)
    {
        trailing.Add(0);
    }
    CHECK(DecodeBytes(trailing) == renderer::RenderGraphPackageStatus::Malformed);
    Array<uint8> badMagic = Array<uint8>::FromRange(valid.AsSpan());
    badMagic[0] = 0;
    CHECK(DecodeBytes(badMagic) == renderer::RenderGraphPackageStatus::UnsupportedVersion);
    Array<uint8> manyResources = Array<uint8>::FromRange(valid.AsSpan());
    manyResources[8] = static_cast<uint8>(format::MAX_RESOURCES + 1);
    CHECK(DecodeBytes(manyResources) == renderer::RenderGraphPackageStatus::CapacityExceeded);
    // Resource 0's aspect field lies after the header and two record words.
    Array<uint8> badAspect = Array<uint8>::FromRange(valid.AsSpan());
    badAspect[format::HEADER_BYTES + 2 * sizeof(uint32)] = 7;
    CHECK(DecodeBytes(badAspect) == renderer::RenderGraphPackageStatus::Malformed);
    // The fragment entry "FragmentMain" is 12 bytes; corrupt the vertex entry's zero padding instead.
    const usize firstStage = format::HEADER_BYTES + 2 * format::RESOURCE_RECORD_BYTES + format::CAMERA_RECORD_BYTES +
                             format::PASS_RECORD_BYTES;
    const usize vertexEntryPadding = firstStage + format::STAGE_FIELD_COUNT * sizeof(uint32) +
                                     format::STATIC_INPUT_COUNT * format::INPUT_RECORD_BYTES + 10;
    Array<uint8> badPadding = Array<uint8>::FromRange(valid.AsSpan());
    badPadding[vertexEntryPadding] = 1;
    CHECK(DecodeBytes(badPadding) == renderer::RenderGraphPackageStatus::Malformed);
    Array<uint8> badInput = Array<uint8>::FromRange(valid.AsSpan());
    badInput[firstStage + format::STAGE_FIELD_COUNT * sizeof(uint32) + sizeof(uint32)] = 0;
    CHECK(DecodeBytes(badInput) == renderer::RenderGraphPackageStatus::Malformed);
    // A color-only package omits both DepthOnly roles consistently.
    PackageOptions colorOnly;
    colorOnly.HasDepthStages = false;
    CHECK(DecodeBytes(MakePackageBytes(colorOnly)) == renderer::RenderGraphPackageStatus::Ready);
    Array<uint8> duplicate = Array<uint8>::FromRange(valid.AsSpan());
    // Relabel the DepthVertex stage as a second ColorVertex record.
    const usize stageBytes = format::STAGE_FIELD_COUNT * sizeof(uint32) +
                             format::STATIC_INPUT_COUNT * format::INPUT_RECORD_BYTES + 12 + 20 +
                             format::STAGE_FIELD_COUNT * sizeof(uint32) + 12 + 24;
    duplicate[firstStage + stageBytes] = 0;
    CHECK(DecodeBytes(duplicate) == renderer::RenderGraphPackageStatus::Malformed);
}

TEST_CASE("RG3 first generation activates once its pipelines are ready", "[render-graph][activation]")
{
    ActivationFixture fixture;
    CHECK(fixture.Activation.GetActiveOrNull() == nullptr);
    rhi::reference::Next = rhi::RasterStatus::Pending;
    REQUIRE(fixture.Activation.Stage(MakePackage({})) == rhi::RasterStatus::Pending);
    CHECK(fixture.Activation.GetReport().Candidate == renderer::RenderGraphCandidateState::Preparing);
    CHECK(fixture.Activation.Poll() == rhi::RasterStatus::Pending);
    CHECK(fixture.Activation.GetActiveOrNull() == nullptr);
    renderer::RenderGraphReport report;
    CHECK(fixture.Update(report) == rhi::RasterStatus::NotReady);
    ActivationFixture::CompletePending(rhi::RasterStatus::Ready);
    REQUIRE(fixture.Activation.Poll() == rhi::RasterStatus::Ready);
    CHECK(fixture.Activation.GetReport().ActiveGeneration == 1);
    CHECK(fixture.Activation.GetReport().Candidate == renderer::RenderGraphCandidateState::None);
    REQUIRE(fixture.Update(report) == rhi::RasterStatus::Ready);
    fixture.Render();
    CHECK(rhi::reference::Draws == 1);
}

TEST_CASE("RG3 pending replacement keeps drawing the active generation", "[render-graph][activation]")
{
    ActivationFixture fixture;
    fixture.ActivateFirst();
    renderer::RenderGraph* firstOrNull = fixture.Activation.GetActiveOrNull();
    const usize shaderCreates = rhi::reference::Creates[static_cast<usize>(rhi::internal::RasterKind::Shader)];
    rhi::reference::Next = rhi::RasterStatus::Pending;
    PackageOptions second;
    second.Generation = 2;
    REQUIRE(fixture.Activation.Stage(MakePackage(second)) == rhi::RasterStatus::Pending);
    CHECK(rhi::reference::Creates[static_cast<usize>(rhi::internal::RasterKind::Shader)] > shaderCreates);
    CHECK(fixture.Activation.Poll() == rhi::RasterStatus::Ready);
    CHECK(fixture.Activation.GetReport().Candidate == renderer::RenderGraphCandidateState::Preparing);
    CHECK(fixture.Activation.GetActiveOrNull() == firstOrNull);
    // Drawing never creates the candidate's pending pipelines.
    const usize pipelineCreates = rhi::reference::Creates[static_cast<usize>(rhi::internal::RasterKind::Pipeline)];
    fixture.Render();
    CHECK(rhi::reference::Draws == 2);
    CHECK(rhi::reference::Creates[static_cast<usize>(rhi::internal::RasterKind::Pipeline)] == pipelineCreates);
    ActivationFixture::CompletePending(rhi::RasterStatus::Ready);
    REQUIRE(fixture.Activation.Poll() == rhi::RasterStatus::Ready);
    // Pipelines are ready, but the candidate has not prepared the current extraction.
    CHECK(fixture.Activation.GetReport().Candidate == renderer::RenderGraphCandidateState::NeedsUpdate);
    CHECK(fixture.Activation.GetActiveOrNull() == firstOrNull);
    renderer::RenderGraphReport report;
    REQUIRE(fixture.Update(report) == rhi::RasterStatus::Ready);
    rhi::reference::Completed = rhi::reference::Submitted;
    REQUIRE(fixture.Activation.Poll() == rhi::RasterStatus::Ready);
    CHECK(fixture.Activation.GetReport().ActiveGeneration == 2);
    renderer::RenderGraph* secondOrNull = fixture.Activation.GetActiveOrNull();
    REQUIRE(secondOrNull != nullptr);
    // The new generation never relabels the previous generation's pixels.
    CHECK(secondOrNull->GetOutputSceneRevision() == 0);
    CHECK(secondOrNull->GetDrawableSceneRevision() == 1);
    fixture.Render();
    rhi::reference::Completed = rhi::reference::Submitted;
    CHECK(secondOrNull->GetOutputSceneRevision() == 1);
}

TEST_CASE("RG3 failed reload preserves the active generation and its pixels", "[render-graph][activation]")
{
    ActivationFixture fixture;
    fixture.ActivateFirst();
    renderer::RenderGraph* firstOrNull = fixture.Activation.GetActiveOrNull();
    rhi::reference::Next = rhi::RasterStatus::Pending;
    PackageOptions second;
    second.Generation = 2;
    REQUIRE(fixture.Activation.Stage(MakePackage(second)) == rhi::RasterStatus::Pending);
    ActivationFixture::CompletePending(rhi::RasterStatus::Failed);
    CHECK(fixture.Activation.Poll() == rhi::RasterStatus::Ready);
    const renderer::RenderGraphActivationReport failed = fixture.Activation.GetReport();
    CHECK(failed.Candidate == renderer::RenderGraphCandidateState::Failed);
    CHECK(failed.LastFailure == rhi::RasterStatus::Failed);
    CHECK(failed.ActiveGeneration == 1);
    CHECK(fixture.Activation.GetActiveOrNull() == firstOrNull);
    CHECK(firstOrNull->GetOutputSceneRevision() == 1);
    fixture.Render();
    CHECK(rhi::reference::Draws == 2);
    // A plan rejected before GPU setup fails immediately with the same guarantee.
    PackageOptions malformed;
    malformed.Generation = 3;
    malformed.Backing = 31;
    CHECK(fixture.Activation.Stage(MakePackage(malformed)) == rhi::RasterStatus::InvalidDescription);
    CHECK(fixture.Activation.GetReport().Candidate == renderer::RenderGraphCandidateState::Failed);
    CHECK(fixture.Activation.GetReport().CandidateGeneration == 3);
    CHECK(fixture.Activation.GetActiveOrNull() == firstOrNull);
}

TEST_CASE("RG3 CPU interface mismatch requires a restart and stages nothing", "[render-graph][activation]")
{
    ActivationFixture fixture;
    fixture.ActivateFirst();
    const usize shaderCreates = rhi::reference::Creates[static_cast<usize>(rhi::internal::RasterKind::Shader)];
    PackageOptions changed;
    changed.Interface = OTHER_INTERFACE;
    CHECK(fixture.Activation.Stage(MakePackage(changed)) == rhi::RasterStatus::Unsupported);
    CHECK(fixture.Activation.GetReport().Candidate == renderer::RenderGraphCandidateState::RestartRequired);
    CHECK(rhi::reference::Creates[static_cast<usize>(rhi::internal::RasterKind::Shader)] == shaderCreates);
    CHECK(fixture.Activation.GetReport().ActiveGeneration == 1);
    fixture.Render();
}

TEST_CASE("RG3 activation waits for confirmed submission and retires the old generation", "[render-graph][activation]")
{
    ActivationFixture fixture;
    fixture.ActivateFirst();
    renderer::RenderGraph& first = *fixture.Activation.GetActiveOrNull();
    PackageOptions second;
    second.Generation = 2;
    REQUIRE(fixture.Activation.Stage(MakePackage(second)) == rhi::RasterStatus::Ready);
    renderer::RenderGraphReport report;
    REQUIRE(fixture.Activation.Poll() == rhi::RasterStatus::Ready);
    REQUIRE(fixture.Update(report) == rhi::RasterStatus::Ready);
    REQUIRE(rhi::BeginFrame(fixture.Device, fixture.Surface) == rhi::DeviceStatus::Ready);
    REQUIRE(first.Encode(fixture.Surface) == rhi::RasterStatus::Ready);
    rhi::SubmissionToken completion;
    REQUIRE(rhi::EndFrame(fixture.Device, completion) == rhi::RasterStatus::Ready);
    // The active generation's encoding still needs its actual submission token.
    CHECK(fixture.Activation.Poll() == rhi::RasterStatus::InvalidState);
    CHECK(fixture.Activation.GetReport().ActiveGeneration == 1);
    REQUIRE(first.ConfirmSubmission(completion) == rhi::RasterStatus::Pending);
    const usize textureDestroys = rhi::reference::Destroys[static_cast<usize>(rhi::internal::RasterKind::Texture)];
    REQUIRE(fixture.Activation.Poll() == rhi::RasterStatus::Ready);
    CHECK(fixture.Activation.GetReport().ActiveGeneration == 2);
    // The replaced allocation retires through RHI completion, not at activation.
    CHECK(rhi::reference::Destroys[static_cast<usize>(rhi::internal::RasterKind::Texture)] == textureDestroys);
    rhi::reference::Completed = rhi::reference::Submitted;
    fixture.Render();
    CHECK(rhi::reference::Destroys[static_cast<usize>(rhi::internal::RasterKind::Texture)] > textureDestroys);
}

TEST_CASE("RG3 a newer candidate supersedes an unactivated one", "[render-graph][activation]")
{
    ActivationFixture fixture;
    fixture.ActivateFirst();
    rhi::reference::Next = rhi::RasterStatus::Pending;
    PackageOptions second;
    second.Generation = 2;
    REQUIRE(fixture.Activation.Stage(MakePackage(second)) == rhi::RasterStatus::Pending);
    ActivationFixture::CompletePending(rhi::RasterStatus::Ready);
    PackageOptions third;
    third.Generation = 3;
    REQUIRE(fixture.Activation.Stage(MakePackage(third)) == rhi::RasterStatus::Ready);
    CHECK(fixture.Activation.GetReport().CandidateGeneration == 3);
    REQUIRE(fixture.Activation.Poll() == rhi::RasterStatus::Ready);
    renderer::RenderGraphReport report;
    REQUIRE(fixture.Update(report) == rhi::RasterStatus::Ready);
    REQUIRE(fixture.Activation.Poll() == rhi::RasterStatus::Ready);
    CHECK(fixture.Activation.GetReport().ActiveGeneration == 3);
}
