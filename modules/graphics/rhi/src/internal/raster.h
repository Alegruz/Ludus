#pragma once

#include <ludus/foundation/base/types.h>

#include <ludus/graphics/rhi/graph.h>

namespace ludus::graphics::rhi::internal
{
inline constexpr foundation::usize RASTER_CAPACITY = 16;
inline constexpr foundation::usize RASTER_BINDINGS = 8;
inline constexpr foundation::usize RASTER_DRAWS = 1024;
inline constexpr foundation::usize LIFETIME_BATCHES = 4;
inline constexpr foundation::usize LIFETIME_BATCH_DRAWS = 256;
inline constexpr foundation::usize LIFETIME_TRANSFERS = 4;
inline constexpr foundation::usize LIFETIME_TRANSFER_BYTES = foundation::usize{256} * 1024;
// Shared by callback requests and submission ordinals; zero stays exhausted.
template <typename T>
struct RasterSequence final
{
    T Next = 1;
    [[nodiscard]] T Take() noexcept
    {
        const T value = Next;
        Next = value == 0 || value == static_cast<T>(~T{0}) ? 0 : static_cast<T>(value + 1);
        return value;
    }
};
[[nodiscard]] inline foundation::uint64 RasterNextGeneration(foundation::uint64 value) noexcept
{
    return value == 0 || value == ~foundation::uint64{0} ? 0 : value + 1;
}
enum class RasterKind : foundation::uint8
{
    Buffer,
    Texture,
    View,
    Sampler,
    Shader,
    Layout,
    Set,
    Pipeline,
    ComputePipeline,
    Count
};
struct RasterLayout final
{
    RasterBinding Entries[RASTER_BINDINGS]{};
    foundation::usize Count = 0;
};
struct RasterShaderInfo final
{
    ShaderStage Stage = ShaderStage::Vertex;
    RasterLayout Layout;
    RasterShaderInput Inputs[8]{};
    foundation::usize InputCount = 0;
    foundation::uint32 WorkgroupSize[3]{1, 1, 1};
    char UniformBlocks[RASTER_BINDINGS][64]{};
    char TextureNames[RASTER_BINDINGS][64]{};
    foundation::uint32 TextureSamplers[RASTER_BINDINGS]{};
};
struct RasterViewSource final
{
    foundation::usize Texture;
};
struct RasterSet final
{
    foundation::usize Layout = 0;
    foundation::usize Slots[RASTER_BINDINGS]{};
    foundation::usize Offsets[RASTER_BINDINGS]{};
    foundation::usize Sizes[RASTER_BINDINGS]{};
};
struct RasterPipelineInfo final
{
    foundation::usize Vertex = 0;
    foundation::usize Fragment = 0;
    foundation::usize Layout = 0;
    RasterVertexStream Streams[2]{};
    RasterVertexAttribute Attributes[8]{};
    foundation::usize StreamCount = 0;
    foundation::usize AttributeCount = 0;
    bool Depth = false;
    bool Blend = false;
    RasterTarget Target = RasterTarget::Surface;
    bool DepthWrite = true;
    RasterDepthCompare DepthCompare = RasterDepthCompare::Less;
};
struct ComputePipelineInfo final
{
    foundation::usize Shader = 0;
    foundation::usize Layout = 0;
    foundation::uint32 WorkgroupSize[3]{1, 1, 1};
};
struct ComputePacket final
{
    foundation::usize Pipeline = 0;
    foundation::usize Set = 0;
    foundation::uint32 Groups[3]{1, 1, 1};
};
struct RasterPacket final
{
    foundation::usize Pipeline = 0;
    foundation::usize Set = 0;
    foundation::usize Vertices[2]{};
    foundation::usize Offsets[2]{};
    foundation::usize Sizes[2]{};
    foundation::usize Indices = 0;
    foundation::usize IndexOffset = 0;
    foundation::uint32 IndexCount = 0;
    foundation::uint32 InstanceCount = 0;
    bool Index32 = false;
    foundation::usize Indirect = RASTER_CAPACITY;
    foundation::usize IndirectOffset = 0;
};
struct RasterPassInfo final
{
    foundation::usize Texture = RASTER_CAPACITY;
    RasterPassDescription Description;
};
// Canonical rectangles stay top-left; API-specific Y conversion occurs only in backends.
struct RasterArea final
{
    RasterRectangle Viewport{}, Scissor{};
    bool Empty = false;
};
inline bool RasterResolveArea(const RasterPassDescription& pass,
                              foundation::uint32 width,
                              foundation::uint32 height,
                              RasterArea& area) noexcept
{
    const RasterRectangle full{0, 0, width, height};
    area.Viewport = pass.UseViewport ? pass.Viewport : full;
    const auto& v = area.Viewport;
    if (v.Width == 0 || v.Height == 0 || v.X > width || v.Y > height || v.Width > width - v.X ||
        v.Height > height - v.Y)
    {
        return false;
    }
    area.Scissor = pass.UseScissor ? pass.Scissor : full;
    auto& s = area.Scissor;
    if (s.X > width)
    {
        s.X = width;
    }
    if (s.Y > height)
    {
        s.Y = height;
    }
    if (s.Width > width - s.X)
    {
        s.Width = width - s.X;
    }
    if (s.Height > height - s.Y)
    {
        s.Height = height - s.Y;
    }
    area.Empty = s.Width == 0 || s.Height == 0;
    return true;
}
struct LifetimeUploadTarget final
{
    foundation::usize Buffer = 0;
    foundation::uint32 Request = 0;
};
struct LifetimeCopyRange final
{
    foundation::usize Buffer = 0;
    foundation::usize Offset = 0;
    foundation::usize Size = 0;
};
// These hooks join the existing frame/loss owner, not a second session.
bool FrameOpen() noexcept;
bool ClaimRasterFrame() noexcept;
void RasterBeginFrame() noexcept;
void RasterEndFrame(bool accepted) noexcept;
bool RasterFinishFrame() noexcept;
void ReleaseRasterResources() noexcept;
enum class RasterCallbacks : foundation::uint8
{
    One = 1,
    Two = 2,
    Three = 3
};
void RasterExpect(foundation::uint32 request, RasterCallbacks callbacks) noexcept;
void RasterComplete(foundation::uint32 request, RasterStatus status) noexcept;
void RasterFail(foundation::uint32 request, RasterStatus status) noexcept;
void ResetLifetimeRecords() noexcept;
} // namespace ludus::graphics::rhi::internal
namespace ludus::graphics::rhi::backend
{
RasterCapabilities RasterLimits() noexcept;
ComputeCapabilities ComputeLimits() noexcept;
RasterStatus
ComputeCreatePipeline(foundation::usize, const internal::ComputePipelineInfo&, foundation::uint32) noexcept;
RasterStatus ComputeEncode(const internal::ComputePacket&) noexcept;
void ComputeBufferBarrier(foundation::usize, GraphAccessMode) noexcept;
RasterStatus RasterCreateBuffer(foundation::usize,
                                const BufferDescription&,
                                std::span<const foundation::uint8>,
                                foundation::uint32) noexcept;
RasterStatus
RasterCreateTexture(foundation::usize, const TextureDescription&, const TextureUpload&, foundation::uint32) noexcept;
RasterStatus RasterCreateView(foundation::usize, const internal::RasterViewSource&, foundation::uint32) noexcept;
RasterStatus RasterCreateSampler(foundation::usize, const SamplerDescription&, foundation::uint32) noexcept;
RasterStatus RasterCreateShader(foundation::usize,
                                const ShaderDescription&,
                                const internal::RasterShaderInfo&,
                                foundation::uint32) noexcept;
RasterStatus RasterCreateLayout(foundation::usize, const internal::RasterLayout&, foundation::uint32) noexcept;
RasterStatus RasterCreateSet(foundation::usize,
                             const internal::RasterLayout&,
                             const internal::RasterSet&,
                             foundation::uint32) noexcept;
RasterStatus RasterCreatePipeline(foundation::usize, const internal::RasterPipelineInfo&, foundation::uint32) noexcept;
void RasterDestroy(internal::RasterKind, foundation::usize) noexcept;
RasterStatus RasterDraw(const internal::RasterPacket&) noexcept;
RasterStatus RasterPreparePass(const internal::RasterPassInfo&) noexcept;
RasterStatus RasterBeginPass(const internal::RasterPassInfo&) noexcept;
void RasterEndPass() noexcept;
void RasterTextureBarrier(foundation::usize, RasterTextureUse) noexcept;
// Reserve mandatory completion bookkeeping before the first draw; submission
// cannot discover that its callback storage is exhausted after GPU work starts.
RasterStatus RasterReserveSubmission() noexcept;
void RasterDiscardSubmission() noexcept;
void RasterSubmit(foundation::uint64 ordinal) noexcept;
foundation::uint64 RasterCompleted() noexcept;
void RasterReset() noexcept;
void RasterShutdown() noexcept;
// Logical records can reset while an old browser callback still owns its cell.
bool LifetimeTransferAvailable(bool, foundation::usize) noexcept;
RasterStatus LifetimeUpload(foundation::usize,
                            const BufferDescription&,
                            const internal::LifetimeUploadTarget&,
                            const foundation::uint8*,
                            foundation::usize) noexcept;
RasterStatus LifetimeReadback(foundation::usize, BufferRole, const internal::LifetimeCopyRange&) noexcept;
RasterStatus LifetimePollTransfer(bool, foundation::usize) noexcept;
RasterStatus LifetimeCopyReadback(foundation::usize, foundation::uint8*, foundation::usize) noexcept;
void LifetimeReleaseTransfer(bool, foundation::usize) noexcept;
void LifetimeReset() noexcept;
} // namespace ludus::graphics::rhi::backend
