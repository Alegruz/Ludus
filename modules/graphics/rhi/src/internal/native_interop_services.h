#pragma once

// Thanks to The Qt Company, "QSGMetalTexture", "QSGVulkanTexture" and
// "QSGRendererInterface" (Qt Quick native interfaces): native wrappers borrow
// images on an exact device, and recording completion is not GPU completion.
// https://doc.qt.io/qt-6/qsgrendererinterface.html
// This Qt-independent bridge adds retained identities and shared-queue retirement.
namespace ludus::graphics::rhi::internal
{
struct NativeInteropAccess final
{
    template <typename T>
    static uint64 Owner(T value) noexcept
    {
        return value.Owner;
    }
    template <typename T>
    static uint64 Generation(T value) noexcept
    {
        return value.Generation;
    }
    template <typename T>
    static usize Slot(T value) noexcept
    {
        return value.Slot == 0 ? RASTER_INVALID : value.Slot - 1;
    }
    template <typename T>
    static void Set(T& value, const RasterIdentity& identity) noexcept
    {
        value.Owner = identity.Owner;
        value.Slot = static_cast<uint32>(identity.Slot + 1);
        value.Generation = identity.Generation;
    }
};
} // namespace ludus::graphics::rhi::internal
namespace ludus::graphics::rhi
{
namespace
{
struct NativeLeaseRecord
{
    uint64 Owner = 0, Generation = 1;
};
struct NativeDeviceRecord final : NativeLeaseRecord
{
    DeviceHandle Session;
    NativeDeviceInfo Info;
    usize Reads = 0;
};
struct NativeReadRecord final : NativeLeaseRecord
{
    usize Device = RASTER_INVALID, Texture = RASTER_INVALID;
    NativeTextureInfo Info;
    bool Releasing = false;
};
StorageTable<NativeDeviceRecord> gNativeDevices;
StorageTable<NativeReadRecord> gNativeReads;
NativeDeviceDescription gNativeSource;
uint64 gNativePins = 0;
bool gNativeSourcePinned = false, gNativeCallback = false, gNativeRetiring = false, gNativeTerminalLoss = false;

bool SameNativeContext(const NativeDeviceIdentity& a, const NativeDeviceIdentity& b) noexcept
{
    return a.Api == b.Api && a.Instance == b.Instance && a.PhysicalDevice == b.PhysicalDevice && a.Device == b.Device &&
           a.CommandQueue == b.CommandQueue && a.QueueFamily == b.QueueFamily && a.QueueIndex == b.QueueIndex;
}
template <typename T, typename H>
T* NativeResolve(StorageTable<T>& records, H handle) noexcept
{
    const auto slot = NativeInteropAccess::Slot(handle);
    if (NativeInteropAccess::Owner(handle) == 0 || slot >= records.GetSize())
    {
        return nullptr;
    }
    auto& record = records[slot];
    return record.Owner == NativeInteropAccess::Owner(handle) &&
                   record.Generation == NativeInteropAccess::Generation(handle)
               ? &record
               : nullptr;
}
template <typename T>
RasterStatus NativeReserve(StorageTable<T>& records, usize& slot) noexcept
{
    const usize limit = CurrentStorageLimits().ResourcesPerKind;
    bool exhausted = false;
    for (slot = 0; slot < limit; ++slot)
    {
        if (slot == records.GetSize() && !records.EnsureSlots(slot + 1))
        {
            return StorageFailure();
        }
        auto& record = records[slot];
        exhausted |= record.Generation == 0;
        if (record.Owner == 0 && record.Generation != 0)
        {
            return RasterStatus::Ready;
        }
    }
    return exhausted ? RasterStatus::IdentityExhausted : RasterStatus::CapacityExceeded;
}
template <typename T>
void NativeReset(T& record) noexcept
{
    const auto generation = RasterNextGeneration(record.Generation);
    record = {};
    record.Generation = generation;
}
NativeDeviceInfo NativeSnapshot(const NativeDeviceRecord& record) noexcept
{
    auto result = record.Info;
    result.Retiring = gNativeRetiring || GetStartup().State != StartupState::Ready;
    return result;
}
RasterStatus CloseNativeRead(NativeTextureRead& handle, bool wait) noexcept
{
    auto* read = NativeResolve(gNativeReads, handle);
    if (read == nullptr)
    {
        return RasterStatus::InvalidHandle;
    }
    if (FrameOpen())
    {
        return RasterStatus::InvalidState;
    }
    const auto slot = NativeInteropAccess::Slot(handle);
    if (!read->Releasing)
    {
        const auto released = backend::NativeTextureRelease(slot);
        if (released != RasterStatus::Ready && released != RasterStatus::Pending &&
            released != RasterStatus::DeviceLost)
        {
            return released;
        }
        read->Releasing = true;
    }
    const auto completed = backend::NativeTexturePoll(slot, wait);
    if (completed != RasterStatus::Ready && completed != RasterStatus::DeviceLost)
    {
        return completed;
    }
    backend::NativeTextureDestroy(slot);
    auto& texture = At(RasterKind::Texture, read->Texture);
    --texture.ExternalReads;
    Release(RasterKind::Texture, read->Texture);
    --gNativeDevices[read->Device].Reads;
    NativeReset(*read);
    handle = {};
    if (!gNativeRetiring)
    {
        Collect();
    }
    return RasterStatus::Ready;
}
} // namespace
namespace internal
{
bool NativeInteropBusy() noexcept
{
    return gNativeCallback || gNativeRetiring;
}
bool DeferNativeInteropShutdown() noexcept
{
    if (gNativePins == 0)
    {
        return false;
    }
    gNativeRetiring = true;
    gNativeTerminalLoss |= GetStartup().State == StartupState::DeviceLost;
    return true;
}
const NativeDeviceDescription* NativeDeviceRequest() noexcept
{
    return gNativeSourcePinned ? &gNativeSource : nullptr;
}
void ReleaseNativeDeviceSource() noexcept
{
    if (!gNativeSourcePinned)
    {
        return;
    }
    const auto lifetime = gNativeSource.Lifetime;
    gNativeSource = {};
    gNativeSourcePinned = false;
    gNativeCallback = true;
    lifetime.Release(lifetime.Context);
    gNativeCallback = false;
}
DeviceStatus PrepareNativeDeviceSource(const NativeDeviceDescription& description) noexcept
{
    const auto& identity = description.Identity;
    if (!backend::NativeInteropSupported() || identity.Api != backend::Kind())
    {
        return DeviceStatus::Unsupported;
    }
    if (!description.SerializedOwnerQueue)
    {
        return DeviceStatus::Unsupported;
    }
    if (identity.Device == nullptr || identity.CommandQueue == nullptr || description.Lifetime.Context == nullptr ||
        description.Lifetime.Retain == nullptr || description.Lifetime.Release == nullptr)
    {
        return DeviceStatus::InvalidDescription;
    }
    if (identity.Api == Backend::Vulkan)
    {
        constexpr uint32 VULKAN_1_1 = (1U << 22) | (1U << 12);
        if (identity.Instance == nullptr || identity.PhysicalDevice == nullptr)
        {
            return DeviceStatus::InvalidDescription;
        }
        if (description.InstanceApiVersion < VULKAN_1_1)
        {
            return DeviceStatus::Unsupported;
        }
    }
    else if (identity.Instance != nullptr || identity.PhysicalDevice != nullptr || identity.QueueFamily != 0 ||
             identity.QueueIndex != 0 || description.InstanceApiVersion != 0)
    {
        return DeviceStatus::InvalidDescription;
    }
    gNativeCallback = true;
    const bool retained = description.Lifetime.Retain(description.Lifetime.Context);
    gNativeCallback = false;
    if (!retained)
    {
        return DeviceStatus::Failed;
    }
    gNativeSource = description;
    gNativeSourcePinned = true;
    return DeviceStatus::Ready;
}
void NativeInteropShutdownFinished() noexcept
{
    gNativeRetiring = false;
    gNativeTerminalLoss = false;
    ReleaseNativeDeviceSource();
}
} // namespace internal
RasterStatus GetNativeInteropCapabilities(DeviceHandle device, NativeInteropCapabilities& output) noexcept
{
    const auto admission = Admission(device, false);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    if (!backend::NativeInteropSupported())
    {
        return RasterStatus::Unsupported;
    }
    output = {true, true, CurrentStorageLimits().ResourcesPerKind};
    return RasterStatus::Ready;
}
RasterStatus AcquireNativeDevice(DeviceHandle device, NativeDeviceLease& output) noexcept
{
    const auto admission = Admission(device);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    if (NativeInteropAccess::Owner(output) != 0)
    {
        return RasterStatus::InvalidState;
    }
    if (!backend::NativeInteropSupported())
    {
        return RasterStatus::Unsupported;
    }
    NativeDeviceInfo info;
    const auto snapshot = backend::NativeDeviceSnapshot(info);
    if (snapshot != RasterStatus::Ready)
    {
        return snapshot;
    }
    usize slot = 0;
    const auto reserved = NativeReserve(gNativeDevices, slot);
    if (reserved != RasterStatus::Ready)
    {
        return reserved;
    }
    auto& record = gNativeDevices[slot];
    record.Owner = DeviceOwner();
    record.Session = device;
    info.Incarnation = record.Owner;
    record.Info = info;
    ++gNativePins;
    NativeInteropAccess::Set(output, {record.Owner, slot, record.Generation});
    return RasterStatus::Ready;
}
RasterStatus GetNativeDeviceInfo(NativeDeviceLease handle, NativeDeviceInfo& output) noexcept
{
    const auto* record = NativeResolve(gNativeDevices, handle);
    if (record == nullptr)
    {
        return RasterStatus::InvalidHandle;
    }
    output = NativeSnapshot(*record);
    return RasterStatus::Ready;
}
RasterStatus ReleaseNativeDevice(NativeDeviceLease& handle) noexcept
{
    auto* record = NativeResolve(gNativeDevices, handle);
    if (record == nullptr)
    {
        return RasterStatus::InvalidHandle;
    }
    if (FrameOpen() || record->Reads != 0)
    {
        return RasterStatus::InvalidState;
    }
    NativeReset(*record);
    --gNativePins;
    handle = {};
    if (gNativePins == 0 && gNativeRetiring)
    {
        FinishNativeInteropShutdown();
    }
    return RasterStatus::Ready;
}
RasterStatus AcquireNativeTextureRead(NativeDeviceLease device,
                                      const NativeDeviceIdentity& consumer,
                                      TextureHandle handle,
                                      SubmissionToken producer,
                                      NativeTextureRead& output,
                                      NativeTextureInfo& info) noexcept
{
    auto* native = NativeResolve(gNativeDevices, device);
    if (native == nullptr)
    {
        return RasterStatus::InvalidHandle;
    }
    if (gNativeRetiring || native->Owner != DeviceOwner())
    {
        return RasterStatus::InvalidState;
    }
    const auto admission = Admission(native->Session);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    if (NativeInteropAccess::Owner(output) != 0)
    {
        return RasterStatus::InvalidState;
    }
    if (!SameNativeContext(native->Info.Identity, consumer))
    {
        return RasterStatus::Unsupported;
    }
    auto* texture = Resolve(handle, RasterKind::Texture);
    if (texture == nullptr)
    {
        return RasterStatus::InvalidHandle;
    }
    if (texture->Status != RasterStatus::Ready)
    {
        return texture->Status == RasterStatus::Pending ? RasterStatus::NotReady : texture->Status;
    }
    const auto& description = texture->Texture;
    if (!description.Attachment || description.Format != RasterFormat::Rgba8Unorm ||
        description.Dimension != TextureDimension::D2 || description.MipLevels != 1 || description.DepthOrLayers != 1 ||
        description.SampleCount != 1)
    {
        return RasterStatus::Unsupported;
    }
    if (!texture->ColorDefined)
    {
        return RasterStatus::InvalidDescription;
    }
    if (texture->TextureRevision == ~uint64{0})
    {
        return RasterStatus::IdentityExhausted;
    }
    const auto ordinal = LifetimeAccess::Ordinal(producer);
    if (LifetimeAccess::Owner(producer) != native->Owner || ordinal == 0 || ordinal != texture->LastUse ||
        ordinal < gFirstAcceptedSubmission || ordinal > gLastAcceptedSubmission)
    {
        return RasterStatus::InvalidHandle;
    }
    // An accepted producer can still be in flight; the shared native queue barrier
    // orders the following foreign sample without a CPU completion wait.
    usize slot = 0;
    const auto reserved = NativeReserve(gNativeReads, slot);
    if (reserved != RasterStatus::Ready)
    {
        return reserved;
    }
    NativeTextureInfo result;
    const auto textureSlot = RasterAccess::Slot(handle);
    Retain(RasterKind::Texture, textureSlot);
    const auto published = backend::NativeTexturePublish({slot, textureSlot}, result);
    if (published != RasterStatus::Ready)
    {
        Release(RasterKind::Texture, textureSlot);
        backend::NativeTextureDestroy(slot);
        return published;
    }
    texture->TextureUse = RasterTextureUse::SampledFragment;
    ++texture->TextureRevision;
    ++texture->ExternalReads;
    result.Device = NativeSnapshot(*native);
    result.Revision = texture->TextureRevision;
    result.ProducerOrdinal = ordinal;
    result.Format = description.Format;
    result.Width = description.Width;
    result.Height = description.Height;
    auto& read = gNativeReads[slot];
    read.Owner = native->Owner;
    read.Device = NativeInteropAccess::Slot(device);
    read.Texture = textureSlot;
    read.Info = result;
    ++native->Reads;
    NativeInteropAccess::Set(output, {read.Owner, slot, read.Generation});
    info = result;
    return RasterStatus::Ready;
}
RasterStatus GetNativeTextureInfo(NativeTextureRead handle, NativeTextureInfo& output) noexcept
{
    const auto* read = NativeResolve(gNativeReads, handle);
    if (read == nullptr)
    {
        return RasterStatus::InvalidHandle;
    }
    if (read->Releasing)
    {
        return RasterStatus::InvalidState;
    }
    if (gNativeTerminalLoss || (read->Owner == DeviceOwner() && GetStartup().State == StartupState::DeviceLost))
    {
        return RasterStatus::DeviceLost;
    }
    const auto status = backend::NativeTexturePoll(NativeInteropAccess::Slot(handle), false);
    if (status == RasterStatus::DeviceLost || status == RasterStatus::Failed)
    {
        return status;
    }
    auto result = read->Info;
    result.Device = NativeSnapshot(gNativeDevices[read->Device]);
    output = result;
    return RasterStatus::Ready;
}
RasterStatus ReleaseNativeTextureRead(NativeTextureRead& read) noexcept
{
    return CloseNativeRead(read, false);
}
RasterStatus DrainNativeTextureRead(NativeTextureRead& read) noexcept
{
    return CloseNativeRead(read, true);
}
RasterStatus GetStatus(NativeTextureRead handle) noexcept
{
    const auto* read = NativeResolve(gNativeReads, handle);
    if (read == nullptr)
    {
        return RasterStatus::InvalidHandle;
    }
    if (gNativeTerminalLoss || (read->Owner == DeviceOwner() && GetStartup().State == StartupState::DeviceLost))
    {
        return RasterStatus::DeviceLost;
    }
    const auto status = backend::NativeTexturePoll(NativeInteropAccess::Slot(handle), false);
    return !read->Releasing && status == RasterStatus::Pending ? RasterStatus::Ready : status;
}
} // namespace ludus::graphics::rhi
