#include "internal/lifecycle.h"
#include "reference_raster.h"

#include <ludus/graphics/rhi/lifetime.h>

#include <cstring>
#include <span>

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
    SamplerHandle samplers[16];
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
    PipelineRequest requests[16];
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
