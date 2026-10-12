#include "reference_raster.h"
#include "internal/backend.h"
#include "internal/lifecycle.h"
#include "internal/profile.h"
#include <cstring>
namespace ludus::graphics::rhi::reference
{
void Reset() noexcept
{
    ProfileAvailable = true;
    HdrAvailable = true;
    ComputeAvailable = true;
    FormatBlendUnavailable = false;
    OverrideRasterLimits = false;
    for (auto& unavailable : FormatUnavailable)
    {
        unavailable = false;
    }
    Dispatches = 0;
    ComputeResult = RasterStatus::Ready;
    Next = RasterStatus::Ready;
    Submission = RasterStatus::Ready;
    LastRequest = 0;
    PendingCount = 0;
    LossOnCompletion = 0;
    Submitted = 0;
    Completed = 0;
    Draws = Passes = Barriers = 0;
    PassStart = PassPrepare = RasterStatus::Ready;
    Discards = 0;
    Copies = 0;
    TransferStart = ReadbackCopy = RasterStatus::Ready;
    RetainTransfersOnReset = false;
    for (auto& direction : TransferOccupied)
    {
        for (auto& occupied : direction)
        {
            occupied = false;
        }
    }
    for (auto& direction : Transfers)
    {
        for (auto& status : direction)
        {
            status = RasterStatus::Pending;
        }
    }
    for (auto& pipeline : Pipelines)
    {
        pipeline = {};
    }
    for (auto& size : BufferSizes)
    {
        size = 0;
    }
    for (auto& count : Creates)
    {
        count = 0;
    }
    for (auto& count : Destroys)
    {
        count = 0;
    }
}
} // namespace ludus::graphics::rhi::reference
namespace ludus::graphics::rhi::backend
{
using namespace foundation;
namespace
{
RasterStatus Created(internal::RasterKind kind, uint32 request) noexcept
{
    ++reference::Creates[static_cast<usize>(kind)];
    reference::LastRequest = request;
    if (reference::Next == RasterStatus::Pending)
    {
        if (reference::PendingRequests.TryResize(reference::PendingCount + 1))
        {
            reference::PendingRequests[reference::PendingCount++] = request;
        }
        internal::RasterExpect(request, internal::RasterCallbacks::One);
    }
    return reference::Next;
}
} // namespace
RasterStatus RasterGrow(internal::RasterKind kind, usize count) noexcept
{
    if (kind == internal::RasterKind::Buffer)
    {
        return reference::BufferBytes.EnsureSlots(count) && reference::BufferSizes.EnsureSlots(count)
                   ? RasterStatus::Ready
                   : internal::StorageFailure();
    }
    if (kind == internal::RasterKind::Pipeline)
    {
        return reference::Pipelines.EnsureSlots(count) ? RasterStatus::Ready : internal::StorageFailure();
    }
    return RasterStatus::Ready;
}
RasterStatus RasterPrepareStorage() noexcept
{
    return RasterStatus::Ready;
}
ComputeCapabilities ComputeLimits() noexcept
{
    return reference::ProfileAvailable && reference::ComputeAvailable && Kind() != Backend::WebGL2
               ? ComputeCapabilities{16777216, 256, {256, 256, 64}, 256, {65535, 65535, 65535}}
               : ComputeCapabilities{};
}
RasterStatus ComputeCreatePipeline(usize, const internal::ComputePipelineInfo&, uint32 request) noexcept
{
    return Created(internal::RasterKind::ComputePipeline, request);
}
RasterStatus ComputeEncode(const internal::ComputePacket&) noexcept
{
    ++reference::Dispatches;
    return reference::ComputeResult;
}
void ComputeBufferBarrier(usize, GraphAccessMode) noexcept {}
FormatCapabilities FormatLimits(RasterFormat format) noexcept
{
    if (static_cast<uint8>(format) > static_cast<uint8>(RasterFormat::Rgba16Float) ||
        reference::FormatUnavailable[static_cast<usize>(format)])
    {
        return {};
    }
    auto result = internal::PortableFormatLimits(format, RasterLimits());
    if (reference::FormatBlendUnavailable)
    {
        result.Uses.Blendable = false;
    }
    return result;
}
uint32 RasterVertexStrideLimit() noexcept
{
    return Kind() == Backend::WebGL2 ? 252U : 256U;
}
RasterCapabilities RasterLimits() noexcept
{
    if (!reference::ProfileAvailable)
    {
        return {};
    }
    if (reference::OverrideRasterLimits)
    {
        return reference::RasterLimitOverride;
    }
    return {usize{64} * 1024 * 1024,
            4096,
            256,
            16384,
            internal::CurrentStorageLimits().ResourcesPerKind,
            internal::CurrentStorageLimits().DrawsPerFrame,
            16,
            reference::HdrAvailable};
}
RasterStatus
RasterCreateBuffer(usize slot, const BufferDescription&, std::span<const uint8> bytes, uint32 request) noexcept
{
    if (bytes.size() > internal::LIFETIME_TRANSFER_BYTES)
    {
        return RasterStatus::OutOfMemory;
    }
    if (!reference::BufferBytes[slot].TryResize(bytes.size()))
    {
        return RasterStatus::OutOfMemory;
    }
    std::memcpy(reference::BufferBytes[slot].GetData(), bytes.data(), bytes.size());
    reference::BufferSizes[slot] = bytes.size();
    return Created(internal::RasterKind::Buffer, request);
}
RasterStatus RasterCreateTexture(usize, const TextureDescription&, const TextureUpload&, uint32 request) noexcept
{
    return Created(internal::RasterKind::Texture, request);
}
RasterStatus RasterCreateView(usize, const internal::RasterViewSource&, uint32 request) noexcept
{
    return Created(internal::RasterKind::View, request);
}
RasterStatus RasterCreateSampler(usize, const SamplerDescription&, uint32 request) noexcept
{
    return Created(internal::RasterKind::Sampler, request);
}
RasterStatus
RasterCreateShader(usize, const ShaderDescription&, const internal::RasterShaderInfo&, uint32 request) noexcept
{
    return Created(internal::RasterKind::Shader, request);
}
RasterStatus RasterCreateLayout(usize, const internal::RasterLayout&, uint32 request) noexcept
{
    return Created(internal::RasterKind::Layout, request);
}
RasterStatus RasterCreateSet(usize, const internal::RasterLayout&, const internal::RasterSet&, uint32 request) noexcept
{
    return Created(internal::RasterKind::Set, request);
}
RasterStatus RasterCreatePipeline(usize slot, const internal::RasterPipelineInfo& info, uint32 request) noexcept
{
    reference::Pipelines[slot] = info;
    return Created(internal::RasterKind::Pipeline, request);
}
void RasterDestroy(internal::RasterKind kind, usize) noexcept
{
    ++reference::Destroys[static_cast<usize>(kind)];
}
RasterStatus RasterDraw(const internal::RasterPacket& packet) noexcept
{
    reference::Packet = packet;
    ++reference::Draws;
    return RasterStatus::Ready;
}
RasterStatus RasterPreparePass(const internal::RasterPassInfo&) noexcept
{
    return reference::PassPrepare;
}
void RasterEndPass() noexcept {}
RasterStatus RasterBeginPass(const internal::RasterPassInfo& pass) noexcept
{
    reference::Pass = pass;
    ++reference::Passes;
    return reference::PassStart;
}
void RasterTextureBarrier(usize, RasterTextureUse) noexcept
{
    ++reference::Barriers;
}
RasterStatus RasterReserveSubmission() noexcept
{
    return reference::Submission;
}
void RasterSubmit(uint64 ordinal) noexcept
{
    reference::Submitted = ordinal;
}
uint64 RasterCompleted() noexcept
{
    const auto token = reference::LossOnCompletion;
    reference::LossOnCompletion = 0;
    if (token != 0)
    {
        internal::Fail(token, StartupError::DeviceLost);
    }
    return reference::Completed;
}
void RasterDiscardSubmission() noexcept
{
    ++reference::Discards;
}
bool LifetimeTransferAvailable(bool readback, usize slot) noexcept
{
    return !reference::TransferOccupied[readback ? 1 : 0][slot];
}
RasterStatus LifetimeUpload(usize transfer,
                            const BufferDescription&,
                            const internal::LifetimeUploadTarget& uploadTarget,
                            const uint8* bytes,
                            usize size) noexcept
{
    const auto buffer = uploadTarget.Buffer;
    const auto request = uploadTarget.Request;

    if (!LifetimeTransferAvailable(false, transfer))
    {
        return RasterStatus::CapacityExceeded;
    }
    if (reference::TransferStart != RasterStatus::Ready)
    {
        return reference::TransferStart;
    }
    if (!reference::BufferBytes[buffer].TryResize(size))
    {
        return RasterStatus::OutOfMemory;
    }
    std::memcpy(reference::BufferBytes[buffer].GetData(), bytes, size);
    reference::BufferSizes[buffer] = size;
    reference::Transfers[0][transfer] = RasterStatus::Pending;
    reference::TransferOccupied[0][transfer] = true;
    ++reference::Copies;
    return Created(internal::RasterKind::Buffer, request);
}
RasterStatus LifetimeReadback(usize transfer, BufferRole, const internal::LifetimeCopyRange& range) noexcept
{
    const auto buffer = range.Buffer;
    const auto offset = range.Offset;
    const auto size = range.Size;
    if (!LifetimeTransferAvailable(true, transfer))
    {
        return RasterStatus::CapacityExceeded;
    }
    if (reference::TransferStart != RasterStatus::Ready)
    {
        return reference::TransferStart;
    }
    const auto* bytes = reference::BufferBytes[buffer].GetData();
    std::memcpy(reference::ReadbackBytes[transfer], bytes + offset, size);
    reference::Transfers[1][transfer] = RasterStatus::Pending;
    reference::TransferOccupied[1][transfer] = true;
    ++reference::Copies;
    return RasterStatus::Ready;
}
RasterStatus LifetimePollTransfer(bool readback, usize slot) noexcept
{
    return reference::Transfers[readback ? 1 : 0][slot];
}
RasterStatus LifetimeCopyReadback(usize slot, uint8* output, usize size) noexcept
{
    if (reference::ReadbackCopy == RasterStatus::Ready)
    {
        std::memcpy(output, reference::ReadbackBytes[slot], size);
    }
    else
    {
        output[0] = 0;
    } // Deliberately dirty backend scratch on failure.
    return reference::ReadbackCopy;
}
void LifetimeReleaseTransfer(bool readback, usize slot) noexcept
{
    reference::Transfers[readback ? 1 : 0][slot] = RasterStatus::Failed;
    reference::TransferOccupied[readback ? 1 : 0][slot] = false;
}
void LifetimeReset() noexcept
{
    if (!reference::RetainTransfersOnReset)
    {
        for (auto& direction : reference::TransferOccupied)
        {
            for (auto& occupied : direction)
            {
                occupied = false;
            }
        }
    }
}
void RasterShutdown() noexcept {}
void RasterReset() noexcept
{
    reference::Submitted = 0;
    reference::Completed = 0;
}
} // namespace ludus::graphics::rhi::backend
