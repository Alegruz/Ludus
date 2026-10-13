#pragma once

#include <ludus/foundation/base/types.h>

#include <ludus/graphics/rhi/device.h>
#include <ludus/graphics/rhi/lifetime.h>

namespace ludus::graphics::rhi
{
namespace internal
{
struct NativeInteropAccess;
}

/// Exact native context for the single-queue sharing profile. These are borrowed
/// objects, not resource owners. All objects must come from one live native context.
/// Dispatchable Vulkan handles are converted to void*; do not pass pointers to handles.
struct NativeDeviceIdentity final
{
    /// Metal or Vulkan; browser APIs have no native sharing profile.
    Backend Api = Backend::Vulkan;
    /// VkInstance for Vulkan; null for Metal.
    void* Instance = nullptr;
    /// VkPhysicalDevice for Vulkan; null for Metal. Matching this alone is insufficient.
    void* PhysicalDevice = nullptr;
    /// VkDevice or id<MTLDevice>, borrowed on the owning thread.
    void* Device = nullptr;
    /// VkQueue or id<MTLCommandQueue>. Both producers and consumers use this exact queue.
    void* CommandQueue = nullptr;
    /// Created graphics queue family for Vulkan; zero for Metal.
    ludus::foundation::uint32 QueueFamily = 0;
    /// Created queue index in QueueFamily for Vulkan; zero for Metal.
    ludus::foundation::uint32 QueueIndex = 0;
};

/// Source-owner handshake for admitting an existing native device. Callbacks run
/// on the device owner thread and must not reenter RHI. They never transfer native
/// device destruction to RHI. The source must retain the context until Release.
struct NativeDeviceLifetime final
{
    /// Source owner, valid through the balanced Release callback.
    void* Context = nullptr;
    /// Retain the source context; false rejects admission without calling Release.
    bool (*Retain)(void*) noexcept = nullptr;
    /// Called exactly once after each successful Retain, following native cleanup.
    void (*Release)(void*) noexcept = nullptr;
};

/// Copied admission request. A successful source Retain promises the instance,
/// device and queue remain valid until Release, including shutdown and loss cleanup.
/// Vulkan metadata must identify an actually created queue on the declared device.
struct NativeDeviceDescription final
{
    /// Native context to admit; physical and logical identity must agree.
    NativeDeviceIdentity Identity;
    /// Required lifetime handshake; the source controls destruction of its context.
    NativeDeviceLifetime Lifetime;
    /// Vulkan instance API version in Vulkan's packed encoding, at least 1.1; zero for Metal.
    ludus::foundation::uint32 InstanceApiVersion = 0;
    /// Require producer and consumer queue submissions to be serialized on the RHI
    /// owner thread. Different queues and concurrent external submission are unsupported.
    bool SerializedOwnerQueue = false;
};

/// Negotiated native sharing profile; query only after device startup is Ready.
struct NativeInteropCapabilities final
{
    /// Headless RHI rendering on an existing Metal/Vulkan device and queue.
    bool DeviceAdmission = false;
    /// Leased single-sample, single-mip/layer D2 Rgba8Unorm color attachment reads.
    bool ColorTextureReads = false;
    /// Maximum live leases per native lease kind, bounded by R6 ResourcesPerKind.
    ludus::foundation::uint32 MaxLeases = 0;
};

/// Pins one native context. Copies do not add ownership; release exactly once after
/// destroying every foreign wrapper/device that borrows the exported objects.
struct NativeDeviceLease final
{
private:
    ludus::foundation::uint64 Owner = 0, Generation = 0;
    ludus::foundation::uint32 Slot = 0;
    /// Grants the bridge registry access to validate retained incarnations.
    friend struct internal::NativeInteropAccess;
};

/// Pins one texture incarnation and excludes RHI writes until foreign GPU reads
/// complete. Copies do not add ownership. Foreign consumers must stop recording
/// new reads before ReleaseNativeTextureRead; destroy their wrappers before final release.
struct NativeTextureRead final
{
private:
    ludus::foundation::uint64 Owner = 0, Generation = 0;
    ludus::foundation::uint32 Slot = 0;
    /// Grants the bridge registry access to validate retained incarnations.
    friend struct internal::NativeInteropAccess;
};

/// Borrowed native context snapshot, valid only while its NativeDeviceLease is held.
struct NativeDeviceInfo final
{
    /// Exact logical device, instance and command queue used by RHI.
    NativeDeviceIdentity Identity;
    /// RHI session incarnation; native pointer reuse does not revive an old lease.
    ludus::foundation::uint64 Incarnation = 0;
    /// Metal registry ID, or zero on Vulkan.
    ludus::foundation::uint64 RegistryId = 0;
    /// Vulkan device UUID, or all zeros on Metal.
    ludus::foundation::uint8 DeviceUuid[16]{};
    /// Vulkan instance API version, or zero on Metal.
    ludus::foundation::uint32 InstanceApiVersion = 0;
    /// Shutdown has invalidated ordinary RHI handles; leased native objects remain
    /// alive until consumer completion and lease release. No new reads may be acquired.
    bool Retiring = false;
};

/// Borrowed image descriptor; no ownership or GPU completion is implied by copying it.
/// Queue ordering makes a newly acquired read usable without CPU waiting: enqueue and submit
/// its foreign reads on Device.Identity.CommandQueue after acquisition returns.
struct NativeTextureInfo final
{
    /// Pinned exact native context and RHI incarnation.
    NativeDeviceInfo Device;
    /// Texture state revision captured at publication.
    ludus::foundation::uint64 Revision = 0;
    /// Producer submission ordinal, retained independently of the public texture owner.
    ludus::foundation::uint64 ProducerOrdinal = 0;
    /// Shared color encoding; currently Rgba8Unorm only.
    RasterFormat Format = RasterFormat::Rgba8Unorm;
    /// Pixel width of the single mip/layer; texture origin is top-left.
    ludus::foundation::uint32 Width = 0;
    /// Pixel height of the single mip/layer; no scaling is performed by the bridge.
    ludus::foundation::uint32 Height = 0;
    /// id<MTLTexture> for Metal; null for Vulkan. Valid only during the read lease.
    void* MetalTexture = nullptr;
    /// VkImage bits for Vulkan; zero for Metal. Convert according to the native VkImage type.
    ludus::foundation::uint64 VulkanImage = 0;
    /// Current Vulkan image layout: VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; zero for Metal.
    ludus::foundation::uint32 VulkanLayout = 0;
    /// Native image is borrowed for sampled reads only; foreign writes/ownership transfers reject.
    bool SampledReadOnly = true;
};

/// Admit a retained foreign device for headless RHI rendering, without creating or
/// destroying that source's instance/device/queue. Width/height are initial pixels.
/// @pre Native objects and queue provenance are valid; all calls/submissions use the owner thread.
/// @return Ready on success; Unsupported for unsupported backend/profile, or an explicit
/// admission failure. Device/surface outputs are preserved on rejection. Vulkan ray queries
/// and BC formats are not enabled by foreign-device admission.
DeviceStatus CreateNativeDevice(const ApplicationInfo& application,
                                const NativeDeviceDescription& native,
                                const DeviceDescription& description,
                                ludus::foundation::uint32 width,
                                ludus::foundation::uint32 height,
                                DeviceHandle& device,
                                SurfaceHandle& surface) noexcept;

/// Query enabled native sharing; outputs are preserved unless Ready is returned.
RasterStatus GetNativeInteropCapabilities(DeviceHandle device, NativeInteropCapabilities& output) noexcept;
/// Pin the exact native context of a Ready RHI device. An occupied output rejects.
/// Shutdown defers native cleanup while native device/image leases remain live.
RasterStatus AcquireNativeDevice(DeviceHandle device, NativeDeviceLease& output) noexcept;
/// Get the pinned context, including after ordinary device shutdown; preserve output on failure.
RasterStatus GetNativeDeviceInfo(NativeDeviceLease handle, NativeDeviceInfo& output) noexcept;
/// Release a device pin after every associated texture read and foreign native borrower
/// has been retired. InvalidState preserves the lease when texture reads still refer to it.
RasterStatus ReleaseNativeDevice(NativeDeviceLease& handle) noexcept;
/// Publish an initialized attachment for same-context sampled reads. Producer must be
/// the exact last accepted RHI submission using this texture. A native queue barrier
/// orders producer writes before consumer reads; no CPU pixel transfer or completion wait.
/// @param device Open context lease for the Ready RHI session, on its owner thread.
/// @param consumer Exact actual consumer context; physical adapter agreement alone is insufficient.
/// @param handle Initialized single-sample RGBA8 attachment incarnation to retain.
/// @param producer Its most recent accepted RHI submission; GPU completion need not have occurred.
/// @param output Null read lease; receives ownership only on Ready.
/// @param info Borrowed native descriptor; preserved unless Ready is returned.
/// @return Ready publishes both outputs; all failures preserve both. Writes, including
/// graph attachments and mutable transfers, reject until the read is fully released.
RasterStatus AcquireNativeTextureRead(NativeDeviceLease device,
                                      const NativeDeviceIdentity& consumer,
                                      TextureHandle handle,
                                      SubmissionToken producer,
                                      NativeTextureRead& output,
                                      NativeTextureInfo& info) noexcept;
/// Query the borrowed descriptor only while its read is open; preserve output on failure.
RasterStatus GetNativeTextureInfo(NativeTextureRead handle, NativeTextureInfo& output) noexcept;
/// Stop new foreign reads and enqueue a completion marker after all already-submitted
/// foreign work on the shared queue. Invoke after actual queue submission, not recording.
/// Pending preserves the handle and backing; poll/repeat until Ready clears the handle.
/// Rejection preserves the handle; terminal loss permits safe retirement. Owner thread only.
RasterStatus ReleaseNativeTextureRead(NativeTextureRead& read) noexcept;
/// Explicit maintenance/teardown variant of ReleaseNativeTextureRead. May block until
/// previously submitted shared-queue GPU work finishes; never call in presentation.
/// Clears the handle on Ready and preserves it on nonterminal failures.
RasterStatus DrainNativeTextureRead(NativeTextureRead& read) noexcept;
/// Ready means an open queue-ordered read, or a completed release marker; Pending means
/// foreign release is still in flight. DeviceLost is terminal. No CPU pixel readback occurs.
RasterStatus GetStatus(NativeTextureRead handle) noexcept;
} // namespace ludus::graphics::rhi
