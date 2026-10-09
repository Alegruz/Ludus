#include "internal/lifecycle.h"
#include "reference_raster.h"

#include <ludus/graphics/rhi/lifetime.h>

#include <cstring>
#include <span>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
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
struct Session final
{
    DeviceHandle Device;
    SurfaceHandle Surface;
    Session()
    {
        Shutdown();
        reference::Reset();
        backend::ImmediateError = StartupError::None;
        backend::NextFrame = FrameStatus::Ready;
        backend::ActiveKind = Backend::WebGPU;
        backend::SelectionSupported = true;
        REQUIRE(CreateDevice({}, {}, {}, Device, Surface) == DeviceStatus::Pending);
        internal::Complete(backend::PendingToken, StartupError::None, {4096, 16384});
    }
    ~Session() noexcept
    {
        Shutdown();
    }
};
template <typename T, usize N>
std::span<const uint8> Bytes(const T (&data)[N]) noexcept
{
    return {reinterpret_cast<const uint8*>(data), sizeof(data)};
}
struct Scene final
{
    RasterShaderHandle Vertex, Fragment;
    BindingLayoutHandle Layout;
    BindingSetHandle Set;
    RasterPipelineHandle Pipeline;
    BufferHandle Uniform, Vertices, Indices;
    RasterVertexSlice Slices[1];
    Scene(DeviceHandle device)
    {
        const RasterBinding binding{0, RasterBindingKind::UniformBuffer, RasterVisibility::Both, 16};
        const RasterShaderInput input{0, RasterVertexFormat::Float3};
        RasterShaderDescription vertex;
        vertex.Artifact.Stage = ShaderStage::Vertex;
        vertex.Artifact.Wgsl = "fixture vertex";
        vertex.Artifact.WgslEntry = "vertexMain";
        vertex.Inputs = {&input, 1};
        vertex.Bindings = {&binding, 1};
        REQUIRE(CreateRasterShader(device, vertex, Vertex) == RasterStatus::Ready);
        auto fragment = vertex;
        fragment.Inputs = {};
        fragment.Artifact.Stage = ShaderStage::Fragment;
        fragment.Artifact.WgslEntry = "fragmentMain";
        REQUIRE(CreateRasterShader(device, fragment, Fragment) == RasterStatus::Ready);
        REQUIRE(CreateBindingLayout(device, {&binding, 1}, Layout) == RasterStatus::Ready);
        const uint8 uniform[16]{};
        REQUIRE(CreateBuffer(device, {BufferRole::Uniform, sizeof(uniform)}, uniform, Uniform) == RasterStatus::Ready);
        const RasterBindingResource resource{ .Buffer = Uniform, .Size = sizeof(uniform) };
        REQUIRE(CreateBindingSet(device, Layout, {&resource, 1}, Set) == RasterStatus::Ready);
        const RasterVertexStream stream{12, false};
        const RasterVertexAttribute attribute{0, 0, 0, RasterVertexFormat::Float3};
        REQUIRE(CreateRasterPipeline(device,
                                     {Vertex, Fragment, Layout, {&stream, 1}, {&attribute, 1}, true},
                                     Pipeline) == RasterStatus::Ready);
        const float32 vertices[9]{};
        const uint16 indices[3]{0, 1, 2};
        REQUIRE(CreateBuffer(device, {BufferRole::Vertex, sizeof(vertices)}, Bytes(vertices), Vertices) ==
                RasterStatus::Ready);
        REQUIRE(CreateBuffer(device, {BufferRole::Index16, sizeof(indices)}, Bytes(indices), Indices) ==
                RasterStatus::Ready);
        Slices[0] = {Vertices, 0, sizeof(vertices)};
    }
    [[nodiscard]] RasterDraw Draw() const noexcept
    {
        return {Pipeline, Set, Slices, Indices, 0, 3, 3, 1};
    }
};
} // namespace
TEST_CASE("Portable resource admission preserves outputs and checks packing before backend calls", "[rhi][raster]")
{
    Session session;
    const uint8 bytes[16]{};
    BufferHandle buffer;
    CHECK(CreateBuffer({}, {BufferRole::Uniform, 16}, bytes, buffer) == RasterStatus::InvalidHandle);
    CHECK(CreateBuffer(session.Device, {BufferRole::Uniform, 17}, bytes, buffer) == RasterStatus::InvalidDescription);
    CHECK(reference::Creates[0] == 0);
    REQUIRE(CreateBuffer(session.Device, {BufferRole::Uniform, 16}, bytes, buffer) == RasterStatus::Ready);
    CHECK(CreateBuffer(session.Device, {BufferRole::Uniform, 16}, bytes, buffer) == RasterStatus::InvalidState);
    CHECK(reference::Creates[0] == 1);
    TextureHandle texture;
    CHECK(CreateTexture(session.Device, {2, 2}, {bytes, 7}, texture) == RasterStatus::InvalidDescription);
    CHECK(CreateTexture(session.Device, {2, 2}, {bytes, ~usize{0} - 3}, texture) == RasterStatus::InvalidDescription);
    REQUIRE(CreateTexture(session.Device, {2, 2}, {bytes, 8}, texture) == RasterStatus::Ready);
    TextureViewHandle view;
    REQUIRE(CreateTextureView(session.Device, texture, view) == RasterStatus::Ready);
    auto stale = texture;
    REQUIRE(Destroy(session.Device, texture) == RasterStatus::Ready);
    CHECK(GetStatus(session.Device, stale) == RasterStatus::InvalidHandle);
    CHECK(reference::Destroys[1] == 0);
    REQUIRE(Destroy(session.Device, view) == RasterStatus::Ready);
    CHECK(reference::Destroys[1] == 1);
    CHECK(reference::Destroys[2] == 1);
}
TEST_CASE("Portable bindings retain detached resources and reject range/layout mismatch", "[rhi][raster]")
{
    Session session;
    Scene scene(session.Device);
    const auto stale = scene.Uniform;
    REQUIRE(Destroy(session.Device, scene.Uniform) == RasterStatus::Ready);
    CHECK(GetStatus(session.Device, stale) == RasterStatus::InvalidHandle);
    CHECK(reference::Destroys[0] == 0);
    const RasterBinding duplicate[2]{{0, RasterBindingKind::UniformBuffer, RasterVisibility::Both, 16},
                                     {0, RasterBindingKind::Sampler, RasterVisibility::Both, 0}};
    BindingLayoutHandle layout;
    CHECK(CreateBindingLayout(session.Device, duplicate, layout) == RasterStatus::InvalidDescription);
    const RasterBinding different{0, RasterBindingKind::UniformBuffer, RasterVisibility::Both, 16};
    REQUIRE(CreateBindingLayout(session.Device, {&different, 1}, layout) == RasterStatus::Ready);
    BindingSetHandle set;
    RasterBindingResource bad{ .Buffer = scene.Vertices, .Offset = 1, .Size = 16 };
    CHECK(CreateBindingSet(session.Device, layout, {&bad, 1}, set) == RasterStatus::InvalidDescription);
    bad.Buffer = stale;
    CHECK(CreateBindingSet(session.Device, layout, {&bad, 1}, set) == RasterStatus::InvalidHandle);
    REQUIRE(Destroy(session.Device, scene.Set) == RasterStatus::Ready);
    CHECK(reference::Destroys[0] == 1);
}
TEST_CASE("Portable pending callbacks cannot resurrect cancelled or old-session resources", "[rhi][raster]")
{
    Session session;
    const uint8 bytes[16]{};
    BufferHandle buffer;
    reference::Next = RasterStatus::Pending;
    REQUIRE(CreateBuffer(session.Device, {BufferRole::Uniform, 16}, bytes, buffer) == RasterStatus::Pending);
    const auto oldRequest = reference::LastRequest;
    const auto stale = buffer;
    REQUIRE(Destroy(session.Device, buffer) == RasterStatus::Ready);
    CHECK(reference::Destroys[0] == 0);
    internal::RasterComplete(oldRequest, RasterStatus::Failed);
    reference::Next = RasterStatus::Ready;
    REQUIRE(CreateBuffer(session.Device, {BufferRole::Uniform, 16}, bytes, buffer) == RasterStatus::Ready);
    CHECK(reference::Destroys[0] == 1);
    internal::RasterComplete(oldRequest, RasterStatus::Ready);
    CHECK(GetStatus(session.Device, stale) == RasterStatus::InvalidHandle);
    CHECK(GetStatus(session.Device, buffer) == RasterStatus::Ready);
}
TEST_CASE("Portable draw preflight rejects invalid packets before native calls", "[rhi][raster]")
{
    Session session;
    Scene scene(session.Device);
    auto draw = scene.Draw();
    CHECK(DrawIndexed(session.Device, draw) == RasterStatus::InvalidState);
    REQUIRE(BeginFrame(session.Device, session.Surface) == DeviceStatus::Ready);
    draw.VertexCount = 2;
    CHECK(DrawIndexed(session.Device, draw) == RasterStatus::InvalidDescription);
    draw = scene.Draw();
    draw.IndexOffset = 1;
    CHECK(DrawIndexed(session.Device, draw) == RasterStatus::InvalidDescription);
    draw = scene.Draw();
    draw.InstanceCount = 0;
    CHECK(DrawIndexed(session.Device, draw) == RasterStatus::InvalidDescription);
    draw = scene.Draw();
    scene.Slices[0].Size = 35;
    CHECK(DrawIndexed(session.Device, draw) == RasterStatus::InvalidDescription);
    scene.Slices[0].Size = 36;
    reference::Submission = RasterStatus::CapacityExceeded;
    CHECK(DrawIndexed(session.Device, draw) == RasterStatus::CapacityExceeded);
    CHECK(reference::Draws == 0);
    reference::Submission = RasterStatus::Ready;
    REQUIRE(DrawIndexed(session.Device, draw) == RasterStatus::Ready);
    CHECK(reference::Draws == 1);
    CHECK(Destroy(session.Device, scene.Indices) == RasterStatus::InvalidState);
    REQUIRE(EndFrame(session.Device) == DeviceStatus::Ready);
}
TEST_CASE("Portable logical destruction waits for CPU leases and true GPU completion", "[rhi][raster]")
{
    Session session;
    Scene scene(session.Device);
    const auto draw = scene.Draw();
    REQUIRE(BeginFrame(session.Device, session.Surface) == DeviceStatus::Ready);
    REQUIRE(DrawIndexed(session.Device, draw) == RasterStatus::Ready);
    REQUIRE(EndFrame(session.Device) == DeviceStatus::Ready);
    REQUIRE(reference::Submitted != 0);
    REQUIRE(Destroy(session.Device, scene.Pipeline) == RasterStatus::Ready);
    REQUIRE(Destroy(session.Device, scene.Set) == RasterStatus::Ready);
    REQUIRE(Destroy(session.Device, scene.Uniform) == RasterStatus::Ready);
    REQUIRE(Destroy(session.Device, scene.Vertices) == RasterStatus::Ready);
    REQUIRE(Destroy(session.Device, scene.Indices) == RasterStatus::Ready);
    REQUIRE(Destroy(session.Device, scene.Vertex) == RasterStatus::Ready);
    REQUIRE(Destroy(session.Device, scene.Fragment) == RasterStatus::Ready);
    REQUIRE(Destroy(session.Device, scene.Layout) == RasterStatus::Ready);
    for (auto count : reference::Destroys)
    {
        CHECK(count == 0);
    }
    reference::Completed = reference::Submitted;
    SamplerHandle sampler;
    REQUIRE(CreateSampler(session.Device, {}, sampler) == RasterStatus::Ready);
    CHECK(reference::Destroys[0] == 3);
    CHECK(reference::Destroys[4] == 2);
    CHECK(reference::Destroys[5] == 1);
    CHECK(reference::Destroys[6] == 1);
    CHECK(reference::Destroys[7] == 1);
}

