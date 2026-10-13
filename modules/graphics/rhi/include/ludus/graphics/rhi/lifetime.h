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
/// Owner-thread R6 accounting; bytes are engine storage/logical resources, not physical VRAM.
struct StorageUsage final
{
    /// Currently committed host storage, including retained page slack and backend metadata.
    ludus::foundation::usize HostBytes = 0;
    /// Maximum committed host bytes, including overlap during growth, for this process storage epoch.
    ludus::foundation::usize PeakHostBytes = 0;
    /// Current logical buffer/texture bytes, retained through pending callbacks/GPU completion.
    ludus::foundation::uint64 ResourceBytes = 0;
    /// Maximum simultaneous logical resource bytes in this storage epoch.
    ludus::foundation::uint64 PeakResourceBytes = 0;
    /// Published general resources, including resources still being created.
    ludus::foundation::uint64 PublishedRecords = 0;
    /// General resources with unfinished creation callbacks.
    ludus::foundation::uint64 PendingRecords = 0;
    /// Resources whose public owner was destroyed but dependencies/GPU work retain storage.
    ludus::foundation::uint64 DetachedRecords = 0;
};
/// Copy current accounting without waiting or allocating; owner-thread only.
/// Invalid handles preserve output. Other unavailable states zero output and report their status.
[[nodiscard]] RasterStatus GetStorageUsage(DeviceHandle, StorageUsage&) noexcept;
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
/// Owned R8 mutation request; copied source bytes and resource leases survive cancellation.
/// Owner-thread use between frames. Release exactly once, including after failure.
/// Cancellation drops notification ownership; it does not undo accepted GPU writes.
struct TransferTicket final
{
private:
    ludus::foundation::uint64 Owner = 0;
    ludus::foundation::uint64 Generation = 0;
    ludus::foundation::uint32 Slot = 0;
    /// Grants private registry access to this request incarnation.
    friend struct internal::LifetimeAccess;
};
/// Top-left texel rectangle in one allocated 2D mip; all dimensions are in pixels.
struct TextureRegion final
{
    /// Zero-based mip level.
    ludus::foundation::uint32 Mip = 0;
    /// First column in the mip.
    ludus::foundation::uint32 X = 0;
    /// First top-oriented row in the mip.
    ludus::foundation::uint32 Y = 0;
    /// Nonzero rectangle width.
    ludus::foundation::uint32 Width = 0;
    /// Nonzero rectangle height.
    ludus::foundation::uint32 Height = 0;
};
/// Logical initialization of allocated mips; does not measure physical GPU residency.
struct TextureResidency final
{
    /// Allocated mip count; backing for every mip is reserved at creation.
    ludus::foundation::uint32 MipLevels = 0;
    /// Bit i proves that every texel of mip i has initialized contents.
    ludus::foundation::uint32 InitializedMips = 0;
    /// Logical texel bytes of initialized mips, excluding padding and private depth.
    ludus::foundation::uint64 InitializedBytes = 0;
};
/// Copy logical mip initialization without waiting; Pending writes do not publish new bits.
/// Invalid handles preserve output. Owner-thread only; no sparse-memory or eviction promise.
[[nodiscard]] RasterStatus GetTextureResidency(DeviceHandle, TextureHandle, TextureResidency&) noexcept;
/// Submit a copied update to an existing buffer between frames. Offset/size are multiples
/// of four, nonzero and within the allocation and TransferBytes. All buffer roles are legal.
/// Pending publishes a null ticket; other results preserve it. Ready buffers with CPU leases
/// (bindings, batches, graphs, ray inputs, transfers) or unfinished GPU uses reject InvalidState.
/// Accepted writes make the destination Pending until actual completion; another request returns NotReady.
/// Release the completed ticket before another mutation; cancellation cannot undo accepted writes.
[[nodiscard]] RasterStatus RequestBufferUpdate(DeviceHandle,
                                               BufferHandle,
                                               ludus::foundation::usize offset,
                                               const ludus::foundation::uint8* bytes,
                                               ludus::foundation::usize size,
                                               TransferTicket&) noexcept;
/// Copy caller rows into owned staging and submit a bounded RGBA8Unorm mip rectangle update.
/// Row pitch is a multiple of four, at least Width*4; byteCount must equal RowPitch*Height.
/// Internal 256-byte row packing must also fit TransferBytes. CPU-retained textures reject Unsupported.
/// Destination leases/in-flight uses reject InvalidState. An undefined mip requires its complete
/// rectangle; success defines that mip after completion. Output publication/cancellation matches buffer updates.
[[nodiscard]] RasterStatus RequestTextureUpdate(DeviceHandle,
                                                TextureHandle,
                                                const TextureRegion&,
                                                const ludus::foundation::uint8* bytes,
                                                ludus::foundation::usize byteCount,
                                                ludus::foundation::usize rowPitch,
                                                TransferTicket&) noexcept;
/// Submit an exact-format RGBA8Unorm GPU copy between distinct textures/mips of equal rectangle size.
/// Source mip must be initialized; source ownership is retained through completion. Destination mutation
/// obeys RequestTextureUpdate's lease/initialization rules. Pending publishes a null ticket; rejection preserves it.
[[nodiscard]] RasterStatus RequestTextureCopy(DeviceHandle,
                                              TextureHandle source,
                                              const TextureRegion& sourceRegion,
                                              TextureHandle destination,
                                              const TextureRegion& destinationRegion,
                                              TransferTicket&) noexcept;
/// Schedule RGBA8Unorm texel readback into owned staging; source mip must be initialized.
/// Caller row pitch is four-byte aligned and at least Width*4; both caller and 256-byte staging
/// rows must fit TransferBytes. CopyReadback returns RowPitch*Height top-oriented bytes with zero padding.
/// Call on the ready device's owner thread between frames, with a null output ticket.
/// Accepted submission returns Pending and publishes an owned ticket; poll GetStatus and release it once.
/// Rejection preserves output. The source survives public destruction.
[[nodiscard]] RasterStatus RequestTextureReadback(DeviceHandle,
                                                  TextureHandle,
                                                  const TextureRegion&,
                                                  ludus::foundation::usize rowPitch,
                                                  ReadbackTicket&) noexcept;
/// Nonblocking mutation completion poll. Ready proves execution; failure remains explicit.
[[nodiscard]] RasterStatus GetStatus(DeviceHandle, TransferTicket) noexcept;
/// Drop notification ownership and clear the ticket. Leases/staging retire only after GPU completion.
/// Invalid/stale handles preserve the ticket; cancellation does not roll back writes.
[[nodiscard]] RasterStatus Release(DeviceHandle, TransferTicket&) noexcept;
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
/// Effective configured service maxima and configured transfer budgets for a ready portable-raster device.
struct LifetimeCapabilities final
{
    /// Number of recording/finished batches, including retained dependency records.
    ludus::foundation::uint32 CommandBatches = 0;
    /// Draw packets per batch; recording consumes no native GPU commands.
    ludus::foundation::uint32 DrawsPerBatch = 0;
    /// Simultaneous uploads and readbacks independently, including cancelled in-flight slots.
    ludus::foundation::uint32 TransfersPerDirection = 0;
    /// Configured bytes in one upload/readback ring slice, including internal row padding.
    ludus::foundation::usize TransferBytes = 0;
    /// Simultaneous independently owned pipeline requests.
    ludus::foundation::uint32 PipelineRequests = 0;
};
/// Query configured budgets; invalid owner preserves output, unavailable device writes zero budgets.
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
/// Preflight/admission failure preserves batch and completion. Sampled textures
/// must have defined contents and a committed sampled use covering binding visibility.
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
