#include "reference_raster.h"
#include "internal/lifecycle.h"
#include <cstring>
namespace ludus::graphics::rhi::reference
{
void Reset() noexcept
{
    ProfileAvailable = true;
    Next = RasterStatus::Ready;
    Submission = RasterStatus::Ready;
    LastRequest = 0;
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
        internal::RasterExpect(request, internal::RasterCallbacks::One);
    }
    return reference::Next;
}
} // namespace
RasterCapabilities RasterLimits() noexcept
{
    if (!reference::ProfileAvailable)
    {
        return {};
    }
    return {usize{64} * 1024 * 1024, 4096, 256, 16384, 16, 1024};
}
RasterStatus
RasterCreateBuffer(usize slot, const BufferDescription&, std::span<const uint8> bytes, uint32 request) noexcept
{
    if (bytes.size() > internal::LIFETIME_TRANSFER_BYTES)
    {
        return RasterStatus::OutOfMemory;
    }
    std::memcpy(reference::BufferBytes[slot], bytes.data(), bytes.size());
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
RasterStatus RasterCreatePipeline(usize, const internal::RasterPipelineInfo&, uint32 request) noexcept
{
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
RasterStatus RasterBeginPass(const internal::RasterPassInfo&) noexcept
{
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
    std::memcpy(reference::BufferBytes[buffer], bytes, size);
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
    const auto& bytes = reference::BufferBytes[buffer];
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