TEST_CASE("Portable identities exhaust without wrapping or becoming live again", "[rhi][raster]")
{
    internal::RasterSequence<uint32> requests{~uint32{0} - 1};
    CHECK(requests.Take() == ~uint32{0} - 1);
    CHECK(requests.Take() == ~uint32{0});
    CHECK(requests.Take() == 0);
    CHECK(requests.Take() == 0);
    internal::RasterSequence<uint64> submissions{~uint64{0}};
    CHECK(submissions.Take() == ~uint64{0});
    CHECK(submissions.Take() == 0);
    CHECK(internal::RasterNextGeneration(~uint64{0} - 1) == ~uint64{0});
    CHECK(internal::RasterNextGeneration(~uint64{0}) == 0);
    CHECK(internal::RasterNextGeneration(0) == 0);
}
TEST_CASE("Failed asynchronous pipeline dependencies preserve their failure result", "[rhi][raster]")
{
    Session session;
    Scene scene(session.Device);
    reference::Next = RasterStatus::Pending;
    const RasterVertexStream stream{12, false};
    const RasterVertexAttribute attribute{0, 0, 0, RasterVertexFormat::Float3};
    RasterPipelineHandle pipeline;
    REQUIRE(CreateRasterPipeline(session.Device,
                                 {scene.Vertex, scene.Fragment, scene.Layout, {&stream, 1}, {&attribute, 1}, true},
                                 pipeline) == RasterStatus::Pending);
    const auto request = reference::LastRequest;
    internal::RasterExpect(request, internal::RasterCallbacks::Two);
    internal::RasterFail(request, RasterStatus::OutOfMemory);
    REQUIRE(GetStatus(session.Device, pipeline) == RasterStatus::OutOfMemory);
    REQUIRE(BeginFrame(session.Device, session.Surface) == DeviceStatus::Ready);
    auto draw = scene.Draw();
    draw.Pipeline = pipeline;
    CHECK(DrawIndexed(session.Device, draw) == RasterStatus::OutOfMemory);
    CHECK(reference::Draws == 0);
    REQUIRE(EndFrame(session.Device) == DeviceStatus::Ready);
    REQUIRE(Destroy(session.Device, pipeline) == RasterStatus::Ready);
    CHECK(reference::Destroys[7] == 0);
    internal::RasterComplete(request, RasterStatus::Ready);
    internal::RasterComplete(request, RasterStatus::Ready);
    REQUIRE(Destroy(session.Device, scene.Pipeline) == RasterStatus::Ready);
    CHECK(reference::Destroys[7] == 2);
}
TEST_CASE("Portable admission verifies negotiated profile before readiness", "[rhi][raster]")
{
    Session session;
    REQUIRE(DestroyDevice(session.Device) == DeviceStatus::Ready);
    session.Surface = {};
    reference::ProfileAvailable = false;
    DeviceDescription description;
    description.Required.PortableRaster = true;
    REQUIRE(ValidateDeviceDescription(description) == DeviceStatus::Ready);
    REQUIRE(CreateDevice({}, {}, description, session.Device, session.Surface) == DeviceStatus::Pending);
    internal::Complete(backend::PendingToken, StartupError::None, {4096, 16384});
    DeviceInfo info;
    CHECK(GetDeviceInfo(session.Device, info) == DeviceStatus::Failed);
    CHECK(info.Startup.Error == StartupError::RequirementsUnsatisfied);
    CHECK(info.Startup.UnmetRequirement == RequirementFailure::PortableRaster);
    CHECK_FALSE(info.Enabled.PortableRaster);
}
TEST_CASE("Pending dependencies publish no handles and multiple validation scopes must drain", "[rhi][raster]")
{
    Session session;
    const uint8 image[4]{};
    TextureHandle texture;
    TextureViewHandle view;
    reference::Next = RasterStatus::Pending;
    REQUIRE(CreateTexture(session.Device, {1, 1}, {image, 4}, texture) == RasterStatus::Pending);
    const auto request = reference::LastRequest;
    internal::RasterExpect(request, internal::RasterCallbacks::Two);
    CHECK(CreateTextureView(session.Device, texture, view) == RasterStatus::NotReady);
    internal::RasterComplete(request, RasterStatus::Ready);
    CHECK(GetStatus(session.Device, texture) == RasterStatus::Pending);
    REQUIRE(Destroy(session.Device, texture) == RasterStatus::Ready);
    SamplerHandle sampler;
    reference::Next = RasterStatus::Ready;
    REQUIRE(CreateSampler(session.Device, {}, sampler) == RasterStatus::Ready);
    CHECK(reference::Destroys[1] == 0);
    internal::RasterComplete(request, RasterStatus::OutOfMemory);
    REQUIRE(Destroy(session.Device, sampler) == RasterStatus::Ready);
    CHECK(reference::Destroys[1] == 1);
}
TEST_CASE("Portable pool capacity and owner restart preserve stale handle rejection", "[rhi][raster]")
{
    Session session;
    SamplerHandle samplers[internal::RASTER_CAPACITY];
    for (auto& sampler : samplers)
    {
        REQUIRE(CreateSampler(session.Device, {}, sampler) == RasterStatus::Ready);
    }
    SamplerHandle extra;
    CHECK(CreateSampler(session.Device, {}, extra) == RasterStatus::CapacityExceeded);
    const auto stale = samplers[0];
    const auto owner = session.Device;
    REQUIRE(Destroy(session.Device, samplers[0]) == RasterStatus::Ready);
    REQUIRE(CreateSampler(session.Device, {}, extra) == RasterStatus::Ready);
    CHECK(GetStatus(session.Device, stale) == RasterStatus::InvalidHandle);
    REQUIRE(DestroyDevice(session.Device) == DeviceStatus::Ready);
    session.Surface = {};
    REQUIRE(CreateDevice({}, {}, {}, session.Device, session.Surface) == DeviceStatus::Pending);
    internal::Complete(backend::PendingToken, StartupError::None, {4096, 16384});
    CHECK(GetStatus(owner, extra) == RasterStatus::InvalidHandle);
    CHECK(GetStatus(session.Device, extra) == RasterStatus::InvalidHandle);
}
TEST_CASE("Fixed restart indices and forged layout substitution issue no native draw", "[rhi][raster]")
{
    Session session;
    Scene scene(session.Device);
    const uint16 values[3]{0, 1, 65535};
    BufferHandle indices;
    REQUIRE(CreateBuffer(session.Device, {BufferRole::Index16, sizeof(values)}, Bytes(values), indices) ==
            RasterStatus::Ready);
    REQUIRE(BeginFrame(session.Device, session.Surface) == DeviceStatus::Ready);
    auto draw = scene.Draw();
    draw.Indices = indices;
    draw.VertexCount = 65536;
    CHECK(DrawIndexed(session.Device, draw) == RasterStatus::InvalidDescription);
    CHECK(reference::Draws == 0);
    REQUIRE(EndFrame(session.Device) == DeviceStatus::Ready);
    const RasterBinding binding{0, RasterBindingKind::UniformBuffer, RasterVisibility::Both, 16};
    BindingLayoutHandle equivalent;
    BindingSetHandle other;
    REQUIRE(CreateBindingLayout(session.Device, {&binding, 1}, equivalent) == RasterStatus::Ready);
    const RasterBindingResource resource{ .Buffer = scene.Uniform, .Size = 16 };
    REQUIRE(CreateBindingSet(session.Device, equivalent, {&resource, 1}, other) == RasterStatus::Ready);
    REQUIRE(BeginFrame(session.Device, session.Surface) == DeviceStatus::Ready);
    draw = scene.Draw();
    draw.Bindings = other;
    CHECK(DrawIndexed(session.Device, draw) == RasterStatus::InvalidDescription);
    CHECK(reference::Draws == 0);
    REQUIRE(EndFrame(session.Device) == DeviceStatus::Ready);
}

