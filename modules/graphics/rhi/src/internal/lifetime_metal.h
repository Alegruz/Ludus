#pragma once
// Thanks to Apple, "MTLBlitCommandEncoder", Metal API documentation,
// https://developer.apple.com/documentation/metal/mtlblitcommandencoder
// for buffer-to-buffer blits; command-buffer completion protects shared slices.
// Private Metal transfer slices. Shared CPU memory is read only after the
// submitted blit command completes; logical cancellation never recycles it early.
namespace ludus::graphics::rhi::backend
{
namespace
{
struct LifetimeMetalTransfer final
{
    id<MTLBuffer> Staging = nil;
    id<MTLCommandBuffer> Command = nil;
    bool Busy = false;
};
LifetimeMetalTransfer gLifetimeMetal[2][internal::LIFETIME_TRANSFERS];
RasterStatus LifetimeMetalStage(LifetimeMetalTransfer& transfer) noexcept
{
    if (transfer.Busy)
    {
        return RasterStatus::CapacityExceeded;
    }
    if (transfer.Staging == nil)
    {
        transfer.Staging = [gDevice newBufferWithLength:internal::LIFETIME_TRANSFER_BYTES
                                                options:MTLResourceStorageModeShared];
    }
    return transfer.Staging != nil ? RasterStatus::Ready : RasterStatus::OutOfMemory;
}
RasterStatus LifetimeMetalCopy(LifetimeMetalTransfer& transfer,
                               id<MTLBuffer> source,
                               usize offset,
                               id<MTLBuffer> destination,
                               usize size) noexcept
{
    transfer.Command = [gQueue commandBuffer];
    if (transfer.Command == nil)
    {
        return RasterStatus::OutOfMemory;
    }
    id<MTLBlitCommandEncoder> encoder = [transfer.Command blitCommandEncoder];
    if (encoder == nil)
    {
        transfer.Command = nil;
        return RasterStatus::OutOfMemory;
    }
    [encoder copyFromBuffer:source sourceOffset:offset toBuffer:destination destinationOffset:0 size:size];
    [encoder endEncoding];
    transfer.Busy = true;
    [transfer.Command commit];
    return RasterStatus::Ready;
}
} // namespace
RasterStatus LifetimeUpload(usize transferSlot,
                            const BufferDescription&,
                            const internal::LifetimeUploadTarget& uploadTarget,
                            const uint8* bytes,
                            usize size) noexcept
{
    const auto bufferSlot = uploadTarget.Buffer;

    @autoreleasepool
    {
        auto& transfer = gLifetimeMetal[0][transferSlot];
        const auto ready = LifetimeMetalStage(transfer);
        if (ready != RasterStatus::Ready)
        {
            return ready;
        }
        gRasterBuffers[bufferSlot] = [gDevice newBufferWithLength:size options:MTLResourceStorageModePrivate];
        if (gRasterBuffers[bufferSlot] == nil)
        {
            return RasterStatus::OutOfMemory;
        }
        std::memcpy([transfer.Staging contents], bytes, size);
        return LifetimeMetalCopy(transfer, transfer.Staging, 0, gRasterBuffers[bufferSlot], size);
    }
}
RasterStatus LifetimeReadback(usize transferSlot, BufferRole, const internal::LifetimeCopyRange& range) noexcept
{
    const auto bufferSlot = range.Buffer;
    const auto offset = range.Offset;
    const auto size = range.Size;
    @autoreleasepool
    {
        auto& transfer = gLifetimeMetal[1][transferSlot];
        const auto ready = LifetimeMetalStage(transfer);
        return ready == RasterStatus::Ready
                   ? LifetimeMetalCopy(transfer, gRasterBuffers[bufferSlot], offset, transfer.Staging, size)
                   : ready;
    }
}
RasterStatus LifetimePollTransfer(bool readback, usize slot) noexcept
{
    auto& transfer = gLifetimeMetal[readback ? 1 : 0][slot];
    if (!transfer.Busy || transfer.Command == nil)
    {
        return RasterStatus::Failed;
    }
    if (!CheckCommand(transfer.Command))
    {
        return RasterStatus::Failed;
    }
    const auto status = transfer.Command.status;
    return status == MTLCommandBufferStatusCompleted ? RasterStatus::Ready
           : status == MTLCommandBufferStatusError   ? RasterStatus::Failed
                                                     : RasterStatus::Pending;
}
RasterStatus LifetimeCopyReadback(usize slot, uint8* output, usize size) noexcept
{
    const auto ready = LifetimePollTransfer(true, slot);
    if (ready != RasterStatus::Ready)
    {
        return ready;
    }
    std::memcpy(output, [gLifetimeMetal[1][slot].Staging contents], size);
    return RasterStatus::Ready;
}
void LifetimeReleaseTransfer(bool readback, usize slot) noexcept
{
    auto& transfer = gLifetimeMetal[readback ? 1 : 0][slot];
    transfer.Command = nil;
    transfer.Busy = false;
}
void LifetimeReset() noexcept
{
    for (auto& direction : gLifetimeMetal)
    {
        for (auto& transfer : direction)
        {
            if (transfer.Command != nil)
            {
                [transfer.Command waitUntilCompleted];
            }
            transfer = {};
        }
    }
}
void RasterDiscardSubmission() noexcept {}
} // namespace ludus::graphics::rhi::backend
