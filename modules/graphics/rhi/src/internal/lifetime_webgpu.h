#pragma once
// Thanks to W3C, "WebGPU", GPUBuffer mapping and GPUQueue:
// https://www.w3.org/TR/webgpu/#buffer-mapping
// https://www.w3.org/TR/webgpu/#gpuqueue
// Mapping success and queue progress remain separate. Stable bounded callback
// cells outlive logical cancellation and shutdown; callbacks publish only into
// their own cell, and the device owner polls the shared registry later.
namespace ludus::graphics::rhi::LUDUS_RHI_WEBGPU_NAMESPACE
{
namespace
{
struct LifetimeWebTransfer final
{
    WGPUBuffer Staging = nullptr;
    uint32 Session = 0;
    uint32 Callbacks = 0;
    RasterStatus Result = RasterStatus::Ready;
    bool Busy = false;
    bool Mapped = false;
    bool Abandoned = false;
};
LifetimeWebTransfer gLifetimeWeb[2][internal::LIFETIME_TRANSFERS];
void LifetimeWebDrop(LifetimeWebTransfer& transfer) noexcept
{
    auto buffer = transfer.Staging;
    const bool mapped = transfer.Mapped;
    transfer = {};
    if (buffer != nullptr)
    {
        if (mapped)
        {
            wgpuBufferUnmap(buffer);
        }
        wgpuBufferRelease(buffer);
    }
}
void LifetimeWebCompleted(LifetimeWebTransfer& transfer, RasterStatus result) noexcept
{
    if (result != RasterStatus::Ready)
    {
        transfer.Result = result;
    }
    if (transfer.Callbacks != 0)
    {
        --transfer.Callbacks;
    }
    if ((transfer.Abandoned || !internal::Current(transfer.Session)) && transfer.Callbacks == 0)
    {
        LifetimeWebDrop(transfer);
    }
}
void LifetimeWebDone(WGPUQueueWorkDoneStatus status, WGPUStringView, void* userdata, void*) noexcept
{
    LifetimeWebCompleted(*static_cast<LifetimeWebTransfer*>(userdata),
                         status == WGPUQueueWorkDoneStatus_Success ? RasterStatus::Ready : RasterStatus::Failed);
}
void LifetimeWebMapped(WGPUMapAsyncStatus status, WGPUStringView, void* userdata, void*) noexcept
{
    auto& transfer = *static_cast<LifetimeWebTransfer*>(userdata);
    transfer.Mapped = status == WGPUMapAsyncStatus_Success;
    LifetimeWebCompleted(transfer, transfer.Mapped ? RasterStatus::Ready : RasterStatus::Failed);
}
void LifetimeWebValidated(WGPUPopErrorScopeStatus status,
                          WGPUErrorType error,
                          WGPUStringView message,
                          void* userdata,
                          void*) noexcept
{
    const auto result = status != WGPUPopErrorScopeStatus_Success ? RasterStatus::Failed
                        : error == WGPUErrorType_NoError          ? RasterStatus::Ready
                        : error == WGPUErrorType_OutOfMemory      ? RasterStatus::OutOfMemory
                                                                  : RasterStatus::Failed;
    if (result != RasterStatus::Ready)
    {
        Diagnose(message);
    }
    LifetimeWebCompleted(*static_cast<LifetimeWebTransfer*>(userdata), result);
}
void LifetimeWebPop(LifetimeWebTransfer& transfer) noexcept
{
    WGPUPopErrorScopeCallbackInfo callback = WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT;
    callback.mode = WGPUCallbackMode_AllowSpontaneous;
    callback.callback = LifetimeWebValidated;
    callback.userdata1 = &transfer;
    (void)wgpuDevicePopErrorScope(gDevice, callback);
    (void)wgpuDevicePopErrorScope(gDevice, callback);
}
RasterStatus LifetimeWebStage(LifetimeWebTransfer& transfer, bool readback) noexcept
{
    if (transfer.Busy || transfer.Callbacks != 0)
    {
        return RasterStatus::CapacityExceeded;
    }
    transfer.Session = gSession;
    transfer.Result = RasterStatus::Ready;
    transfer.Abandoned = false;
    if (transfer.Staging == nullptr)
    {
        WGPUBufferDescriptor buffer = WGPU_BUFFER_DESCRIPTOR_INIT;
        buffer.size = internal::LIFETIME_TRANSFER_BYTES;
        buffer.usage = readback ? WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst
                                : WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
        transfer.Staging = wgpuDeviceCreateBuffer(gDevice, &buffer);
    }
    return transfer.Staging != nullptr ? RasterStatus::Ready : RasterStatus::OutOfMemory;
}
} // namespace
RasterStatus LifetimeUpload(usize transferSlot,
                            const BufferDescription& info,
                            usize bufferSlot,
                            const uint8* bytes,
                            usize size,
                            uint32 request) noexcept
{
    auto& transfer = gLifetimeWeb[0][transferSlot];
    if (transfer.Busy || transfer.Callbacks != 0)
    {
        return RasterStatus::CapacityExceeded;
    }
    RasterScopes(request);
    const auto ready = LifetimeWebStage(transfer, false);
    transfer.Busy = true;
    WGPUBufferDescriptor buffer = WGPU_BUFFER_DESCRIPTOR_INIT;
    buffer.size = size;
    buffer.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst |
                   (info.Role == BufferRole::Uniform  ? WGPUBufferUsage_Uniform
                    : info.Role == BufferRole::Vertex ? WGPUBufferUsage_Vertex
                                                      : WGPUBufferUsage_Index);
    gRasterBuffers[bufferSlot] = wgpuDeviceCreateBuffer(gDevice, &buffer);
    if (ready != RasterStatus::Ready || gRasterBuffers[bufferSlot] == nullptr)
    {
        transfer.Result = RasterStatus::OutOfMemory;
        internal::RasterFail(request, RasterStatus::OutOfMemory);
        return RasterPop(request);
    }
    auto encoder = wgpuDeviceCreateCommandEncoder(gDevice, nullptr);
    if (encoder == nullptr)
    {
        transfer.Result = RasterStatus::OutOfMemory;
        internal::RasterFail(request, RasterStatus::OutOfMemory);
        return RasterPop(request);
    }
    wgpuQueueWriteBuffer(gQueue, transfer.Staging, 0, bytes, size);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, transfer.Staging, 0, gRasterBuffers[bufferSlot], 0, size);
    auto command = wgpuCommandEncoderFinish(encoder, nullptr);
    wgpuCommandEncoderRelease(encoder);
    if (command == nullptr)
    {
        transfer.Result = RasterStatus::OutOfMemory;
        internal::RasterFail(request, RasterStatus::OutOfMemory);
        return RasterPop(request);
    }
    wgpuQueueSubmit(gQueue, 1, &command);
    wgpuCommandBufferRelease(command);
    transfer.Callbacks = 1;
    WGPUQueueWorkDoneCallbackInfo callback = WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT;
    callback.mode = WGPUCallbackMode_AllowSpontaneous;
    callback.callback = LifetimeWebDone;
    callback.userdata1 = &transfer;
    (void)wgpuQueueOnSubmittedWorkDone(gQueue, callback);
    return RasterPop(request);
}
RasterStatus LifetimeReadback(usize transferSlot, BufferRole, const internal::LifetimeCopyRange& range) noexcept
{
    const auto bufferSlot = range.Buffer;
    const auto offset = range.Offset;
    const auto size = range.Size;
    auto& transfer = gLifetimeWeb[1][transferSlot];
    if (transfer.Busy || transfer.Callbacks != 0)
    {
        return RasterStatus::CapacityExceeded;
    }
    wgpuDevicePushErrorScope(gDevice, WGPUErrorFilter_OutOfMemory);
    wgpuDevicePushErrorScope(gDevice, WGPUErrorFilter_Validation);
    const auto ready = LifetimeWebStage(transfer, true);
    transfer.Busy = true;
    transfer.Callbacks = 2;
    if (ready != RasterStatus::Ready)
    {
        transfer.Result = ready;
        LifetimeWebPop(transfer);
        return RasterStatus::Pending;
    }
    auto encoder = wgpuDeviceCreateCommandEncoder(gDevice, nullptr);
    if (encoder == nullptr)
    {
        transfer.Result = RasterStatus::OutOfMemory;
        LifetimeWebPop(transfer);
        return RasterStatus::Pending;
    }
    wgpuCommandEncoderCopyBufferToBuffer(encoder, gRasterBuffers[bufferSlot], offset, transfer.Staging, 0, size);
    auto command = wgpuCommandEncoderFinish(encoder, nullptr);
    wgpuCommandEncoderRelease(encoder);
    if (command == nullptr)
    {
        transfer.Result = RasterStatus::OutOfMemory;
        LifetimeWebPop(transfer);
        return RasterStatus::Pending;
    }
    wgpuQueueSubmit(gQueue, 1, &command);
    wgpuCommandBufferRelease(command);
    ++transfer.Callbacks;
    WGPUBufferMapCallbackInfo callback = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
    callback.mode = WGPUCallbackMode_AllowSpontaneous;
    callback.callback = LifetimeWebMapped;
    callback.userdata1 = &transfer;
    (void)wgpuBufferMapAsync(transfer.Staging, WGPUMapMode_Read, 0, size, callback);
    LifetimeWebPop(transfer);
    return RasterStatus::Pending;
}
RasterStatus LifetimePollTransfer(bool readback, usize slot) noexcept
{
    auto& transfer = gLifetimeWeb[readback ? 1 : 0][slot];
    if (!transfer.Busy || transfer.Session != gSession)
    {
        return RasterStatus::Failed;
    }
    if (transfer.Callbacks != 0)
    {
        return RasterStatus::Pending;
    }
    if (readback && !transfer.Mapped && transfer.Result == RasterStatus::Ready)
    {
        return RasterStatus::Failed;
    }
    return transfer.Result;
}
RasterStatus LifetimeCopyReadback(usize slot, uint8* output, usize size) noexcept
{
    const auto ready = LifetimePollTransfer(true, slot);
    if (ready != RasterStatus::Ready)
    {
        return ready;
    }
    auto& transfer = gLifetimeWeb[1][slot];
    const void* mapped = wgpuBufferGetConstMappedRange(transfer.Staging, 0, size);
    if (mapped == nullptr)
    {
        return RasterStatus::Failed;
    }
    std::memcpy(output, mapped, size);
    return RasterStatus::Ready;
}
void LifetimeReleaseTransfer(bool readback, usize slot) noexcept
{
    auto& transfer = gLifetimeWeb[readback ? 1 : 0][slot];
    if (transfer.Mapped)
    {
        wgpuBufferUnmap(transfer.Staging);
        transfer.Mapped = false;
    }
    transfer.Busy = false;
}
void LifetimeReset() noexcept
{
    for (auto& direction : gLifetimeWeb)
    {
        for (auto& transfer : direction)
        {
            if (transfer.Callbacks == 0)
            {
                LifetimeWebDrop(transfer);
            }
            else
            {
                transfer.Abandoned = true;
                // Map cancellation is asynchronous; callback storage remains occupied.
                if (transfer.Staging != nullptr)
                {
                    wgpuBufferDestroy(transfer.Staging);
                }
            }
        }
    }
}
void RasterDiscardSubmission() noexcept
{
    if (gRasterReserved != nullptr)
    {
        *gRasterReserved = {};
        gRasterReserved = nullptr;
    }
}
} // namespace ludus::graphics::rhi::LUDUS_RHI_WEBGPU_NAMESPACE