TEST_CASE("Indexed indirect packets reject wrong roles, misalignment, truncation and stale owners", "[rhi][compute]")
{
    Session session;
    Scene scene(session.Device);
    const uint8 bytes[20]{};
    BufferHandle arguments;
    REQUIRE(CreateBuffer(session.Device, {BufferRole::StorageIndirect, sizeof(bytes)}, bytes, arguments) ==
            RasterStatus::Ready);
    REQUIRE(BeginFrame(session.Device, session.Surface) == DeviceStatus::Ready);
    auto draw = scene.Draw();
    draw.Indirect = scene.Uniform;
    CHECK(DrawIndexed(session.Device, draw) == RasterStatus::InvalidDescription);
    draw.Indirect = arguments;
    draw.IndirectOffset = 1;
    CHECK(DrawIndexed(session.Device, draw) == RasterStatus::InvalidDescription);
    draw.IndirectOffset = 4;
    CHECK(DrawIndexed(session.Device, draw) == RasterStatus::InvalidDescription);
    CHECK(reference::Draws == 0);
    draw.IndirectOffset = 0;
    REQUIRE(DrawIndexed(session.Device, draw) == RasterStatus::Ready);
    CHECK(reference::Draws == 1);
    REQUIRE(EndFrame(session.Device) == DeviceStatus::Ready);
    REQUIRE(Destroy(session.Device, arguments) == RasterStatus::Ready);
    REQUIRE(BeginFrame(session.Device, session.Surface) == DeviceStatus::Ready);
    CHECK(DrawIndexed(session.Device, draw) == RasterStatus::InvalidHandle);
    CHECK(reference::Draws == 1);
    REQUIRE(EndFrame(session.Device) == DeviceStatus::Ready);
}

