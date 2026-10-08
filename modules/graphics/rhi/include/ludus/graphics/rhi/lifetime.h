#pragma once

#include <ludus/foundation/base/types.h>

#include <ludus/graphics/rhi/raster.h>

namespace ludus::graphics::rhi
{
namespace internal
{
/// Private access to lifetime-service identities.
struct LifetimeAccess;
} // namespace internal
/// Owned retained draw batch, scoped to one device incarnation. Copies do not transfer ownership.
/// Main-thread use only. Submit or discard exactly once; stale copies are rejected.
struct CommandBatch final
{
private:
    ludus::foundation::uint64 Owner = 0;
    ludus::foundation::uint64 Generation = 0;
    ludus::foundation::uint32 Slot = 0;
    /// Grants private registry access to assign and validate this incarnation.
    friend struct internal::LifetimeAccess;
};
/// Owned buffer-upload request. Release cancels logical ownership, never in-flight GPU work.
/// Main-thread use only. TakeUploadedBuffer or Release exactly once; stale copies are rejected.
struct UploadTicket final
{
private:
    ludus::foundation::uint64 Owner = 0;
    ludus::foundation::uint64 Generation = 0;
    ludus::foundation::uint32 Slot = 0;
    /// Grants private registry access to assign and validate this incarnation.
    friend struct internal::LifetimeAccess;
};
/// Owned buffer-readback request retaining its source until completion or cancellation retirement.
/// Main-thread use only. Release exactly once; stale copies are rejected.
struct ReadbackTicket final
{
private:
    ludus::foundation::uint64 Owner = 0;
    ludus::foundation::uint64 Generation = 0;
    ludus::foundation::uint32 Slot = 0;
    /// Grants private registry access to assign and validate this incarnation.
    friend struct internal::LifetimeAccess;
};
/// Owned prewarm request retaining a shared immutable pipeline and its shader/layout dependencies.
/// Main-thread use only. Release exactly once; stale copies are rejected.
struct PipelineRequest final
{
private:
    ludus::foundation::uint64 Owner = 0;
    ludus::foundation::uint64 Generation = 0;
    ludus::foundation::uint32 Slot = 0;
    /// Grants private registry access to assign and validate this incarnation.
    friend struct internal::LifetimeAccess;
};
/// Borrowed ordered-queue completion identity; copying requires no release.
/// Main-thread polling only. Completion proves GPU execution, not presentation or image correctness.
/// A complete higher ordinal covers earlier accepted work in the same device session.
struct SubmissionToken final
{
private:
    ludus::foundation::uint64 Owner = 0;
    ludus::foundation::uint64 Ordinal = 0;
    /// Grants private registry access to the owner and monotonic queue ordinal.
    friend struct internal::LifetimeAccess;
};
/// Effective fixed service budgets for a ready portable-raster device.
struct LifetimeCapabilities final
{
    /// Number of recording/finished batches, including retained dependency records.
    ludus::foundation::uint32 CommandBatches = 0;
    /// Draw packets per batch; recording consumes no native GPU commands.
    ludus::foundation::uint32 DrawsPerBatch = 0;
    /// Simultaneous uploads and readbacks independently, including cancelled in-flight slots.
    ludus::foundation::uint32 TransfersPerDirection = 0;
    /// Maximum bytes in one upload/readback ring slice.
    ludus::foundation::usize TransferBytes = 0;
    /// Simultaneous independently owned pipeline requests.
    ludus::foundation::uint32 PipelineRequests = 0;
};
/// Query fixed budgets; invalid owner preserves output, unavailable device writes zero budgets.
[[nodiscard]] RasterStatus GetLifetimeCapabilities(DeviceHandle device, LifetimeCapabilities& output) noexcept;
/// Reserve a recording batch in a null output between frames. Ready publishes ownership.
/// Admission failure preserves output; no caller storage is borrowed.
[[nodiscard]] RasterStatus BeginCommands(DeviceHandle device, CommandBatch& output) noexcept;
/// Validate and copy one complete draw into a Recording batch before any native work.
/// Retains pipeline, binding snapshot, geometry and their dependencies. Rejection preserves the batch.
/// Destroying public resource handles after acceptance cannot invalidate this retained packet.
[[nodiscard]] RasterStatus RecordDraw(DeviceHandle device, CommandBatch batch, const RasterDraw& draw) noexcept;
/// Change Recording to Finished once; later recording is InvalidState.
[[nodiscard]] RasterStatus FinishCommands(DeviceHandle device, CommandBatch batch) noexcept;
/// Discard Recording/Finished work and release CPU leases. Clears batch on success.
/// No GPU work is issued. Invalid/stale owner preserves batch.
[[nodiscard]] RasterStatus DiscardCommands(DeviceHandle device, CommandBatch& batch) noexcept;
/// Acquire the current target, encode a Finished batch and submit it once.
/// @pre completion is null; no other frame is open. Main-thread use only.
/// Ready transfers CPU leases to GPU retirement, clears batch, and publishes completion.
/// A skipped target returns NotReady and preserves the finished batch for retry.
/// Native failure after encoding begins can submit partial work: a non-null completion
/// still identifies that accepted GPU work; inspect the returned failure independently.
/// Preflight/admission failure preserves batch and completion.
[[nodiscard]] RasterStatus
SubmitCommands(DeviceHandle device, SurfaceHandle surface, CommandBatch& batch, SubmissionToken& completion) noexcept;
/// End an open direct frame with completion tracking, including clear-only frames.
/// @pre completion is null. Ready publishes accepted ordered GPU work, never a displayed frame number.
/// Admission/reservation failure preserves output and leaves the frame open for retry.
[[nodiscard]] RasterStatus EndFrame(DeviceHandle device, SubmissionToken& completion) noexcept;
/// Nonblocking completion poll. Ready means Complete; Pending means accepted work is outstanding.
/// Failed/DeviceLost preserve the failure rather than reporting completion. Foreign/zero ordinals
/// and ordinals not published by this device return InvalidHandle. Tokens need no release.
[[nodiscard]] RasterStatus GetStatus(DeviceHandle device, SubmissionToken completion) noexcept;
/// Copy complete initial buffer bytes into an owned bounded upload-ring slice before return.
/// @param device Ready owner with portable raster enabled.
/// @param description Immutable buffer role and original size in bytes.
/// @param bytes Borrowed byte pointer; non-null with byteCount == description.Size.
/// @param byteCount Bytes copied, nonzero and at most TransferBytes; buffer-role alignment applies.
/// @param output Null ticket receiving ownership on Pending; failures preserve it.
/// The copy is submitted before return; PollLifetime/GetStatus polls without waiting. TakeUploadedBuffer transfers a
/// Ready immutable result; Release cancels publication while retaining in-flight staging until completion.
[[nodiscard]] RasterStatus RequestBufferUpload(DeviceHandle device,
                                               const BufferDescription& description,
                                               const ludus::foundation::uint8* bytes,
                                               ludus::foundation::usize byteCount,
                                               UploadTicket& output) noexcept;
/// Schedule a bounded byte-range copy from a ready buffer to an owned readback-ring slice.
/// Offset and size are multiples of four, size is nonzero and at most TransferBytes.
/// The source incarnation is retained even after its public handle is destroyed.
/// Pending publishes output; rejection preserves it. Call between frames.
[[nodiscard]] RasterStatus RequestBufferReadback(DeviceHandle device,
                                                 BufferHandle source,
                                                 ludus::foundation::usize offset,
                                                 ludus::foundation::usize size,
                                                 ReadbackTicket& output) noexcept;
/// Poll upload validation and GPU completion without waiting; failure still requires Release.
[[nodiscard]] RasterStatus GetStatus(DeviceHandle device, UploadTicket ticket) noexcept;
/// Poll copy completion and mapping independently; Ready guarantees CopyReadback is legal.
[[nodiscard]] RasterStatus GetStatus(DeviceHandle device, ReadbackTicket ticket) noexcept;
/// Transfer the Ready uploaded buffer into a null output and clear its ticket.
/// Pending/failure preserves both outputs; source bytes are no longer borrowed.
[[nodiscard]] RasterStatus TakeUploadedBuffer(DeviceHandle device, UploadTicket& ticket, BufferHandle& output) noexcept;
/// Copy all Ready readback bytes to caller storage; never exposes a native mapping.
/// @param device Ready owner of the readback ticket.
/// @param ticket Ready owned request; copying does not release it.
/// @param output Non-null destination with capacity at least the requested size in bytes.
/// @param capacity Destination capacity in bytes.
/// Rejection/Pending preserves destination bytes. Success keeps the ticket until Release.
[[nodiscard]] RasterStatus CopyReadback(DeviceHandle device,
                                        ReadbackTicket ticket,
                                        ludus::foundation::uint8* output,
                                        ludus::foundation::usize capacity) noexcept;
/// Cancel/drop an upload ticket and clear it. In-flight GPU staging remains retained until safe.
[[nodiscard]] RasterStatus Release(DeviceHandle device, UploadTicket& ticket) noexcept;
/// Drop a readback ticket and clear it. In-flight mapping/copy and source leases retire safely.
[[nodiscard]] RasterStatus Release(DeviceHandle device, ReadbackTicket& ticket) noexcept;
/// Copy a canonical field-wise pipeline description and retain dependencies before return.
/// Repeated requests using identical immutable shader/layout incarnations and fixed state share
/// one backend pipeline; keys compare every active field, never pointers, hashes or padding.
/// Ready/Pending publishes one independent request. Null output required; failures preserve it.
/// PollLifetime creates at most one pending pipeline per call on the device/context owner.
/// WebGPU driver creation is asynchronous; native/WebGL driver creation may occupy that call.
[[nodiscard]] RasterStatus
RequestPipeline(DeviceHandle device, const RasterPipelineDescription& description, PipelineRequest& output) noexcept;
/// Poll a pipeline request; Pending/failure is explicit and never switches backend or creates a fallback.
[[nodiscard]] RasterStatus GetStatus(DeviceHandle device, PipelineRequest request) noexcept;
/// Obtain a borrowed Ready pipeline for recording/drawing while this request is owned.
/// Null output required. Do not Destroy the borrowed pipeline (InvalidState); release its request.
/// Accepted batches retain it beyond request release. Pending/failure preserves output.
[[nodiscard]] RasterStatus
GetRequestedPipeline(DeviceHandle device, PipelineRequest request, RasterPipelineHandle& output) noexcept;
/// Release one independent request and clear it; the last request detaches the cached pipeline.
/// Pending native callbacks and accepted CPU/GPU work retain their own records until retirement.
[[nodiscard]] RasterStatus Release(DeviceHandle device, PipelineRequest& request) noexcept;
/// Drain transfer/creation completion and launch at most one queued pipeline without blocking on GPU work.
/// Call between frames on the owner thread, including later browser event-loop turns.
/// Admission failure is explicit. Does not pump a browser event loop or promise a driver time budget.
[[nodiscard]] RasterStatus PollLifetime(DeviceHandle device) noexcept;
} // namespace ludus::graphics::rhi
