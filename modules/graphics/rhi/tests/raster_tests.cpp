#include "internal/lifecycle.h"
#include "reference_raster.h"

#include <ludus/graphics/rhi/raster.h>

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