TEST_CASE("Completion-observed device loss closes resource admission before native work", "[rhi][raster]")
{
    Session session;
    const uint8 bytes[16]{};
    BufferHandle buffer;
    REQUIRE(CreateBuffer(session.Device, {BufferRole::Uniform, 16}, bytes, buffer) == RasterStatus::Ready);
    reference::LossOnCompletion = backend::PendingToken;
    SECTION("Polling returns the owning device loss")
    {
        CHECK(GetStatus(session.Device, buffer) == RasterStatus::DeviceLost);
    }
    SECTION("Creation cannot call the torn-down backend")
    {
        SamplerHandle sampler;
        CHECK(CreateSampler(session.Device, {}, sampler) == RasterStatus::DeviceLost);
        CHECK(reference::Creates[3] == 0);
    }
    DeviceInfo info;
    CHECK(GetDeviceInfo(session.Device, info) == DeviceStatus::DeviceLost);
    CHECK(reference::Destroys[0] == 1);
    REQUIRE(DestroyDevice(session.Device) == DeviceStatus::Ready);
}
TEST_CASE("Retained batches survive detached resources until discard or GPU completion", "[rhi][lifetime]")
{
    Session session;
    Scene scene(session.Device);
    CommandBatch batch;
    REQUIRE(BeginCommands(session.Device, batch) == RasterStatus::Ready);
    REQUIRE(RecordDraw(session.Device, batch, scene.Draw()) == RasterStatus::Ready);
    REQUIRE(reference::Draws == 0);
    REQUIRE(Destroy(session.Device, scene.Pipeline) == RasterStatus::Ready);
    REQUIRE(Destroy(session.Device, scene.Set) == RasterStatus::Ready);
    REQUIRE(Destroy(session.Device, scene.Uniform) == RasterStatus::Ready);
    REQUIRE(Destroy(session.Device, scene.Vertices) == RasterStatus::Ready);
    REQUIRE(Destroy(session.Device, scene.Indices) == RasterStatus::Ready);
    REQUIRE(Destroy(session.Device, scene.Vertex) == RasterStatus::Ready);
    REQUIRE(Destroy(session.Device, scene.Fragment) == RasterStatus::Ready);
    REQUIRE(Destroy(session.Device, scene.Layout) == RasterStatus::Ready);
    for (auto count : reference::Destroys)
    {
        CHECK(count == 0);
    }
    REQUIRE(FinishCommands(session.Device, batch) == RasterStatus::Ready);
    CHECK(FinishCommands(session.Device, batch) == RasterStatus::InvalidState);
    CHECK(RecordDraw(session.Device, batch, scene.Draw()) == RasterStatus::InvalidState);
    auto stale = batch;
    SECTION("Discard releases the dependency graph without GPU work")
    {
        REQUIRE(DiscardCommands(session.Device, batch) == RasterStatus::Ready);
        CHECK(reference::Draws == 0);
        CHECK(reference::Submitted == 0);
    }
    SECTION("Skipped target preserves work and submission retains it until completion")
    {
        SubmissionToken completion;
        backend::NextFrame = FrameStatus::Skipped;
        CHECK(SubmitCommands(session.Device, session.Surface, batch, completion) == RasterStatus::NotReady);
        CHECK(GetStatus(session.Device, completion) == RasterStatus::InvalidHandle);
        backend::NextFrame = FrameStatus::Ready;
        REQUIRE(SubmitCommands(session.Device, session.Surface, batch, completion) == RasterStatus::Ready);
        CHECK(reference::Draws == 1);
        CHECK(GetStatus(session.Device, completion) == RasterStatus::Pending);
        for (auto count : reference::Destroys)
        {
            CHECK(count == 0);
        }
        reference::Completed = reference::Submitted;
        CHECK(GetStatus(session.Device, completion) == RasterStatus::Ready);
    }
    CHECK(DiscardCommands(session.Device, stale) == RasterStatus::InvalidHandle);
    CHECK(reference::Destroys[0] == 3);
    CHECK(reference::Destroys[4] == 2);
    CHECK(reference::Destroys[5] == 1);
    CHECK(reference::Destroys[6] == 1);
    CHECK(reference::Destroys[7] == 1);
}
TEST_CASE("Batch budgets and clear-only completion reservation preserve retry state", "[rhi][lifetime]")
{
    Session session;
    Scene scene(session.Device);
    LifetimeCapabilities limits;
    REQUIRE(GetLifetimeCapabilities(session.Device, limits) == RasterStatus::Ready);
    CommandBatch batches[4];
    for (auto& batch : batches)
    {
        REQUIRE(BeginCommands(session.Device, batch) == RasterStatus::Ready);
    }
    CommandBatch extra;
    CHECK(BeginCommands(session.Device, extra) == RasterStatus::CapacityExceeded);
    for (uint32 i = 0; i < limits.DrawsPerBatch; ++i)
    {
        REQUIRE(RecordDraw(session.Device, batches[0], scene.Draw()) == RasterStatus::Ready);
    }
    CHECK(RecordDraw(session.Device, batches[0], scene.Draw()) == RasterStatus::CapacityExceeded);
    for (auto& batch : batches)
    {
        REQUIRE(DiscardCommands(session.Device, batch) == RasterStatus::Ready);
    }
    REQUIRE(BeginFrame(session.Device, session.Surface) == DeviceStatus::Ready);
    SubmissionToken first;
    reference::Submission = RasterStatus::CapacityExceeded;
    CHECK(EndFrame(session.Device, first) == RasterStatus::CapacityExceeded);
    CHECK(GetStatus(session.Device, first) == RasterStatus::InvalidHandle);
    reference::Submission = RasterStatus::Ready;
    REQUIRE(EndFrame(session.Device, first) == RasterStatus::Ready);
    CHECK(GetStatus(session.Device, first) == RasterStatus::Pending);
    REQUIRE(BeginFrame(session.Device, session.Surface) == DeviceStatus::Ready);
    SubmissionToken second;
    REQUIRE(EndFrame(session.Device, second) == RasterStatus::Ready);
    reference::Completed = reference::Submitted;
    CHECK(GetStatus(session.Device, first) == RasterStatus::Ready);
    CHECK(GetStatus(session.Device, second) == RasterStatus::Ready);
    CHECK(GetStatus({}, first) == RasterStatus::InvalidHandle);
}
TEST_CASE("Upload rings copy caller bytes and cancelled slots await true completion", "[rhi][lifetime]")
{
    Session session;
    uint8 bytes[16]{1, 2, 3, 4};
    UploadTicket tickets[4];
    for (auto& ticket : tickets)
    {
        REQUIRE(
            RequestBufferUpload(session.Device, {BufferRole::Uniform, sizeof(bytes)}, bytes, sizeof(bytes), ticket) ==
            RasterStatus::Pending);
    }
    bytes[0] = 99;
    UploadTicket extra;
    CHECK(RequestBufferUpload(session.Device, {BufferRole::Uniform, sizeof(bytes)}, bytes, sizeof(bytes), extra) ==
          RasterStatus::CapacityExceeded);
    auto stale = tickets[0];
    REQUIRE(Release(session.Device, tickets[0]) == RasterStatus::Ready);
    CHECK(GetStatus(session.Device, stale) == RasterStatus::InvalidHandle);
    CHECK(reference::Destroys[0] == 0);
    CHECK(RequestBufferUpload(session.Device, {BufferRole::Uniform, sizeof(bytes)}, bytes, sizeof(bytes), extra) ==
          RasterStatus::CapacityExceeded);
    reference::Transfers[0][0] = RasterStatus::Ready;
    REQUIRE(PollLifetime(session.Device) == RasterStatus::Ready);
    CHECK(reference::Destroys[0] == 1);
    REQUIRE(RequestBufferUpload(session.Device, {BufferRole::Uniform, sizeof(bytes)}, bytes, sizeof(bytes), extra) ==
            RasterStatus::Pending);
    reference::Transfers[0][1] = RasterStatus::Ready;
    REQUIRE(GetStatus(session.Device, tickets[1]) == RasterStatus::Ready);
    BufferHandle buffer;
    REQUIRE(TakeUploadedBuffer(session.Device, tickets[1], buffer) == RasterStatus::Ready);
    ReadbackTicket readback;
    REQUIRE(RequestBufferReadback(session.Device, buffer, 0, 16, readback) == RasterStatus::Pending);
    reference::Transfers[1][0] = RasterStatus::Ready;
    uint8 result[16]{};
    REQUIRE(CopyReadback(session.Device, readback, result, sizeof(result)) == RasterStatus::Ready);
    CHECK(result[0] == 1);
    REQUIRE(Release(session.Device, readback) == RasterStatus::Ready);
    REQUIRE(Destroy(session.Device, buffer) == RasterStatus::Ready);
}
TEST_CASE("Upload validation cancellation and memory failures retain the right obligations", "[rhi][lifetime]")
{
    Session session;
    const uint16 indices[3]{0, 1, 2};
    UploadTicket ticket;
    const auto* bytes = reinterpret_cast<const uint8*>(indices);
    CHECK(RequestBufferUpload(session.Device, {BufferRole::Vertex, 6}, bytes, 6, ticket) ==
          RasterStatus::InvalidDescription);
    reference::TransferStart = RasterStatus::OutOfMemory;
    CHECK(RequestBufferUpload(session.Device, {BufferRole::Index16, 6}, bytes, 6, ticket) == RasterStatus::OutOfMemory);
    CHECK(GetStatus(session.Device, ticket) == RasterStatus::InvalidHandle);
    reference::TransferStart = RasterStatus::Ready;
    reference::Next = RasterStatus::Pending;
    REQUIRE(RequestBufferUpload(session.Device, {BufferRole::Index16, 6}, bytes, 6, ticket) == RasterStatus::Pending);
    const auto request = reference::LastRequest;
    REQUIRE(Release(session.Device, ticket) == RasterStatus::Ready);
    const auto destroyed = reference::Destroys[0];
    reference::Transfers[0][0] = RasterStatus::Ready;
    REQUIRE(PollLifetime(session.Device) == RasterStatus::Ready);
    CHECK(reference::Destroys[0] == destroyed);
    internal::RasterComplete(request, RasterStatus::OutOfMemory);
    REQUIRE(PollLifetime(session.Device) == RasterStatus::Ready);
    CHECK(reference::Destroys[0] == destroyed + 1);
    reference::Next = RasterStatus::Ready;
    REQUIRE(RequestBufferUpload(session.Device, {BufferRole::Index16, 6}, bytes, 6, ticket) == RasterStatus::Pending);
    reference::Transfers[0][0] = RasterStatus::Ready;
    BufferHandle buffer;
    REQUIRE(TakeUploadedBuffer(session.Device, ticket, buffer) == RasterStatus::Ready);
    CHECK(reference::BufferSizes[0] == 8);
    CHECK(reference::BufferBytes[0][6] == 0);
    CHECK(reference::BufferBytes[0][7] == 0);
}
TEST_CASE("Readback owns detached sources and preserves output on pending and failed copies", "[rhi][lifetime]")
{
    Session session;
    const uint8 bytes[16]{11, 22, 33, 44};
    BufferHandle source;
    REQUIRE(CreateBuffer(session.Device, {BufferRole::Uniform, 16}, bytes, source) == RasterStatus::Ready);
    ReadbackTicket tickets[4];
    CHECK(RequestBufferReadback(session.Device, source, 1, 4, tickets[0]) == RasterStatus::InvalidDescription);
    CHECK(RequestBufferReadback(session.Device, source, 0, 20, tickets[0]) == RasterStatus::InvalidDescription);
    for (auto& ticket : tickets)
    {
        REQUIRE(RequestBufferReadback(session.Device, source, 0, 16, ticket) == RasterStatus::Pending);
    }
    ReadbackTicket extra;
    CHECK(RequestBufferReadback(session.Device, source, 0, 16, extra) == RasterStatus::CapacityExceeded);
    REQUIRE(Destroy(session.Device, source) == RasterStatus::Ready);
    CHECK(reference::Destroys[0] == 0);
    uint8 output[16]{99};
    CHECK(CopyReadback(session.Device, tickets[0], output, sizeof(output)) == RasterStatus::Pending);
    CHECK(output[0] == 99);
    reference::Transfers[1][0] = RasterStatus::Ready;
    CHECK(CopyReadback(session.Device, tickets[0], output, 15) == RasterStatus::InvalidDescription);
    reference::ReadbackCopy = RasterStatus::Failed;
    CHECK(CopyReadback(session.Device, tickets[0], output, 16) == RasterStatus::Failed);
    CHECK(output[0] == 99);
    reference::ReadbackCopy = RasterStatus::Ready;
    REQUIRE(CopyReadback(session.Device, tickets[0], output, 16) == RasterStatus::Ready);
    CHECK(output[0] == 11);
    for (auto& ticket : tickets)
    {
        REQUIRE(Release(session.Device, ticket) == RasterStatus::Ready);
    }
    CHECK(reference::Destroys[0] == 0);
    for (auto& status : reference::Transfers[1])
    {
        status = RasterStatus::Ready;
    }
    REQUIRE(PollLifetime(session.Device) == RasterStatus::Ready);
    CHECK(reference::Destroys[0] == 1);
}
TEST_CASE("Pipeline requests deduplicate fixed state and independently retain borrowed results", "[rhi][lifetime]")
{
    Session session;
    Scene scene(session.Device);
    const RasterVertexStream stream{12, false};
    const RasterVertexAttribute attribute{0, 0, 0, RasterVertexFormat::Float3};
    const RasterPipelineDescription description{scene.Vertex,
                                                scene.Fragment,
                                                scene.Layout,
                                                {&stream, 1},
                                                {&attribute, 1},
                                                true};
    const auto created = reference::Creates[7];
    PipelineRequest first, second;
    REQUIRE(RequestPipeline(session.Device, description, first) == RasterStatus::Pending);
    REQUIRE(RequestPipeline(session.Device, description, second) == RasterStatus::Pending);
    CHECK(reference::Creates[7] == created);
    REQUIRE(GetStatus(session.Device, first) == RasterStatus::Ready);
    REQUIRE(GetStatus(session.Device, second) == RasterStatus::Ready);
    CHECK(reference::Creates[7] == created + 1);
    RasterPipelineHandle pipeline;
    REQUIRE(GetRequestedPipeline(session.Device, first, pipeline) == RasterStatus::Ready);
    CHECK(Destroy(session.Device, pipeline) == RasterStatus::InvalidState);
    auto stale = first;
    REQUIRE(Release(session.Device, first) == RasterStatus::Ready);
    CHECK(GetStatus(session.Device, stale) == RasterStatus::InvalidHandle);
    CHECK(GetStatus(session.Device, pipeline) == RasterStatus::Ready);
    auto draw = scene.Draw();
    draw.Pipeline = pipeline;
    CommandBatch batch;
    REQUIRE(BeginCommands(session.Device, batch) == RasterStatus::Ready);
    REQUIRE(RecordDraw(session.Device, batch, draw) == RasterStatus::Ready);
    REQUIRE(Release(session.Device, second) == RasterStatus::Ready);
    CHECK(GetStatus(session.Device, pipeline) == RasterStatus::InvalidHandle);
    CHECK(reference::Destroys[7] == 0);
    REQUIRE(FinishCommands(session.Device, batch) == RasterStatus::Ready);
    SubmissionToken token;
    REQUIRE(SubmitCommands(session.Device, session.Surface, batch, token) == RasterStatus::Ready);
    reference::Completed = reference::Submitted;
    REQUIRE(GetStatus(session.Device, token) == RasterStatus::Ready);
    CHECK(reference::Destroys[7] == 1);
}
TEST_CASE("Pipeline pending failure cancellation and request capacity remain explicit", "[rhi][lifetime]")
{
    Session session;
    Scene scene(session.Device);
    const RasterVertexStream stream{12, false};
    const RasterVertexAttribute attribute{0, 0, 0, RasterVertexFormat::Float3};
    RasterPipelineDescription description{scene.Vertex,
                                          scene.Fragment,
                                          scene.Layout,
                                          {&stream, 1},
                                          {&attribute, 1},
                                          true};
    PipelineRequest first;
    REQUIRE(RequestPipeline(session.Device, description, first) == RasterStatus::Pending);
    const auto created = reference::Creates[7];
    REQUIRE(Release(session.Device, first) == RasterStatus::Ready);
    REQUIRE(PollLifetime(session.Device) == RasterStatus::Ready);
    CHECK(reference::Creates[7] == created);
    PipelineRequest requests[internal::RASTER_CAPACITY];
    for (auto& request : requests)
    {
        REQUIRE(RequestPipeline(session.Device, description, request) == RasterStatus::Pending);
    }
    PipelineRequest extra;
    CHECK(RequestPipeline(session.Device, description, extra) == RasterStatus::CapacityExceeded);
    reference::Next = RasterStatus::Pending;
    REQUIRE(GetStatus(session.Device, requests[0]) == RasterStatus::Pending);
    const auto callback = reference::LastRequest;
    internal::RasterComplete(callback, RasterStatus::OutOfMemory);
    CHECK(GetStatus(session.Device, requests[1]) == RasterStatus::OutOfMemory);
    RasterPipelineHandle pipeline;
    CHECK(GetRequestedPipeline(session.Device, requests[1], pipeline) == RasterStatus::OutOfMemory);
    for (auto& request : requests)
    {
        REQUIRE(Release(session.Device, request) == RasterStatus::Ready);
    }
    reference::Next = RasterStatus::Ready;
    description.Blend = true;
    REQUIRE(RequestPipeline(session.Device, description, first) == RasterStatus::Pending);
    internal::RasterComplete(callback, RasterStatus::Ready);
    CHECK(GetStatus(session.Device, first) == RasterStatus::Ready);
}
TEST_CASE("Loss and restart invalidate every lifetime identity", "[rhi][lifetime]")
{
    Session session;
    const uint8 bytes[16]{};
    UploadTicket upload;
    REQUIRE(RequestBufferUpload(session.Device, {BufferRole::Uniform, 16}, bytes, 16, upload) == RasterStatus::Pending);
    CommandBatch batch;
    REQUIRE(BeginCommands(session.Device, batch) == RasterStatus::Ready);
    REQUIRE(BeginFrame(session.Device, session.Surface) == DeviceStatus::Ready);
    SubmissionToken completion;
    REQUIRE(EndFrame(session.Device, completion) == RasterStatus::Ready);
    internal::Fail(backend::PendingToken, StartupError::DeviceLost);
    CHECK(GetStatus(session.Device, upload) == RasterStatus::DeviceLost);
    CHECK(GetStatus(session.Device, completion) == RasterStatus::DeviceLost);
    Shutdown();
    Session replacement;
    CHECK(GetStatus(replacement.Device, upload) == RasterStatus::InvalidHandle);
    CHECK(GetStatus(replacement.Device, completion) == RasterStatus::InvalidHandle);
    CHECK(DiscardCommands(replacement.Device, batch) == RasterStatus::InvalidHandle);
}
TEST_CASE("Restart admits free transfer slices while old backend callbacks retain other cells", "[rhi][lifetime]")
{
    Session session;
    bool readback = false;
    SECTION("Upload")
    {
        readback = false;
    }
    SECTION("Readback")
    {
        readback = true;
    }
    const uint8 bytes[16]{1, 2, 3, 4};
    BufferHandle source;
    REQUIRE(CreateBuffer(session.Device, {BufferRole::Uniform, 16}, bytes, source) == RasterStatus::Ready);
    const auto request = [&](UploadTicket& upload, ReadbackTicket& read) {
        return readback ? RequestBufferReadback(session.Device, source, 0, 16, read)
                        : RequestBufferUpload(session.Device, {BufferRole::Uniform, 16}, bytes, 16, upload);
    };
    UploadTicket oldUpload;
    ReadbackTicket oldReadback;
    REQUIRE(request(oldUpload, oldReadback) == RasterStatus::Pending);
    reference::RetainTransfersOnReset = true;
    REQUIRE(DestroyDevice(session.Device) == DeviceStatus::Ready);
    session.Surface = {};
    REQUIRE(CreateDevice({}, {}, {}, session.Device, session.Surface) == DeviceStatus::Pending);
    internal::Complete(backend::PendingToken, StartupError::None, {4096, 16384});
    CHECK((readback ? GetStatus(session.Device, oldReadback) : GetStatus(session.Device, oldUpload)) ==
          RasterStatus::InvalidHandle);
    source = {};
    REQUIRE(CreateBuffer(session.Device, {BufferRole::Uniform, 16}, bytes, source) == RasterStatus::Ready);
    UploadTicket uploads[4];
    ReadbackTicket reads[4];
    for (usize i = 0; i < 3; ++i)
    {
        REQUIRE(request(uploads[i], reads[i]) == RasterStatus::Pending);
    }
    CHECK(reference::Copies == 4);
    CHECK(request(uploads[3], reads[3]) == RasterStatus::CapacityExceeded);
    const usize direction = readback ? 1 : 0;
    // An old-session callback drains only its own physical cell. Slot zero can
    // now accept new work without changing the three new-session requests.
    reference::TransferOccupied[direction][0] = false;
    REQUIRE(request(uploads[3], reads[3]) == RasterStatus::Pending);
    CHECK(reference::Copies == 5);
    for (auto& status : reference::Transfers[direction])
    {
        status = RasterStatus::Ready;
    }
    if (readback)
    {
        uint8 actual[16]{};
        REQUIRE(CopyReadback(session.Device, reads[0], actual, sizeof(actual)) == RasterStatus::Ready);
        CHECK(std::memcmp(actual, bytes, sizeof(bytes)) == 0);
    }
    for (usize i = 0; i < 4; ++i)
    {
        REQUIRE((readback ? Release(session.Device, reads[i]) : Release(session.Device, uploads[i])) ==
                RasterStatus::Ready);
    }
    reference::RetainTransfersOnReset = false;
}
TEST_CASE("Pipeline cache canonicalizes attributes and distinguishes immutable fixed state", "[rhi][lifetime]")
{
    Session session;
    Scene scene(session.Device);
    const RasterBinding binding{0, RasterBindingKind::UniformBuffer, RasterVisibility::Both, 16};
    const RasterShaderInput inputs[2]{{0, RasterVertexFormat::Float3}, {1, RasterVertexFormat::Float2}};
    RasterShaderDescription shader;
    shader.Artifact.Stage = ShaderStage::Vertex;
    shader.Artifact.Wgsl = "two input fixture";
    shader.Artifact.WgslEntry = "vertexMain";
    shader.Inputs = inputs;
    shader.Bindings = {&binding, 1};
    RasterShaderHandle vertex;
    REQUIRE(CreateRasterShader(session.Device, shader, vertex) == RasterStatus::Ready);
    const RasterVertexStream stream{20, false};
    const RasterVertexAttribute forward[2]{{0, 0, 0, RasterVertexFormat::Float3},
                                           {1, 0, 12, RasterVertexFormat::Float2}};
    const RasterVertexAttribute reverse[2]{forward[1], forward[0]};
    RasterPipelineDescription description{vertex, scene.Fragment, scene.Layout, {&stream, 1}, forward, true};
    PipelineRequest a, b, c, d;
    const auto created = reference::Creates[7];
    REQUIRE(RequestPipeline(session.Device, description, a) == RasterStatus::Pending);
    description.Attributes = reverse;
    REQUIRE(RequestPipeline(session.Device, description, b) == RasterStatus::Pending);
    REQUIRE(GetStatus(session.Device, a) == RasterStatus::Ready);
    CHECK(reference::Creates[7] == created + 1);
    description.Depth = false;
    REQUIRE(RequestPipeline(session.Device, description, c) == RasterStatus::Pending);
    REQUIRE(GetStatus(session.Device, c) == RasterStatus::Ready);
    description.Blend = true;
    REQUIRE(RequestPipeline(session.Device, description, d) == RasterStatus::Pending);
    REQUIRE(GetStatus(session.Device, d) == RasterStatus::Ready);
    CHECK(reference::Creates[7] == created + 3);
    REQUIRE(Release(session.Device, a) == RasterStatus::Ready);
    REQUIRE(Release(session.Device, b) == RasterStatus::Ready);
    REQUIRE(Release(session.Device, c) == RasterStatus::Ready);
    REQUIRE(Release(session.Device, d) == RasterStatus::Ready);
}
TEST_CASE("Cancelled pipeline callbacks retain detached shader and layout dependencies", "[rhi][lifetime]")
{
    Session session;
    Scene scene(session.Device);
    const RasterVertexStream stream{12, false};
    const RasterVertexAttribute attribute{0, 0, 0, RasterVertexFormat::Float3};
    PipelineRequest request;
    REQUIRE(RequestPipeline(session.Device,
                            {scene.Vertex, scene.Fragment, scene.Layout, {&stream, 1}, {&attribute, 1}, true},
                            request) == RasterStatus::Pending);
    reference::Next = RasterStatus::Pending;
    REQUIRE(PollLifetime(session.Device) == RasterStatus::Ready);
    const auto callback = reference::LastRequest;
    REQUIRE(Release(session.Device, request) == RasterStatus::Ready);
    REQUIRE(Destroy(session.Device, scene.Pipeline) == RasterStatus::Ready);
    REQUIRE(Destroy(session.Device, scene.Set) == RasterStatus::Ready);
    REQUIRE(Destroy(session.Device, scene.Vertex) == RasterStatus::Ready);
    REQUIRE(Destroy(session.Device, scene.Fragment) == RasterStatus::Ready);
    REQUIRE(Destroy(session.Device, scene.Layout) == RasterStatus::Ready);
    CHECK(reference::Destroys[4] == 0);
    CHECK(reference::Destroys[5] == 0);
    CHECK(reference::Destroys[7] == 1);
    internal::RasterComplete(callback, RasterStatus::Failed);
    REQUIRE(PollLifetime(session.Device) == RasterStatus::Ready);
    CHECK(reference::Destroys[4] == 2);
    CHECK(reference::Destroys[5] == 1);
    CHECK(reference::Destroys[7] == 2);
}

namespace
{
struct GraphScene final
{
    OrderedGraph Graph;
    GraphVersion Vertex, Index, Uniform;
    GraphScene(DeviceHandle device, const Scene& scene)
    {
        REQUIRE(CreateOrderedGraph(device, Graph) == RasterStatus::Ready);
        REQUIRE(ImportGraphBuffer(device, Graph, scene.Vertices, Vertex) == RasterStatus::Ready);
        REQUIRE(ImportGraphBuffer(device, Graph, scene.Indices, Index) == RasterStatus::Ready);
        REQUIRE(ImportGraphBuffer(device, Graph, scene.Uniform, Uniform) == RasterStatus::Ready);
    }
    void Add(DeviceHandle device, const Scene& scene, bool uniform = true)
    {
        const GraphUse uses[]{{Vertex, GraphAccessMode::Vertex, 0, 36},
                              {Index, GraphAccessMode::Index, 0, 6},
                              {Uniform, GraphAccessMode::Uniform, 0, 16}};
        const auto draw = scene.Draw();
        GraphPassDescription pass;
        pass.Name = "scene";
        pass.Source = "fixture.cpp";
        pass.Line = 17;
        pass.Uses = uses;
        pass.UseCount = uniform ? 3 : 2;
        pass.Draws = &draw;
        pass.DrawCount = 1;
        REQUIRE(AddGraphPass(device, Graph, pass) == RasterStatus::Ready);
    }
};
struct GraphTexture final
{
    TextureHandle Texture;
    GraphVersion Initial, Output;
    GraphTexture(DeviceHandle device, OrderedGraph graph, bool pooled = false)
    {
        if (pooled)
        {
            REQUIRE(CreateGraphTexture(device, graph, {16, 8, RasterFormat::Rgba8Unorm, true}, Initial, Texture) ==
                    RasterStatus::Ready);
        }
        else
        {
            REQUIRE(CreateTexture(device, {16, 8, RasterFormat::Rgba8Unorm, true}, {}, Texture) == RasterStatus::Ready);
            RasterTextureState state;
            REQUIRE(GetTextureState(device, Texture, state) == RasterStatus::Ready);
            REQUIRE_FALSE(state.ColorDefined);
            REQUIRE(ImportGraphTexture(device, graph, Texture, state, RasterTextureUse::SampledFragment, Initial) ==
                    RasterStatus::Ready);
        }
        REQUIRE(NextGraphVersion(device, graph, Initial, Output) == RasterStatus::Ready);
    }
    void Add(DeviceHandle device,
             OrderedGraph graph,
             RasterLoad load = RasterLoad::Clear,
             RasterStore store = RasterStore::Store,
             bool sideEffect = false) const
    {
        const GraphUse use{Output,
                           load == RasterLoad::Load ? GraphAccessMode::ColorReadWrite : GraphAccessMode::ColorWrite};
        GraphPassDescription pass;
        pass.Name = "offscreen";
        pass.Attachment.Color = Texture;
        pass.Attachment.ColorLoad = load;
        pass.Attachment.ColorStore = store;
        pass.Uses = &use;
        pass.UseCount = 1;
        pass.SideEffect = sideEffect;
        REQUIRE(AddGraphPass(device, graph, pass) == RasterStatus::Ready);
    }
};
} // namespace
TEST_CASE("Ordered graph checks complete packet declarations before effects", "[rhi][graph]")
{
    Session session;
    Scene scene(session.Device);
    GraphScene graph(session.Device, scene);
    SECTION("Missing reflected uniform use")
    {
        graph.Add(session.Device, scene, false);
        GraphReport report;
        CHECK(CompileOrderedGraph(session.Device, graph.Graph, report) == RasterStatus::InvalidDescription);
        CHECK(report.ErrorPass == 0);
        CHECK(reference::Draws == 0);
        CHECK(reference::Submitted == 0);
    }
    SECTION("Accepted packets retain destroyed public owners")
    {
        graph.Add(session.Device, scene);
        REQUIRE(Destroy(session.Device, scene.Pipeline) == RasterStatus::Ready);
        REQUIRE(Destroy(session.Device, scene.Set) == RasterStatus::Ready);
        REQUIRE(Destroy(session.Device, scene.Vertices) == RasterStatus::Ready);
        REQUIRE(Destroy(session.Device, scene.Indices) == RasterStatus::Ready);
        REQUIRE(Destroy(session.Device, scene.Uniform) == RasterStatus::Ready);
        GraphReport report;
        REQUIRE(CompileOrderedGraph(session.Device, graph.Graph, report) == RasterStatus::Ready);
        CHECK(report.LivePasses == 1);
        CHECK(std::strcmp(report.Names[0], "scene") == 0);
        CHECK(std::strcmp(report.Sources[0], "fixture.cpp") == 0);
        CHECK(report.Lines[0] == 17);
        CHECK(report.WholeBufferHazards);
        SubmissionToken completed;
        REQUIRE(ExecuteOrderedGraph(session.Device, session.Surface, graph.Graph, completed) == RasterStatus::Ready);
        CHECK(reference::Draws == 1);
        CHECK(GetStatus(session.Device, completed) == RasterStatus::Pending);
        CHECK(DiscardOrderedGraph(session.Device, graph.Graph) == RasterStatus::InvalidHandle);
        reference::Completed = reference::Submitted;
        CHECK(GetStatus(session.Device, completed) == RasterStatus::Ready);
    }
}
TEST_CASE("Ordered graph versions produce RAW WAR WAW without reordering", "[rhi][graph]")
{
    Session session;
    OrderedGraph graph;
    REQUIRE(CreateOrderedGraph(session.Device, graph) == RasterStatus::Ready);
    GraphTexture texture(session.Device, graph);
    texture.Add(session.Device, graph);
    const GraphUse sampled{texture.Output, GraphAccessMode::Sampled, 0, 0, RasterVisibility::Fragment};
    GraphPassDescription composite;
    composite.Name = "composite";
    composite.Uses = &sampled;
    composite.UseCount = 1;
    REQUIRE(AddGraphPass(session.Device, graph, composite) == RasterStatus::Ready);
    GraphVersion overwritten;
    REQUIRE(NextGraphVersion(session.Device, graph, texture.Output, overwritten) == RasterStatus::Ready);
    texture.Output = overwritten;
    texture.Add(session.Device, graph);
    REQUIRE(AddGraphRoot(session.Device, graph, overwritten, GraphRoot::History) == RasterStatus::Ready);
    GraphReport report;
    REQUIRE(CompileOrderedGraph(session.Device, graph, report) == RasterStatus::Ready);
    CHECK(report.LivePasses == 3);
    bool raw = false, war = false, waw = false;
    for (usize i = 0; i < report.DependencyCount; ++i)
    {
        const auto& edge = report.Dependencies[i];
        raw |= edge.Before == 0 && edge.After == 1 && edge.Hazard == GraphHazard::ReadAfterWrite;
        war |= edge.Before == 1 && edge.After == 2 && edge.Hazard == GraphHazard::WriteAfterRead;
        waw |= edge.Before == 0 && edge.After == 2 && edge.Hazard == GraphHazard::WriteAfterWrite;
    }
    CHECK(raw);
    CHECK(war);
    CHECK(waw);
    CHECK(report.FirstUse[0] == 0);
    CHECK(report.LastUse[0] == 2);
}
TEST_CASE("Ordered graph rejects forward reads stale versions undefined loads and roots", "[rhi][graph]")
{
    Session session;
    OrderedGraph graph;
    REQUIRE(CreateOrderedGraph(session.Device, graph) == RasterStatus::Ready);
    GraphTexture texture(session.Device, graph);
    SECTION("Forward read cannot move the producer")
    {
        const GraphUse use{texture.Output, GraphAccessMode::Sampled};
        GraphPassDescription pass;
        pass.Name = "forward";
        pass.Uses = &use;
        pass.UseCount = 1;
        REQUIRE(AddGraphPass(session.Device, graph, pass) == RasterStatus::Ready);
        texture.Add(session.Device, graph);
    }
    SECTION("Load needs defined prior contents")
    {
        texture.Add(session.Device, graph, RasterLoad::Load);
    }
    SECTION("Partial draw after Discard never establishes initialization")
    {
        texture.Add(session.Device, graph, RasterLoad::Discard);
        REQUIRE(AddGraphRoot(session.Device, graph, texture.Output, GraphRoot::History) == RasterStatus::Ready);
    }
    SECTION("Store Discard invalidates a cleared root")
    {
        texture.Add(session.Device, graph, RasterLoad::Clear, RasterStore::Discard);
        REQUIRE(AddGraphRoot(session.Device, graph, texture.Output, GraphRoot::Export) == RasterStatus::Ready);
    }
    SECTION("Overwritten versions cannot be read later")
    {
        texture.Add(session.Device, graph);
        const auto first = texture.Output;
        GraphVersion second;
        REQUIRE(NextGraphVersion(session.Device, graph, first, second) == RasterStatus::Ready);
        texture.Output = second;
        texture.Add(session.Device, graph);
        const GraphUse use{first, GraphAccessMode::Sampled};
        GraphPassDescription pass;
        pass.Name = "stale";
        pass.Uses = &use;
        pass.UseCount = 1;
        REQUIRE(AddGraphPass(session.Device, graph, pass) == RasterStatus::Ready);
    }
    SECTION("Feedback is a conflicting duplicate physical declaration")
    {
        const GraphUse uses[]{{texture.Output, GraphAccessMode::ColorWrite},
                              {texture.Initial, GraphAccessMode::Sampled}};
        GraphPassDescription pass;
        pass.Name = "feedback";
        pass.Attachment.Color = texture.Texture;
        pass.Uses = uses;
        pass.UseCount = 2;
        REQUIRE(AddGraphPass(session.Device, graph, pass) == RasterStatus::Ready);
    }
    GraphReport report;
    CHECK(CompileOrderedGraph(session.Device, graph, report) == RasterStatus::InvalidDescription);
    CHECK(reference::Passes == 0);
    CHECK(reference::Draws == 0);
    CHECK(reference::Submitted == 0);
}
TEST_CASE("Ordered graph culls only unobservable work and reference mode retains it", "[rhi][graph]")
{
    Session session;
    const bool referenceMode = GENERATE(false, true);
    OrderedGraph graph;
    REQUIRE(CreateOrderedGraph(session.Device, graph) == RasterStatus::Ready);
    GraphTexture unused(session.Device, graph);
    unused.Add(session.Device, graph);
    GraphTexture history(session.Device, graph);
    history.Add(session.Device, graph);
    REQUIRE(AddGraphRoot(session.Device, graph, history.Output, GraphRoot::History) == RasterStatus::Ready);
    GraphReport report;
    REQUIRE(CompileOrderedGraph(session.Device, graph, report, referenceMode) == RasterStatus::Ready);
    CHECK(report.LivePasses == (referenceMode ? 2 : 1));
    CHECK(report.Culled[0] == !referenceMode);
    CHECK_FALSE(report.Culled[1]);
    CHECK(report.FirstUse[0] == (referenceMode ? 0 : 32));
    SubmissionToken token;
    REQUIRE(ExecuteOrderedGraph(session.Device, session.Surface, graph, token) == RasterStatus::Ready);
    CHECK(reference::Passes == (referenceMode ? 2 : 1));
    RasterTextureState historyState, unusedState;
    REQUIRE(GetTextureState(session.Device, history.Texture, historyState) == RasterStatus::Ready);
    REQUIRE(GetTextureState(session.Device, unused.Texture, unusedState) == RasterStatus::Ready);
    CHECK(historyState.ColorDefined);
    CHECK(unusedState.ColorDefined == referenceMode);
    CHECK(historyState.Use == RasterTextureUse::SampledFragment);
}
TEST_CASE("Ordered graph preflight and skipped acquisition preserve retry while partial failure faults", "[rhi][graph]")
{
    Session session;
    OrderedGraph graph;
    REQUIRE(CreateOrderedGraph(session.Device, graph) == RasterStatus::Ready);
    GraphTexture texture(session.Device, graph);
    texture.Add(session.Device, graph, RasterLoad::Clear, RasterStore::Store, true);
    GraphReport report;
    REQUIRE(CompileOrderedGraph(session.Device, graph, report) == RasterStatus::Ready);
    SubmissionToken token;
    reference::PassPrepare = RasterStatus::OutOfMemory;
    CHECK(ExecuteOrderedGraph(session.Device, session.Surface, graph, token) == RasterStatus::OutOfMemory);
    CHECK(reference::Passes == 0);
    CHECK(reference::Submitted == 0);
    reference::PassPrepare = RasterStatus::Ready;
    backend::NextFrame = FrameStatus::Skipped;
    CHECK(ExecuteOrderedGraph(session.Device, session.Surface, graph, token) == RasterStatus::NotReady);
    backend::NextFrame = FrameStatus::Ready;
    reference::PassStart = RasterStatus::Failed;
    CHECK(ExecuteOrderedGraph(session.Device, session.Surface, graph, token) == RasterStatus::Failed);
    CHECK(GetStartup().State == StartupState::Failed);
    CHECK(reference::Submitted == 0);
}
TEST_CASE("Pending history imports share direct-submission revisions without host waits", "[rhi][graph]")
{
    Session session;
    OrderedGraph first;
    REQUIRE(CreateOrderedGraph(session.Device, first) == RasterStatus::Ready);
    GraphTexture texture(session.Device, first);
    texture.Add(session.Device, first);
    REQUIRE(AddGraphRoot(session.Device, first, texture.Output, GraphRoot::History) == RasterStatus::Ready);
    GraphReport report;
    REQUIRE(CompileOrderedGraph(session.Device, first, report) == RasterStatus::Ready);
    SubmissionToken dependency;
    REQUIRE(ExecuteOrderedGraph(session.Device, session.Surface, first, dependency) == RasterStatus::Ready);
    REQUIRE(GetStatus(session.Device, dependency) == RasterStatus::Pending);
    RasterTextureState state;
    REQUIRE(GetTextureState(session.Device, texture.Texture, state) == RasterStatus::Ready);
    OrderedGraph second;
    GraphVersion imported;
    REQUIRE(CreateOrderedGraph(session.Device, second) == RasterStatus::Ready);
    REQUIRE(ImportGraphTexture(session.Device,
                               second,
                               texture.Texture,
                               state,
                               RasterTextureUse::SampledFragment,
                               imported) == RasterStatus::Ready);
    const GraphUse use{imported, GraphAccessMode::Sampled};
    GraphPassDescription pass;
    pass.Name = "history";
    pass.Uses = &use;
    pass.UseCount = 1;
    REQUIRE(AddGraphPass(session.Device, second, pass) == RasterStatus::Ready);
    REQUIRE(CompileOrderedGraph(session.Device, second, report) == RasterStatus::Ready);
    SECTION("Same-queue pending data is ordered without waiting")
    {
        SubmissionToken token;
        REQUIRE(ExecuteOrderedGraph(session.Device, session.Surface, second, token) == RasterStatus::Ready);
        CHECK(reference::Completed == 0);
    }
    SECTION("Direct accepted work invalidates a frozen import snapshot")
    {
        REQUIRE(BeginFrame(session.Device, session.Surface) == DeviceStatus::Ready);
        RasterPassDescription direct;
        direct.Color = texture.Texture;
        REQUIRE(BeginRasterPass(session.Device, direct) == RasterStatus::Ready);
        REQUIRE(EndFrame(session.Device) == DeviceStatus::Ready);
        SubmissionToken token;
        CHECK(ExecuteOrderedGraph(session.Device, session.Surface, second, token) == RasterStatus::InvalidState);
        REQUIRE(DiscardOrderedGraph(session.Device, second) == RasterStatus::Ready);
    }
}
TEST_CASE("Transient object pooling waits for views leases and GPU completion and invalidates borrowed handles",
          "[rhi][graph]")
{
    Session session;
    OrderedGraph graph;
    REQUIRE(CreateOrderedGraph(session.Device, graph) == RasterStatus::Ready);
    GraphTexture first(session.Device, graph, true);
    TextureViewHandle view;
    REQUIRE(CreateTextureView(session.Device, first.Texture, view) == RasterStatus::Ready);
    const auto old = first.Texture;
    CHECK(Destroy(session.Device, first.Texture) == RasterStatus::InvalidState);
    first.Add(session.Device, graph, RasterLoad::Clear, RasterStore::Store, true);
    GraphReport report;
    REQUIRE(CompileOrderedGraph(session.Device, graph, report) == RasterStatus::Ready);
    SubmissionToken token;
    REQUIRE(ExecuteOrderedGraph(session.Device, session.Surface, graph, token) == RasterStatus::Ready);
    CHECK(GetStatus(session.Device, old) == RasterStatus::InvalidHandle);
    OrderedGraph held;
    REQUIRE(CreateOrderedGraph(session.Device, held) == RasterStatus::Ready);
    for (usize i = 0; i < 7; ++i)
    {
        GraphTexture other(session.Device, held, true);
    }
    GraphVersion extra;
    TextureHandle extraTexture;
    CHECK(CreateGraphTexture(session.Device, held, {16, 8, RasterFormat::Rgba8Unorm, true}, extra, extraTexture) ==
          RasterStatus::CapacityExceeded);
    reference::Completed = reference::Submitted;
    CHECK(CreateGraphTexture(session.Device, held, {16, 8, RasterFormat::Rgba8Unorm, true}, extra, extraTexture) ==
          RasterStatus::CapacityExceeded);
    REQUIRE(Destroy(session.Device, view) == RasterStatus::Ready);
    REQUIRE(CreateGraphTexture(session.Device, held, {16, 8, RasterFormat::Rgba8Unorm, true}, extra, extraTexture) ==
            RasterStatus::Ready);
    CHECK(reference::Creates[static_cast<usize>(internal::RasterKind::Texture)] == 8);
    CHECK(GetStatus(session.Device, old) == RasterStatus::InvalidHandle);
    REQUIRE(DiscardOrderedGraph(session.Device, held) == RasterStatus::Ready);
}
TEST_CASE("Graph owner identities and capacity failures never issue effects", "[rhi][graph]")
{
    Session session;
    OrderedGraph graph;
    REQUIRE(CreateOrderedGraph(session.Device, graph) == RasterStatus::Ready);
    GraphTexture texture(session.Device, graph);
    OrderedGraph foreign;
    REQUIRE(CreateOrderedGraph(session.Device, foreign) == RasterStatus::Ready);
    GraphVersion next;
    CHECK(NextGraphVersion(session.Device, foreign, texture.Initial, next) == RasterStatus::InvalidHandle);
    for (usize i = 0; i < 32; ++i)
    {
        GraphPassDescription pass;
        pass.Name = "surface";
        REQUIRE(AddGraphPass(session.Device, graph, pass) == RasterStatus::Ready);
    }
    GraphPassDescription extra;
    extra.Name = "overflow";
    CHECK(AddGraphPass(session.Device, graph, extra) == RasterStatus::CapacityExceeded);
    CHECK(reference::Passes == 0);
    REQUIRE(DiscardOrderedGraph(session.Device, graph) == RasterStatus::Ready);
    CHECK(AddGraphPass(session.Device, graph, extra) == RasterStatus::InvalidHandle);
}

TEST_CASE("Idle transient pools evict whole objects when attachment dimensions change", "[rhi][graph]")
{
    Session session;
    OrderedGraph initial;
    REQUIRE(CreateOrderedGraph(session.Device, initial) == RasterStatus::Ready);
    for (usize i = 0; i < 8; ++i)
    {
        GraphTexture texture(session.Device, initial, true);
    }
    REQUIRE(DiscardOrderedGraph(session.Device, initial) == RasterStatus::Ready);
    OrderedGraph resized;
    REQUIRE(CreateOrderedGraph(session.Device, resized) == RasterStatus::Ready);
    GraphVersion version;
    TextureHandle texture;
    REQUIRE(CreateGraphTexture(session.Device, resized, {32, 16, RasterFormat::Rgba8Unorm, true}, version, texture) ==
            RasterStatus::Ready);
    CHECK(reference::Creates[static_cast<usize>(internal::RasterKind::Texture)] == 9);
    CHECK(reference::Destroys[static_cast<usize>(internal::RasterKind::Texture)] == 1);
    RasterTextureState state;
    REQUIRE(GetTextureState(session.Device, texture, state) == RasterStatus::Ready);
    CHECK_FALSE(state.ColorDefined);
    CHECK_FALSE(state.DepthDefined);
    REQUIRE(DiscardOrderedGraph(session.Device, resized) == RasterStatus::Ready);
}

TEST_CASE("Direct draws and R2 batches reject graph imports without compatible sampled visibility", "[rhi][graph]")
{
    Session session;
    Scene scene(session.Device);
    const auto mode = GENERATE(0, 1, 2, 3);
    TextureHandle texture;
    REQUIRE(CreateTexture(session.Device, {16, 8, RasterFormat::Rgba8Unorm, true}, {}, texture) == RasterStatus::Ready);
    if (mode != 0)
    {
        OrderedGraph graph;
        GraphVersion initial, written;
        RasterTextureState state;
        REQUIRE(CreateOrderedGraph(session.Device, graph) == RasterStatus::Ready);
        REQUIRE(GetTextureState(session.Device, texture, state) == RasterStatus::Ready);
        const auto finalUse = mode == 1   ? RasterTextureUse::ColorAttachment
                              : mode == 2 ? RasterTextureUse::SampledFragment
                                          : RasterTextureUse::SampledBoth;
        REQUIRE(ImportGraphTexture(session.Device, graph, texture, state, finalUse, initial) == RasterStatus::Ready);
        REQUIRE(NextGraphVersion(session.Device, graph, initial, written) == RasterStatus::Ready);
        const GraphUse write{written, GraphAccessMode::ColorWrite};
        GraphPassDescription pass;
        pass.Name = "external state";
        pass.Attachment.Color = texture;
        pass.Uses = &write;
        pass.UseCount = 1;
        REQUIRE(AddGraphPass(session.Device, graph, pass) == RasterStatus::Ready);
        REQUIRE(AddGraphRoot(session.Device, graph, written, GraphRoot::History) == RasterStatus::Ready);
        GraphReport report;
        REQUIRE(CompileOrderedGraph(session.Device, graph, report) == RasterStatus::Ready);
        SubmissionToken completion;
        REQUIRE(ExecuteOrderedGraph(session.Device, session.Surface, graph, completion) == RasterStatus::Ready);
    }
    TextureViewHandle view;
    REQUIRE(CreateTextureView(session.Device, texture, view) == RasterStatus::Ready);
    const RasterBinding entries[]{{0, RasterBindingKind::UniformBuffer, RasterVisibility::Both, 16},
                                  {1, RasterBindingKind::Texture2D, RasterVisibility::Both, 0}};
    BindingLayoutHandle layout;
    REQUIRE(CreateBindingLayout(session.Device, entries, layout) == RasterStatus::Ready);
    const RasterBindingResource resources[]{{ .Buffer = scene.Uniform, .Size = 16 }, { .Binding = 1, .Texture = view }};
    BindingSetHandle set;
    REQUIRE(CreateBindingSet(session.Device, layout, resources, set) == RasterStatus::Ready);
    const RasterVertexStream stream{12, false};
    const RasterVertexAttribute attribute{0, 0, 0, RasterVertexFormat::Float3};
    RasterPipelineHandle pipeline;
    REQUIRE(CreateRasterPipeline(session.Device,
                                 {scene.Vertex, scene.Fragment, layout, {&stream, 1}, {&attribute, 1}, true},
                                 pipeline) == RasterStatus::Ready);
    const RasterDraw draw{pipeline, set, scene.Slices, scene.Indices, 0, 3, 3, 1};
    CommandBatch batch;
    REQUIRE(BeginCommands(session.Device, batch) == RasterStatus::Ready);
    REQUIRE(RecordDraw(session.Device, batch, draw) == RasterStatus::Ready);
    REQUIRE(FinishCommands(session.Device, batch) == RasterStatus::Ready);
    const auto before = reference::Submitted;
    const auto expected = mode == 0   ? RasterStatus::InvalidDescription
                          : mode == 3 ? RasterStatus::Ready
                                      : RasterStatus::InvalidState;
    SubmissionToken completion;
    CHECK(SubmitCommands(session.Device, session.Surface, batch, completion) == expected);
    if (mode != 3)
    {
        CHECK(reference::Submitted == before);
        CHECK(reference::Draws == 0);
        REQUIRE(DiscardCommands(session.Device, batch) == RasterStatus::Ready);
    }
    REQUIRE(BeginFrame(session.Device, session.Surface) == DeviceStatus::Ready);
    CHECK(DrawIndexed(session.Device, draw) == expected);
    CHECK(reference::Draws == (mode == 3 ? 2 : 0));
    REQUIRE(EndFrame(session.Device) == DeviceStatus::Ready);
}
TEST_CASE("L1 depth comparison participates in pipeline request identity", "[rhi][raster][lifetime]")
{
    Session session;
    Scene scene(session.Device);
    const RasterVertexStream stream{12, false};
    const RasterVertexAttribute attribute{0, 0, 0, RasterVertexFormat::Float3};
    RasterPipelineDescription description{scene.Vertex,
                                          scene.Fragment,
                                          scene.Layout,
                                          {&stream, 1},
                                          {&attribute, 1},
                                          true};
    PipelineRequest conventional, reverse, same;
    REQUIRE(RequestPipeline(session.Device, description, conventional) == RasterStatus::Pending);
    REQUIRE(GetStatus(session.Device, conventional) == RasterStatus::Ready);
    const auto created = reference::Creates[7];
    description.DepthCompare = RasterDepthCompare::Greater;
    REQUIRE(RequestPipeline(session.Device, description, reverse) == RasterStatus::Pending);
    REQUIRE(GetStatus(session.Device, reverse) == RasterStatus::Ready);
    CHECK(reference::Creates[7] == created + 1);
    REQUIRE(RequestPipeline(session.Device, description, same) == RasterStatus::Ready);
    CHECK(reference::Creates[7] == created + 1);
    description.DepthCompare = static_cast<RasterDepthCompare>(255);
    PipelineRequest rejected;
    CHECK(RequestPipeline(session.Device, description, rejected) == RasterStatus::InvalidDescription);
    CHECK(reference::Creates[7] == created + 1);
    REQUIRE(Release(session.Device, conventional) == RasterStatus::Ready);
    REQUIRE(Release(session.Device, reverse) == RasterStatus::Ready);
    REQUIRE(Release(session.Device, same) == RasterStatus::Ready);
}
TEST_CASE("L1 frozen surface viewport checks actual acquisition without faulting the device", "[rhi][graph]")
{
    Session session;
    OrderedGraph graph;
    REQUIRE(CreateOrderedGraph(session.Device, graph) == RasterStatus::Ready);
    GraphPassDescription pass;
    pass.Name = "stale surface extent";
    pass.Attachment.UseViewport = true;
    pass.Attachment.Viewport = {0, 0, 97, 64};
    pass.Attachment.ClearDepth = 0;
    REQUIRE(AddGraphPass(session.Device, graph, pass) == RasterStatus::Ready);
    GraphReport report;
    REQUIRE(CompileOrderedGraph(session.Device, graph, report) == RasterStatus::Ready);
    SubmissionToken completion;
    CHECK(ExecuteOrderedGraph(session.Device, session.Surface, graph, completion) == RasterStatus::InvalidDescription);
    CHECK(GetStatus(session.Device, completion) == RasterStatus::Pending);
    CHECK(reference::Draws == 0);
    CHECK(DiscardOrderedGraph(session.Device, graph) == RasterStatus::InvalidHandle);
    DeviceInfo info;
    CHECK(GetDeviceInfo(session.Device, info) == DeviceStatus::Ready);
    REQUIRE(BeginFrame(session.Device, session.Surface) == DeviceStatus::Ready);
    RasterPassDescription invalid;
    invalid.ClearDepth = 2;
    CHECK(BeginRasterPass(session.Device, invalid) == RasterStatus::InvalidDescription);
    SubmissionToken next;
    CHECK(EndFrame(session.Device, next) == RasterStatus::Ready);
}
TEST_CASE("L1 rectangle intersection handles empty and overflowing scissors without wrap", "[rhi][raster]")
{
    internal::RasterArea area;
    RasterPassDescription pass;
    pass.UseScissor = true;
    pass.Scissor = {90, 60, ~uint32{0}, ~uint32{0}};
    REQUIRE(internal::RasterResolveArea(pass, 96, 64, area));
    CHECK(area.Scissor.Width == 6);
    CHECK(area.Scissor.Height == 4);
    CHECK_FALSE(area.Empty);
    pass.Scissor = {~uint32{0}, 0, ~uint32{0}, 1};
    REQUIRE(internal::RasterResolveArea(pass, 96, 64, area));
    CHECK(area.Empty);
    pass.UseViewport = true;
    pass.Viewport = {95, 0, ~uint32{0}, 1};
    CHECK_FALSE(internal::RasterResolveArea(pass, 96, 64, area));
}
TEST_CASE("L3 complete immutable mip uploads reject holes, overflow and attachment chains", "[rhi][raster][l3]")
{
    Session session;
    const uint8 bytes[32]{};
    TextureMipUpload mips[]{{0, 4}, {20, 4}, {28, 4}};
    TextureDescription description{3, 5, RasterFormat::R8Unorm, false, 3, true};
    TextureHandle texture;
    CHECK(CreateTexture(session.Device, description, {bytes, 0, {mips, 2}}, texture) ==
          RasterStatus::InvalidDescription);
    mips[1].Offset = 21;
    CHECK(CreateTexture(session.Device, description, {bytes, 0, mips}, texture) == RasterStatus::InvalidDescription);
    mips[1].Offset = 20;
    mips[2].RowPitch = ~usize{0} - 3;
    CHECK(CreateTexture(session.Device, description, {bytes, 0, mips}, texture) == RasterStatus::InvalidDescription);
    mips[2].RowPitch = 4;
    REQUIRE(CreateTexture(session.Device, description, {bytes, 0, mips}, texture) == RasterStatus::Ready);
    TextureDescription copy;
    REQUIRE(GetTextureDescription(session.Device, texture, copy) == RasterStatus::Ready);
    CHECK(copy.MipLevels == 3);
    REQUIRE(Destroy(session.Device, texture) == RasterStatus::Ready);
    description.MipLevels = 4;
    CHECK(CreateTexture(session.Device, description, {bytes, 0, mips}, texture) == RasterStatus::InvalidDescription);
    description = {2, 2, RasterFormat::Rgba8Unorm, true, 2};
    CHECK(CreateTexture(session.Device, description, {}, texture) == RasterStatus::InvalidDescription);
    description = {2, 2, RasterFormat::Rg8Unorm};
    const uint8 rg[8]{};
    REQUIRE(CreateTexture(session.Device, description, {rg, 4}, texture) == RasterStatus::Ready);
    REQUIRE(Destroy(session.Device, texture) == RasterStatus::Ready);
    description.Attachment = true;
    CHECK(CreateTexture(session.Device, description, {}, texture) == RasterStatus::InvalidDescription);
    SamplerHandle sampler;
    SamplerDescription invalid;
    invalid.MinLod = 1;
    CHECK(CreateSampler(session.Device, invalid, sampler) == RasterStatus::InvalidDescription);
    invalid.MipFilter = RasterMipFilter::Linear;
    invalid.MaxLod = 0;
    CHECK(CreateSampler(session.Device, invalid, sampler) == RasterStatus::InvalidDescription);
    invalid.MaxLod = 15;
    REQUIRE(CreateSampler(session.Device, invalid, sampler) == RasterStatus::Ready);
}
