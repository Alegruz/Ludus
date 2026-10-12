#include "internal/backend.h"
#include <ludus/foundation/base/checked_integer.hpp>

#include "internal/measurement.h"

#include <ludus/graphics/rhi/graph.h>

#include "internal/lifecycle.h"
#include "internal/profile.h"
#include "internal/raster.h"
#include "internal/ray_backend.h"

#include <ludus/foundation/base/pointer.hpp>
#include <ludus/foundation/containers/array.hpp>

#include <new>

#include <cstring>

// Thanks to Khronos, "WebGL 2.0 Specification", sections 3.7.3 (buffer objects),
// 3.7.6 (textures) and 3.7.9 (drawing buffers), for the deliberately explicit
// range/packing and mesh-local indexed-draw boundaries:
// https://registry.khronos.org/webgl/specs/latest/2.0/
// Ownership/retention follows docs/architecture/rhi-gdi.md; this is the RHI's
// single portable registry. Backend objects never become public identities.
namespace ludus::graphics::rhi
{
namespace internal
{
struct RasterIdentity final
{
    foundation::uint64 Owner;
    foundation::usize Slot;
    foundation::uint64 Generation;
};
struct RasterAccess final
{
    template <typename T>
    static foundation::uint64 Owner(T handle) noexcept
    {
        return handle.Owner;
    }
    template <typename T>
    static foundation::uint64 Generation(T handle) noexcept
    {
        return handle.Generation;
    }
    template <typename T>
    static foundation::usize Slot(T handle) noexcept
    {
        return handle.Slot == 0 ? RASTER_INVALID : handle.Slot - 1;
    }
    template <typename T>
    static void Set(T& handle, const RasterIdentity& identity) noexcept
    {
        handle.Owner = identity.Owner;
        handle.Slot = static_cast<foundation::uint32>(identity.Slot + 1);
        handle.Generation = identity.Generation;
    }
};
} // namespace internal

namespace
{
using namespace foundation;
using namespace internal;
constexpr usize KINDS = static_cast<usize>(RasterKind::Count);
struct CpuTextureRecord final
{
    StorageArray<uint8> Bytes;
    TextureMipUpload Mips[16];
};
struct RayRecordInfo final
{
    StorageArray<usize> GeometrySlots;
    StorageArray<float32> TriangleUvs;
    usize Scene = 0, Program = 0, Count = 0;
    RayAllocation Allocation;
};
struct Record final
{
    core::UniquePtr<RayRecordInfo> Ray;
    core::UniquePtr<CpuTextureRecord> CpuTexture;
    uint64 Owner = 0;
    uint64 Generation = 1;
    uint64 LastUse = 0;
    uint32 Request = 0;
    uint32 Callbacks = 0;
    usize References = 0;
    RasterStatus Status = RasterStatus::Failed;
    bool Published = false;
    bool InFrame = false;
    bool ServiceOwned = false;
    bool QueuedPipeline = false;
    BufferDescription Buffer;
    uint64 BufferRevision = 1;
    bool FrameBufferWritten = false;
    ComputePipelineInfo Compute;
    TextureDescription Texture;
    uint64 TextureRevision = 1;
    RasterTextureUse TextureUse = RasterTextureUse::Undefined;
    RasterTextureUse FrameTextureUse = RasterTextureUse::Undefined;
    bool ColorDefined = false;
    bool DepthDefined = false;
    bool FrameColorDefined = false;
    bool FrameDepthDefined = false;
    usize TextureSlot = 0;
    RasterShaderInfo Shader;
    RasterLayout Layout;
    RasterSet Set;
    RasterPipelineInfo Pipeline;
    StorageArray<uint8> Indices;
    uint64 StorageBytes = 0;
    usize AuxiliaryHostBytes = 0;
};
constexpr usize RASTER_KINDS = static_cast<usize>(RasterKind::RayGeometry);
StorageTable<Record> gRecords[KINDS];
RasterSequence<uint32> gRequests;
RasterSequence<uint64> gSubmissions;
uint64 gFrameSubmission = 0;
uint64 gFirstAcceptedSubmission = 0;
uint64 gLastAcceptedSubmission = 0;
usize gDrawCount = 0;
RasterPassInfo gRasterPass;
bool gSurfaceColorDefined = true;
bool gSurfaceDepthDefined = false;
bool gGraphExecuting = false;
void ResetGraphRecords() noexcept;
RasterStatus ValidatePacketPass(const RasterPacket&, const RasterPassInfo&, bool) noexcept;
RasterStatus ComputeAdmission(DeviceHandle, bool = true) noexcept;
bool StorageRole(BufferRole role) noexcept
{
    return role == BufferRole::Storage || role == BufferRole::StorageVertex || role == BufferRole::StorageIndirect;
}
bool BufferBinding(RasterBindingKind kind) noexcept
{
    return kind == RasterBindingKind::UniformBuffer || kind == RasterBindingKind::StorageRead ||
           kind == RasterBindingKind::StorageReadWrite;
}
void RetainPacketTextures(const RasterPacket&) noexcept;

Record& At(RasterKind kind, usize slot) noexcept
{
    return gRecords[static_cast<usize>(kind)][slot];
}
RasterStatus Admission(DeviceHandle device, bool mutation = true) noexcept
{
    DeviceInfo info;
    switch (GetDeviceInfo(device, info))
    {
        case DeviceStatus::InvalidHandle:
            return RasterStatus::InvalidHandle;
        case DeviceStatus::Ready:
            break;
        case DeviceStatus::Pending:
            return RasterStatus::NotReady;
        case DeviceStatus::DeviceLost:
            return RasterStatus::DeviceLost;
        default:
            return RasterStatus::Failed;
    }
    if (!info.Enabled.PortableRaster)
    {
        return RasterStatus::Unsupported;
    }
    return mutation && FrameOpen() ? RasterStatus::InvalidState : RasterStatus::Ready;
}
template <typename T>
Record* Resolve(T handle, RasterKind kind, bool published = true) noexcept
{
    const auto slot = RasterAccess::Slot(handle);
    if (slot >= gRecords[static_cast<usize>(kind)].GetSize() || RasterAccess::Owner(handle) == 0)
    {
        return nullptr;
    }
    auto& record = At(kind, slot);
    return record.Owner == DeviceOwner() && record.Owner == RasterAccess::Owner(handle) &&
                   record.Generation == RasterAccess::Generation(handle) && (!published || record.Published)
               ? &record
               : nullptr;
}
void Retain(RasterKind kind, usize slot) noexcept
{
    ++At(kind, slot).References;
}
void Release(RasterKind kind, usize slot) noexcept
{
    --At(kind, slot).References;
}
void Dependencies(Record& record, RasterKind kind, bool retain) noexcept
{
    const auto reference = [retain](RasterKind dependency, usize slot) noexcept {
        if (retain)
        {
            Retain(dependency, slot);
        }
        else
        {
            Release(dependency, slot);
        }
    };
    if (kind == RasterKind::View)
    {
        reference(RasterKind::Texture, record.TextureSlot);
    }
    else if (kind == RasterKind::Pipeline)
    {
        reference(RasterKind::Shader, record.Pipeline.Vertex);
        reference(RasterKind::Shader, record.Pipeline.Fragment);
        reference(RasterKind::Layout, record.Pipeline.Layout);
    }
    else if (kind == RasterKind::ComputePipeline)
    {
        reference(RasterKind::Shader, record.Compute.Shader);
        reference(RasterKind::Layout, record.Compute.Layout);
    }
    else if (kind == RasterKind::RayScene)
    {
        for (auto slot : record.Ray->GeometrySlots)
        {
            reference(RasterKind::RayGeometry, slot);
        }
    }
    else if (kind == RasterKind::RayBatch)
    {
        reference(RasterKind::RayProgram, record.Ray->Program);
        reference(RasterKind::RayScene, record.Ray->Scene);
    }
    else if (kind == RasterKind::Set)
    {
        const auto& layout = At(RasterKind::Layout, record.Set.Layout).Layout;
        for (usize i = 0; i < layout.Count; ++i)
        {
            const auto type = layout.Entries[i].Kind;
            reference(BufferBinding(type)                    ? RasterKind::Buffer
                      : type == RasterBindingKind::Texture2D ? RasterKind::View
                                                             : RasterKind::Sampler,
                      record.Set.Slots[i]);
        }
        reference(RasterKind::Layout, record.Set.Layout);
    }
}
void Collect() noexcept
{
    const auto completed = backend::RasterCompleted();
    for (usize kind = static_cast<usize>(RasterKind::RayGeometry); kind < KINDS; ++kind)
    {
        auto& pool = gRecords[kind];
        for (usize slot = 0; slot < pool.GetSize(); ++slot)
        {
            auto& record = gRecords[kind][slot];
            if (record.Owner != 0 && record.Status == RasterStatus::Pending)
            {
                record.Status = backend::RayPoll(static_cast<RasterKind>(kind), slot);
            }
        }
    }
    // Acyclic dependencies can expose another detached record in each pass.
    for (usize pass = 0; pass < KINDS; ++pass)
    {
        for (usize kind = KINDS; kind-- > 0;)
        {
            for (usize slot = 0; slot < gRecords[kind].GetSize(); ++slot)
            {
                auto& record = gRecords[kind][slot];
                if (record.Owner == 0 || record.References != 0 || record.Status == RasterStatus::Pending ||
                    record.Callbacks != 0 || record.LastUse > completed)
                {
                    continue;
                }
                backend::RasterDestroy(static_cast<RasterKind>(kind), slot);
                ReleaseResourceStorage(record.StorageBytes);
                Dependencies(record, static_cast<RasterKind>(kind), false);
                const auto generation = RasterNextGeneration(record.Generation);
                ReleaseHostStorage(record.AuxiliaryHostBytes);
                record.CpuTexture.Reset();
                record.Ray.Reset();
                record = {};
                record.Generation = generation;
            }
        }
    }
}
RasterStatus Reserve(RasterKind kind, usize& slot) noexcept
{
    Collect();
    if (GetStartup().State != StartupState::Ready)
    {
        return GetStartup().State == StartupState::DeviceLost ? RasterStatus::DeviceLost : RasterStatus::Failed;
    }
    auto& records = gRecords[static_cast<usize>(kind)];
    bool exhausted = false;
    const auto limit = static_cast<usize>(kind) < RASTER_KINDS ? usize{CurrentDeviceProfile().Raster.ResourcesPerKind}
                                                               : usize{CurrentStorageLimits().RayResourcesPerKind};
    for (slot = 0; slot < limit; ++slot)
    {
        if (slot == records.GetSize())
        {
            const auto remaining = limit - slot;
            const auto size = slot + (remaining < 64 ? remaining : 64);
            if (!records.EnsureSlots(size))
            {
                return StorageFailure();
            }
        }
        auto& record = records[slot];
        if (record.Owner != 0)
        {
            continue;
        }
        if (record.Generation == 0)
        {
            exhausted = true;
            continue;
        }
        if (gRequests.Next == 0)
        {
            return RasterStatus::IdentityExhausted;
        }
        const auto grown =
            static_cast<usize>(kind) < RASTER_KINDS ? backend::RasterGrow(kind, slot + 1) : RasterStatus::Ready;
        if (grown != RasterStatus::Ready)
        {
            return grown;
        }
        record.Owner = DeviceOwner();
        record.References = 1;
        record.Request = gRequests.Take();
        record.Status = RasterStatus::Pending;
        return RasterStatus::Ready;
    }
    return exhausted ? RasterStatus::IdentityExhausted : RasterStatus::CapacityExceeded;
}
template <typename T, typename Create>
RasterStatus Publish(RasterKind kind, T& output, Create create, uint64 storageBytes = 0) noexcept
{
    if (RasterAccess::Owner(output) != 0)
    {
        return RasterStatus::InvalidState;
    }
    usize slot = 0;
    Collect();
    if (GetStartup().State != StartupState::Ready)
    {
        return GetStartup().State == StartupState::DeviceLost ? RasterStatus::DeviceLost : RasterStatus::Failed;
    }
    if (!ReserveResourceStorage(storageBytes))
    {
        return StorageFailure();
    }
    const auto reservation = Reserve(kind, slot);
    if (reservation != RasterStatus::Ready)
    {
        ReleaseResourceStorage(storageBytes);
        return reservation;
    }
    auto& record = At(kind, slot);
    record.StorageBytes = storageBytes;
    const auto result = create(slot, record);
    if (record.Owner == 0)
    {
        return GetStartup().State == StartupState::DeviceLost ? RasterStatus::DeviceLost : RasterStatus::Failed;
    }
    // Spontaneous error-scope callbacks may have updated Status inside create.
    if (result != RasterStatus::Ready && result != RasterStatus::Pending)
    {
        backend::RasterDestroy(kind, slot);
        ReleaseResourceStorage(record.StorageBytes);
        const auto generation = record.Generation;
        ReleaseHostStorage(record.AuxiliaryHostBytes);
        record.CpuTexture.Reset();
        record.Ray.Reset();
        record = {};
        record.Generation = generation;
        return result;
    }
    if (record.Status == RasterStatus::Pending)
    {
        record.Status = result;
    }
    Dependencies(record, kind, true);
    record.Published = true;
    RasterAccess::Set(output, {record.Owner, slot, record.Generation});
    return result;
}
bool ValidBinding(const RasterBinding& binding) noexcept
{
    const auto visibility = static_cast<uint8>(binding.Visibility);
    if (binding.Binding >= RASTER_BINDINGS || visibility < 1 || visibility > 4)
    {
        return false;
    }
    if (binding.Kind == RasterBindingKind::UniformBuffer)
    {
        return binding.MinSize > 0 && binding.MinSize <= 16384;
    }
    if (binding.Kind == RasterBindingKind::StorageRead || binding.Kind == RasterBindingKind::StorageReadWrite)
    {
        const auto limits = backend::ComputeLimits();
        return visibility == 4 && binding.MinSize > 0 && binding.MinSize % 4 == 0 &&
               binding.MinSize <= limits.MaxStorageRange;
    }
    return visibility != 4 &&
           (binding.Kind == RasterBindingKind::Texture2D || binding.Kind == RasterBindingKind::Sampler) &&
           binding.MinSize == 0;
}
bool CopyLayout(std::span<const RasterBinding> entries, RasterLayout& layout) noexcept
{
    if (entries.size() > RASTER_BINDINGS)
    {
        return false;
    }
    usize storageBindings = 0;
    for (const auto& entry : entries)
    {
        if (entry.Kind == RasterBindingKind::StorageRead || entry.Kind == RasterBindingKind::StorageReadWrite)
        {
            ++storageBindings;
        }
    }
    if (storageBindings > 4)
    {
        return false;
    }
    layout.Count = entries.size();
    for (usize i = 0; i < entries.size(); ++i)
    {
        if (!ValidBinding(entries[i]))
        {
            return false;
        }
        for (usize j = 0; j < i; ++j)
        {
            if (entries[i].Binding == entries[j].Binding)
            {
                return false;
            }
        }
        layout.Entries[i] = entries[i];
    }
    return true;
}
bool CopyName(std::string_view name, char (&output)[64]) noexcept
{
    if (name.size() >= 64)
    {
        return false;
    }
    if (name.empty())
    {
        return true;
    }
    for (const char c : name)
    {
        if ((c < 'a' || c > 'z') && (c < 'A' || c > 'Z') && (c < '0' || c > '9') && c != '_')
        {
            return false;
        }
    }
    std::memcpy(output, name.data(), name.size());
    return true;
}
bool Fits(usize offset, usize size, usize total) noexcept
{
    return offset <= total && size <= total - offset;
}
void FrameRetain(RasterKind kind, usize slot) noexcept
{
    auto& record = At(kind, slot);
    if (!record.InFrame)
    {
        record.InFrame = true;
        record.FrameTextureUse = record.TextureUse;
        record.FrameColorDefined = record.ColorDefined;
        record.FrameDepthDefined = record.DepthDefined;
        ++record.References;
    }
}
bool ShaderFits(const RasterShaderInfo& shader, const RasterLayout& layout) noexcept
{
    for (usize i = 0; i < shader.Layout.Count; ++i)
    {
        const auto& need = shader.Layout.Entries[i];
        bool found = false;
        const auto stage = shader.Stage == ShaderStage::Vertex ? 1U : shader.Stage == ShaderStage::Fragment ? 2U : 4U;
        for (usize j = 0; j < layout.Count; ++j)
        {
            const auto& entry = layout.Entries[j];
            if (entry.Binding == need.Binding && entry.Kind == need.Kind && entry.MinSize >= need.MinSize &&
                (static_cast<uint8>(entry.Visibility) & stage) != 0)
            {
                found = true;
            }
        }
        if (!found)
        {
            return false;
        }
    }
    return true;
}
} // namespace
RasterStatus GetRasterCapabilities(DeviceHandle device, RasterCapabilities& output) noexcept
{
    const auto admission = Admission(device, false);
    if (admission == RasterStatus::InvalidHandle)
    {
        return admission;
    }
    output = admission == RasterStatus::Ready ? internal::CurrentDeviceProfile().Raster : RasterCapabilities{};
    return admission;
}
RasterStatus CreateBuffer(DeviceHandle device,
                          const BufferDescription& description,
                          std::span<const uint8> bytes,
                          BufferHandle& output) noexcept
{
    const auto admission = Admission(device);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    const auto limits = backend::RasterLimits();
    if (description.Size == 0 || bytes.size() != description.Size || description.Size > limits.MaxBufferSize ||
        static_cast<uint8>(description.Role) > static_cast<uint8>(BufferRole::StorageIndirect))
    {
        return RasterStatus::InvalidDescription;
    }
    if (StorageRole(description.Role))
    {
        const auto compute = ComputeAdmission(device);
        if (compute != RasterStatus::Ready)
        {
            return compute;
        }
        if (description.Size > backend::ComputeLimits().MaxStorageRange)
        {
            return RasterStatus::InvalidDescription;
        }
    }
    const usize alignment = description.Role == BufferRole::Index16   ? 2
                            : description.Role == BufferRole::Uniform ? 16
                                                                      : 4;
    if (description.Size % alignment != 0)
    {
        return RasterStatus::InvalidDescription;
    }
    return Publish(
        RasterKind::Buffer,
        output,
        [&](usize slot, Record& record) noexcept {
            record.Buffer = description;
            if (description.Role == BufferRole::Index16 || description.Role == BufferRole::Index32)
            {
                if (!record.Indices.TryResize(bytes.size()))
                {
                    return StorageFailure();
                }
                std::memcpy(record.Indices.GetData(), bytes.data(), bytes.size());
            }
            return backend::RasterCreateBuffer(slot, description, bytes, record.Request);
        },
        description.Size);
}
RasterStatus GetFormatCapabilities(DeviceHandle device, RasterFormat format, FormatCapabilities& output) noexcept
{
    if (static_cast<uint8>(format) > static_cast<uint8>(RasterFormat::Rgba16Float))
    {
        return RasterStatus::InvalidDescription;
    }
    const auto admission = Admission(device, false);
    if (admission != RasterStatus::Ready)
    {
        if (admission != RasterStatus::InvalidHandle)
        {
            output = {};
        }
        return admission;
    }
    output = internal::CurrentDeviceProfile().Formats[static_cast<usize>(format)];
    return output.Uses.Sampled ? RasterStatus::Ready : RasterStatus::Unsupported;
}
RasterStatus CreateTexture(DeviceHandle device,
                           const TextureDescription& description,
                           const TextureUpload& upload,
                           TextureHandle& output) noexcept
{
    const auto admission = Admission(device);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    const auto textureLimits = backend::RasterLimits();
    const auto maxDimension = textureLimits.MaxTextureDimension2D;
    if (description.Width == 0 || description.Height == 0 || description.Width > maxDimension ||
        description.Height > maxDimension ||
        (description.Format != RasterFormat::Rgba8Unorm && description.Format != RasterFormat::Rgba8Srgb &&
         description.Format != RasterFormat::R8Unorm && description.Format != RasterFormat::Rg8Unorm &&
         description.Format != RasterFormat::Rgba16Float) ||
        (description.Attachment && (description.Format == RasterFormat::R8Unorm ||
                                    description.Format == RasterFormat::Rg8Unorm || description.MipLevels != 1)))
    {
        return RasterStatus::InvalidDescription;
    }
    if (description.RetainCpuContent && (description.Attachment || description.Format == RasterFormat::Rgba16Float))
    {
        return RasterStatus::InvalidDescription;
    }
    if (description.Format == RasterFormat::Rgba16Float && !description.Attachment)
    {
        return RasterStatus::Unsupported;
    }
    const auto format = internal::CurrentDeviceProfile().Formats[static_cast<usize>(description.Format)];
    const auto dimension = description.Attachment ? format.MaxAttachmentDimension2D : format.MaxSampledDimension2D;
    if (!format.Uses.Sampled || !format.Uses.Filterable || (description.Attachment && !format.Uses.ColorAttachment) ||
        description.Width > dimension || description.Height > dimension || description.MipLevels > format.MaxMipLevels)
    {
        return RasterStatus::Unsupported;
    }
    uint32 levels = 1;
    for (uint32 extent = description.Width > description.Height ? description.Width : description.Height; extent > 1;
         extent /= 2)
    {
        ++levels;
    }
    if (description.MipLevels == 0 || description.MipLevels > levels || description.MipLevels > 16)
    {
        return RasterStatus::InvalidDescription;
    }
    uint64 storageBytes = 0;
    uint32 storageWidth = description.Width, storageHeight = description.Height;
    const uint64 storageChannels = description.Format == RasterFormat::R8Unorm       ? 1U
                                   : description.Format == RasterFormat::Rg8Unorm    ? 2U
                                   : description.Format == RasterFormat::Rgba16Float ? 8U
                                                                                     : 4U;
    for (uint32 mip = 0; mip < description.MipLevels; ++mip)
    {
        uint64 pixels = 0, bytes = 0;
        if (!foundation::TryMultiply(uint64{storageWidth}, uint64{storageHeight}, pixels) ||
            !foundation::TryMultiply(pixels, storageChannels, bytes) ||
            !foundation::TryAdd(storageBytes, bytes, storageBytes))
        {
            return RasterStatus::InvalidDescription;
        }
        if (description.Attachment)
        {
            if (!foundation::TryMultiply(pixels, uint64{4}, bytes) ||
                !foundation::TryAdd(storageBytes, bytes, storageBytes))
            {
                return RasterStatus::InvalidDescription;
            }
        }
        storageWidth = storageWidth > 1 ? storageWidth / 2 : 1;
        storageHeight = storageHeight > 1 ? storageHeight / 2 : 1;
    }
    if (description.Attachment)
    {
        if (!upload.Bytes.empty() || upload.RowPitch != 0 || !upload.Mips.empty())
        {
            return RasterStatus::InvalidDescription;
        }
        return Publish(
            RasterKind::Texture,
            output,
            [&](usize slot, Record& record) noexcept {
                record.Texture = description;
                return backend::RasterCreateTexture(slot, description, upload, record.Request);
            },
            storageBytes);
    }
    if ((upload.Mips.empty() && description.MipLevels != 1) ||
        (!upload.Mips.empty() && (upload.Mips.size() != description.MipLevels || upload.RowPitch != 0)))
    {
        return RasterStatus::InvalidDescription;
    }
    usize occupied = 0;
    uint32 width = description.Width, height = description.Height;
    const usize channels = description.Format == RasterFormat::R8Unorm    ? 1U
                           : description.Format == RasterFormat::Rg8Unorm ? 2U
                                                                          : 4U;
    for (usize level = 0; level < description.MipLevels; ++level)
    {
        const auto pitch = upload.Mips.empty() ? upload.RowPitch : upload.Mips[level].RowPitch;
        const auto offset = upload.Mips.empty() ? usize{0} : upload.Mips[level].Offset;
        if (pitch < static_cast<usize>(width) * channels || pitch % 4 != 0 || offset != occupied ||
            occupied > upload.Bytes.size() || pitch > (upload.Bytes.size() - occupied) / height)
        {
            return RasterStatus::InvalidDescription;
        }
        occupied += pitch * height;
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
    }
    if (upload.Bytes.data() == nullptr || occupied != upload.Bytes.size() || occupied > textureLimits.MaxBufferSize)
    {
        return RasterStatus::InvalidDescription;
    }
    return Publish(
        RasterKind::Texture,
        output,
        [&](usize slot, Record& record) noexcept {
            record.Texture = description;
            if (description.RetainCpuContent)
            {
                if (!ReserveHostStorage(sizeof(CpuTextureRecord)))
                {
                    return StorageFailure();
                }
                record.AuxiliaryHostBytes = sizeof(CpuTextureRecord);
                auto* copy = new (std::nothrow) CpuTextureRecord;
                if (copy == nullptr)
                {
                    return RasterStatus::OutOfMemory;
                }
                record.CpuTexture.Reset(copy);
                if (!copy->Bytes.TryResize(upload.Bytes.size()))
                {
                    return StorageFailure();
                }
                std::memcpy(copy->Bytes.GetData(), upload.Bytes.data(), upload.Bytes.size());
                for (usize level = 0; level < description.MipLevels; ++level)
                {
                    copy->Mips[level] = upload.Mips.empty() ? TextureMipUpload{0, upload.RowPitch} : upload.Mips[level];
                }
            }
            record.ColorDefined = true;
            record.TextureUse = RasterTextureUse::SampledBoth;
            return backend::RasterCreateTexture(slot, description, upload, record.Request);
        },
        storageBytes);
}
bool IsNull(TextureHandle handle) noexcept
{
    return RasterAccess::Owner(handle) == 0;
}
RasterStatus GetTextureDescription(DeviceHandle device, TextureHandle handle, TextureDescription& output) noexcept
{
    const auto admission = Admission(device, false);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    const auto* record = Resolve(handle, RasterKind::Texture);
    if (record == nullptr)
    {
        return RasterStatus::InvalidHandle;
    }
    if (record->Status != RasterStatus::Ready)
    {
        return record->Status == RasterStatus::Pending ? RasterStatus::NotReady : record->Status;
    }
    output = record->Texture;
    return RasterStatus::Ready;
}
RasterStatus CreateTextureView(DeviceHandle device, TextureHandle texture, TextureViewHandle& output) noexcept
{
    const auto admission = Admission(device);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    const auto* source = Resolve(texture, RasterKind::Texture);
    if (source == nullptr)
    {
        return RasterStatus::InvalidHandle;
    }
    if (source->Status != RasterStatus::Ready)
    {
        return source->Status == RasterStatus::Pending ? RasterStatus::NotReady : source->Status;
    }
    return Publish(RasterKind::View, output, [&](usize slot, Record& record) noexcept {
        record.TextureSlot = RasterAccess::Slot(texture);
        return backend::RasterCreateView(slot, {record.TextureSlot}, record.Request);
    });
}
RasterStatus CreateSampler(DeviceHandle device, const SamplerDescription& description, SamplerHandle& output) noexcept
{
    const auto admission = Admission(device);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    // Positive range predicates reject NaN as well as infinities without a math dependency.
    const bool validLod = description.MinLod >= 0 && description.MinLod <= 15 &&
                          description.MaxLod >= description.MinLod && description.MaxLod <= 15;
    if (static_cast<uint8>(description.Filter) > 1 || static_cast<uint8>(description.U) > 1 ||
        static_cast<uint8>(description.V) > 1 || static_cast<uint8>(description.MipFilter) > 2 || !validLod ||
        (description.MipFilter == RasterMipFilter::None && description.MinLod != 0))
    {
        return RasterStatus::InvalidDescription;
    }
    return Publish(RasterKind::Sampler, output, [&](usize slot, Record& record) noexcept {
        return backend::RasterCreateSampler(slot, description, record.Request);
    });
}
RasterStatus
CreateRasterShader(DeviceHandle device, const RasterShaderDescription& description, RasterShaderHandle& output) noexcept
{
    const auto admission = Admission(device);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    RasterShaderInfo info;
    info.Stage = description.Artifact.Stage;
    if ((info.Stage != ShaderStage::Vertex && info.Stage != ShaderStage::Fragment &&
         info.Stage != ShaderStage::Compute) ||
        !CopyLayout(description.Bindings, info.Layout))
    {
        return RasterStatus::InvalidDescription;
    }
    if (info.Stage == ShaderStage::Compute)
    {
        const auto compute = ComputeAdmission(device);
        if (compute != RasterStatus::Ready)
        {
            return compute;
        }
        const auto limits = backend::ComputeLimits();
        uint32 product = 1;
        for (usize i = 0; i < 3; ++i)
        {
            const auto size = description.WorkgroupSize[i];
            if (size == 0 || size > limits.MaxWorkgroupSize[i] || size > limits.MaxWorkgroupInvocations / product)
            {
                return RasterStatus::InvalidDescription;
            }
            product *= size;
            info.WorkgroupSize[i] = size;
        }
    }
    for (const auto& binding : description.Bindings)
    {
        if (static_cast<uint8>(binding.Visibility) != (info.Stage == ShaderStage::Compute  ? 4
                                                       : info.Stage == ShaderStage::Vertex ? 1
                                                                                           : 2) &&
            (info.Stage == ShaderStage::Compute || binding.Visibility != RasterVisibility::Both))
        {
            return RasterStatus::InvalidDescription;
        }
    }
    if (description.UniformMembers.size() > 64)
    {
        return RasterStatus::InvalidDescription;
    }
    for (usize i = 0; i < description.UniformMembers.size(); ++i)
    {
        const auto& member = description.UniformMembers[i];
        char name[64]{};
        bool valid = false;
        for (const auto& binding : description.Bindings)
        {
            valid |= binding.Binding == member.Binding && binding.Kind == RasterBindingKind::UniformBuffer &&
                     member.Offset < binding.MinSize;
        }
        if (!valid || member.Offset % 4 != 0 || member.Name.empty() || !CopyName(member.Name, name) ||
            static_cast<uint8>(member.Shape) > static_cast<uint8>(RasterUniformShape::Float4x4Array))
        {
            return RasterStatus::InvalidDescription;
        }
        for (usize j = 0; j < i; ++j)
        {
            const auto& previous = description.UniformMembers[j];
            if (previous.Binding == member.Binding &&
                (previous.Name == member.Name || previous.Offset == member.Offset))
            {
                return RasterStatus::InvalidDescription;
            }
        }
    }
    for (usize i = 0; i < RASTER_BINDINGS; ++i)
    {
        if (!CopyName(description.UniformBlocks[i], info.UniformBlocks[i]) ||
            !CopyName(description.TextureNames[i], info.TextureNames[i]) ||
            description.TextureSamplers[i] >= RASTER_BINDINGS)
        {
            return RasterStatus::InvalidDescription;
        }
        info.TextureSamplers[i] = description.TextureSamplers[i];
    }
    if (description.Inputs.size() > backend::RasterLimits().MaxVertexAttributes ||
        (info.Stage != ShaderStage::Vertex && !description.Inputs.empty()))
    {
        return RasterStatus::InvalidDescription;
    }
    info.InputCount = description.Inputs.size();
    for (usize i = 0; i < info.InputCount; ++i)
    {
        const auto& input = description.Inputs[i];
        if (input.Location >= backend::RasterLimits().MaxVertexAttributes || static_cast<uint8>(input.Format) > 2)
        {
            return RasterStatus::InvalidDescription;
        }
        for (usize j = 0; j < i; ++j)
        {
            if (info.Inputs[j].Location == input.Location)
            {
                return RasterStatus::InvalidDescription;
            }
        }
        info.Inputs[i] = input;
    }
    const auto& artifact = description.Artifact;
    const auto backendKind = GetStartup().SelectedBackend;
    if (backendKind == Backend::WebGL2)
    {
        for (usize i = 0; i < info.Layout.Count; ++i)
        {
            const auto& binding = info.Layout.Entries[i];
            if (binding.Kind != RasterBindingKind::Texture2D)
            {
                continue;
            }
            bool samplerFound = false;
            for (usize j = 0; j < info.Layout.Count; ++j)
            {
                const auto& candidate = info.Layout.Entries[j];
                samplerFound |= candidate.Kind == RasterBindingKind::Sampler &&
                                candidate.Binding == info.TextureSamplers[binding.Binding];
            }
            if (!samplerFound)
            {
                return RasterStatus::InvalidDescription;
            }
        }
    }
    const auto entry = backendKind == Backend::Metal    ? artifact.MslEntry
                       : backendKind == Backend::WebGPU ? artifact.WgslEntry
                       : backendKind == Backend::WebGL2 ? artifact.GlslEsEntry
                                                        : artifact.SpirvEntry;
    char name[64]{};
    if (entry.empty() || !CopyName(entry, name) ||
        (backendKind == Backend::Metal    ? artifact.Msl.empty()
         : backendKind == Backend::WebGPU ? artifact.Wgsl.empty()
         : backendKind == Backend::WebGL2 ? artifact.GlslEs.empty()
                                          : artifact.Spirv.size() < 5 || artifact.Spirv[0] != 0x07230203U))
    {
        return RasterStatus::InvalidDescription;
    }
    return Publish(RasterKind::Shader, output, [&](usize slot, Record& record) noexcept {
        record.Shader = info;
        return backend::RasterCreateShader(slot, artifact, info, record.Request);
    });
}
RasterStatus
CreateBindingLayout(DeviceHandle device, std::span<const RasterBinding> entries, BindingLayoutHandle& output) noexcept
{
    const auto admission = Admission(device);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    RasterLayout layout;
    if (!CopyLayout(entries, layout))
    {
        return RasterStatus::InvalidDescription;
    }
    return Publish(RasterKind::Layout, output, [&](usize slot, Record& record) noexcept {
        record.Layout = layout;
        return backend::RasterCreateLayout(slot, layout, record.Request);
    });
}
RasterStatus CreateBindingSet(DeviceHandle device,
                              BindingLayoutHandle handle,
                              std::span<const RasterBindingResource> entries,
                              BindingSetHandle& output) noexcept
{
    const auto admission = Admission(device);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    const auto* layoutRecord = Resolve(handle, RasterKind::Layout);
    if (layoutRecord == nullptr)
    {
        return RasterStatus::InvalidHandle;
    }
    if (layoutRecord->Status != RasterStatus::Ready)
    {
        return layoutRecord->Status == RasterStatus::Pending ? RasterStatus::NotReady : layoutRecord->Status;
    }
    const auto& layout = layoutRecord->Layout;
    if (entries.size() != layout.Count)
    {
        return RasterStatus::InvalidDescription;
    }
    RasterSet set;
    set.Layout = RasterAccess::Slot(handle);
    const auto limits = backend::RasterLimits();
    for (usize i = 0; i < layout.Count; ++i)
    {
        const RasterBindingResource* entry = nullptr;
        for (const auto& candidate : entries)
        {
            if (candidate.Binding != layout.Entries[i].Binding)
            {
                continue;
            }
            if (entry != nullptr)
            {
                return RasterStatus::InvalidDescription;
            }
            entry = &candidate;
        }
        if (entry == nullptr)
        {
            return RasterStatus::InvalidDescription;
        }
        const auto kind = layout.Entries[i].Kind;
        const auto* resource = BufferBinding(kind)                    ? Resolve(entry->Buffer, RasterKind::Buffer)
                               : kind == RasterBindingKind::Texture2D ? Resolve(entry->Texture, RasterKind::View)
                                                                      : Resolve(entry->Sampler, RasterKind::Sampler);
        if (resource == nullptr)
        {
            return RasterStatus::InvalidHandle;
        }
        if (resource->Status != RasterStatus::Ready)
        {
            return resource->Status == RasterStatus::Pending ? RasterStatus::NotReady : resource->Status;
        }
        if (kind == RasterBindingKind::UniformBuffer &&
            (resource->Buffer.Role != BufferRole::Uniform || limits.UniformOffsetAlignment == 0 ||
             entry->Offset % limits.UniformOffsetAlignment != 0 || entry->Size % 16 != 0 ||
             entry->Size < layout.Entries[i].MinSize || entry->Size > limits.MaxUniformRange ||
             !Fits(entry->Offset, entry->Size, resource->Buffer.Size)))
        {
            return RasterStatus::InvalidDescription;
        }
        if (kind == RasterBindingKind::StorageRead || kind == RasterBindingKind::StorageReadWrite)
        {
            const auto compute = backend::ComputeLimits();
            if (!StorageRole(resource->Buffer.Role) || compute.StorageOffsetAlignment == 0 ||
                entry->Offset % compute.StorageOffsetAlignment != 0 || entry->Size % 4 != 0 ||
                entry->Size < layout.Entries[i].MinSize || entry->Size > compute.MaxStorageRange ||
                !Fits(entry->Offset, entry->Size, resource->Buffer.Size))
            {
                return RasterStatus::InvalidDescription;
            }
        }
        set.Slots[i] = BufferBinding(kind)                    ? RasterAccess::Slot(entry->Buffer)
                       : kind == RasterBindingKind::Texture2D ? RasterAccess::Slot(entry->Texture)
                                                              : RasterAccess::Slot(entry->Sampler);
        set.Offsets[i] = entry->Offset;
        set.Sizes[i] = entry->Size;
    }
    return Publish(RasterKind::Set, output, [&](usize slot, Record& record) noexcept {
        record.Set = set;
        return backend::RasterCreateSet(slot, layout, set, record.Request);
    });
}
namespace
{
RasterStatus
ValidatePipeline(DeviceHandle device, const RasterPipelineDescription& description, RasterPipelineInfo& info) noexcept
{
    const auto admission = Admission(device);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    const auto* vertex = Resolve(description.Vertex, RasterKind::Shader);
    const auto* fragment = Resolve(description.Fragment, RasterKind::Shader);
    const auto* layout = Resolve(description.Layout, RasterKind::Layout);
    if (vertex == nullptr || fragment == nullptr || layout == nullptr)
    {
        return RasterStatus::InvalidHandle;
    }
    const Record* dependencies[]{vertex, fragment, layout};
    for (const auto* dependency : dependencies)
    {
        if (dependency->Status != RasterStatus::Ready)
        {
            return dependency->Status == RasterStatus::Pending ? RasterStatus::NotReady : dependency->Status;
        }
    }
    if (vertex->Shader.Stage != ShaderStage::Vertex || fragment->Shader.Stage != ShaderStage::Fragment ||
        !ShaderFits(vertex->Shader, layout->Layout) || !ShaderFits(fragment->Shader, layout->Layout) ||
        description.Streams.size() > 2 || description.Attributes.size() > backend::RasterLimits().MaxVertexAttributes ||
        description.Attributes.size() != vertex->Shader.InputCount)
    {
        return RasterStatus::InvalidDescription;
    }
    for (usize i = 0; i < layout->Layout.Count; ++i)
    {
        if (layout->Layout.Entries[i].Visibility == RasterVisibility::Compute)
        {
            return RasterStatus::InvalidDescription;
        }
    }
    info.Vertex = RasterAccess::Slot(description.Vertex);
    info.Fragment = RasterAccess::Slot(description.Fragment);
    info.Layout = RasterAccess::Slot(description.Layout);
    info.Depth = description.Depth;
    if (static_cast<uint8>(description.Target) > static_cast<uint8>(RasterTarget::Rgba16Float))
    {
        return RasterStatus::InvalidDescription;
    }
    if (description.Target != RasterTarget::Surface)
    {
        const auto format = description.Target == RasterTarget::Rgba16Float  ? RasterFormat::Rgba16Float
                            : description.Target == RasterTarget::Rgba8Unorm ? RasterFormat::Rgba8Unorm
                                                                             : RasterFormat::Rgba8Srgb;
        const auto capabilities = internal::CurrentDeviceProfile().Formats[static_cast<usize>(format)];
        if (!capabilities.Uses.ColorAttachment || (description.Blend && !capabilities.Uses.Blendable))
        {
            return RasterStatus::Unsupported;
        }
    }
    info.Blend = description.Blend;
    info.Target = description.Target;
    if (static_cast<uint8>(description.DepthCompare) > static_cast<uint8>(RasterDepthCompare::Always))
    {
        return RasterStatus::InvalidDescription;
    }
    info.DepthWrite = description.DepthWrite;
    info.DepthCompare = description.DepthCompare;
    info.StreamCount = description.Streams.size();
    info.AttributeCount = description.Attributes.size();
    for (usize i = 0; i < info.StreamCount; ++i)
    {
        const auto& stream = description.Streams[i];
        if (stream.Stride == 0 || stream.Stride > internal::CurrentDeviceProfile().Interface.MaxVertexStride ||
            stream.Stride % 4 != 0)
        {
            return RasterStatus::InvalidDescription;
        }
        info.Streams[i] = stream;
    }
    for (usize i = 0; i < info.AttributeCount; ++i)
    {
        const auto& attribute = description.Attributes[i];
        const auto format = static_cast<uint8>(attribute.Format);
        const usize size = (static_cast<usize>(format) + 2) * 4;
        if (format > 2 || attribute.Location >= backend::RasterLimits().MaxVertexAttributes ||
            attribute.Stream >= info.StreamCount || attribute.Offset % 4 != 0 ||
            !Fits(attribute.Offset, size, info.Streams[attribute.Stream].Stride))
        {
            return RasterStatus::InvalidDescription;
        }
        for (usize j = 0; j < i; ++j)
        {
            if (attribute.Location == info.Attributes[j].Location)
            {
                return RasterStatus::InvalidDescription;
            }
        }
        bool matched = false;
        for (usize j = 0; j < vertex->Shader.InputCount; ++j)
        {
            if (vertex->Shader.Inputs[j].Location == attribute.Location &&
                vertex->Shader.Inputs[j].Format == attribute.Format)
            {
                matched = true;
            }
        }
        if (!matched)
        {
            return RasterStatus::InvalidDescription;
        }
        info.Attributes[i] = attribute;
    }
    return RasterStatus::Ready;
}
} // namespace
RasterStatus CreateRasterPipeline(DeviceHandle device,
                                  const RasterPipelineDescription& description,
                                  RasterPipelineHandle& output) noexcept
{
    RasterPipelineInfo info;
    const auto valid = ValidatePipeline(device, description, info);
    if (valid != RasterStatus::Ready)
    {
        return valid;
    }
    return Publish(RasterKind::Pipeline, output, [&](usize slot, Record& record) noexcept {
        record.Pipeline = info;
        return backend::RasterCreatePipeline(slot, info, record.Request);
    });
}
namespace
{
RasterStatus ValidateDraw(DeviceHandle device, const RasterDraw& draw, RasterPacket& packet) noexcept
{
    const auto admission = Admission(device, false);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    const auto* pipeline = Resolve(draw.Pipeline, RasterKind::Pipeline);
    const auto* set = Resolve(draw.Bindings, RasterKind::Set);
    const auto* indices = Resolve(draw.Indices, RasterKind::Buffer);
    if (pipeline == nullptr || set == nullptr || indices == nullptr)
    {
        return RasterStatus::InvalidHandle;
    }
    const Record* dependencies[]{pipeline, set, indices};
    for (const auto* dependency : dependencies)
    {
        if (dependency->Status != RasterStatus::Ready)
        {
            return dependency->Status == RasterStatus::Pending ? RasterStatus::NotReady : dependency->Status;
        }
    }
    const auto& info = pipeline->Pipeline;
    if (set->Set.Layout != info.Layout || draw.Vertices.size() != info.StreamCount || draw.IndexCount == 0 ||
        draw.IndexCount % 3 != 0 || draw.VertexCount == 0 || draw.InstanceCount == 0 ||
        draw.InstanceCount > 0x7fffffffU ||
        (indices->Buffer.Role != BufferRole::Index16 && indices->Buffer.Role != BufferRole::Index32))
    {
        return RasterStatus::InvalidDescription;
    }
    const usize width = indices->Buffer.Role == BufferRole::Index16 ? 2 : 4;
    if (draw.IndexOffset % width != 0 || draw.IndexCount > ~usize{0} / width ||
        !Fits(draw.IndexOffset, static_cast<usize>(draw.IndexCount) * width, indices->Buffer.Size))
    {
        return RasterStatus::InvalidDescription;
    }
    for (usize i = 0; i < draw.IndexCount; ++i)
    {
        // Byte copies avoid alignment assumptions; index upload uses native-endian
        // unsigned integers, as do the selected native/browser graphics APIs.
        uint32 index = 0;
        if (width == 2)
        {
            uint16 value = 0;
            std::memcpy(&value, indices->Indices.GetData() + draw.IndexOffset + i * width, width);
            index = value;
        }
        else
        {
            std::memcpy(&index, indices->Indices.GetData() + draw.IndexOffset + i * width, width);
        }
        if (index >= draw.VertexCount || index == (width == 2 ? 0xffffU : 0xffffffffU))
        {
            return RasterStatus::InvalidDescription;
        }
    }
    if (RasterAccess::Owner(draw.Indirect) != 0)
    {
        const auto compute = ComputeAdmission(device, false);
        if (compute != RasterStatus::Ready)
        {
            return compute;
        }
        const auto* indirect = Resolve(draw.Indirect, RasterKind::Buffer);
        if (indirect == nullptr)
        {
            return RasterStatus::InvalidHandle;
        }
        if (indirect->Status != RasterStatus::Ready)
        {
            return indirect->Status == RasterStatus::Pending ? RasterStatus::NotReady : indirect->Status;
        }
        if (indirect->Buffer.Role != BufferRole::StorageIndirect || draw.IndirectOffset % 4 != 0 ||
            !Fits(draw.IndirectOffset, sizeof(IndexedIndirectArguments), indirect->Buffer.Size))
        {
            return RasterStatus::InvalidDescription;
        }
        packet.Indirect = RasterAccess::Slot(draw.Indirect);
        packet.IndirectOffset = draw.IndirectOffset;
    }
    else if (draw.IndirectOffset != 0)
    {
        return RasterStatus::InvalidDescription;
    }
    packet.Pipeline = RasterAccess::Slot(draw.Pipeline);
    packet.Set = RasterAccess::Slot(draw.Bindings);
    packet.Indices = RasterAccess::Slot(draw.Indices);
    packet.IndexOffset = draw.IndexOffset;
    packet.IndexCount = draw.IndexCount;
    packet.InstanceCount = draw.InstanceCount;
    packet.Index32 = width == 4;
    for (usize i = 0; i < info.StreamCount; ++i)
    {
        const auto& slice = draw.Vertices[i];
        const auto* buffer = Resolve(slice.Buffer, RasterKind::Buffer);
        if (buffer == nullptr)
        {
            return RasterStatus::InvalidHandle;
        }
        if (buffer->Status != RasterStatus::Ready)
        {
            return buffer->Status == RasterStatus::Pending ? RasterStatus::NotReady : buffer->Status;
        }
        const usize count = info.Streams[i].PerInstance ? draw.InstanceCount : draw.VertexCount;
        if ((buffer->Buffer.Role != BufferRole::Vertex && buffer->Buffer.Role != BufferRole::StorageVertex) ||
            slice.Offset % 4 != 0 || !Fits(slice.Offset, slice.Size, buffer->Buffer.Size) ||
            count > slice.Size / info.Streams[i].Stride)
        {
            return RasterStatus::InvalidDescription;
        }
        packet.Vertices[i] = RasterAccess::Slot(slice.Buffer);
        packet.Offsets[i] = slice.Offset;
        packet.Sizes[i] = slice.Size;
    }
    return RasterStatus::Ready;
}
RasterStatus ReserveFrameSubmission() noexcept
{
    if (gFrameSubmission != 0)
    {
        return RasterStatus::Ready;
    }
    if (gSubmissions.Next == 0)
    {
        return RasterStatus::IdentityExhausted;
    }
    const auto result = backend::RasterReserveSubmission();
    if (result == RasterStatus::Ready)
    {
        gFrameSubmission = gSubmissions.Next;
    }
    return result;
}
RasterStatus EncodePacket(const RasterPacket& packet) noexcept
{
    if (gDrawCount >= CurrentDeviceProfile().Raster.DrawsPerFrame)
    {
        return RasterStatus::CapacityExceeded;
    }
    const auto reservation = ReserveFrameSubmission();
    if (reservation != RasterStatus::Ready)
    {
        return reservation;
    }
    if (!ClaimRasterFrame())
    {
        return RasterStatus::InvalidState;
    }
    FrameRetain(RasterKind::Pipeline, packet.Pipeline);
    FrameRetain(RasterKind::Set, packet.Set);
    FrameRetain(RasterKind::Buffer, packet.Indices);
    if (packet.Indirect != RASTER_INVALID)
    {
        FrameRetain(RasterKind::Buffer, packet.Indirect);
    }
    for (usize i = 0; i < At(RasterKind::Pipeline, packet.Pipeline).Pipeline.StreamCount; ++i)
    {
        FrameRetain(RasterKind::Buffer, packet.Vertices[i]);
    }
    RetainPacketTextures(packet);
    ++gDrawCount;
    return backend::RasterDraw(packet);
}
} // namespace
RasterStatus DrawIndexed(DeviceHandle device, const RasterDraw& draw) noexcept
{
    if (!FrameOpen())
    {
        const auto admission = Admission(device, false);
        return admission == RasterStatus::Ready ? RasterStatus::InvalidState : admission;
    }
    RasterPacket packet;
    const auto valid = ValidateDraw(device, draw, packet);
    if (valid != RasterStatus::Ready)
    {
        return valid;
    }
    const auto compatible = ValidatePacketPass(packet, gRasterPass, true);
    return compatible == RasterStatus::Ready ? EncodePacket(packet) : compatible;
}

namespace internal
{
void RasterExpect(uint32 request, RasterCallbacks callbacks) noexcept
{
    for (auto& records : gRecords)
    {
        for (auto& record : records)
        {
            if (record.Owner != 0 && record.Request == request)
            {
                record.Callbacks = static_cast<uint32>(callbacks);
                return;
            }
        }
    }
}
void RasterComplete(uint32 request, RasterStatus status) noexcept
{
    for (auto& records : gRecords)
    {
        for (auto& record : records)
        {
            if (record.Owner != 0 && record.Request == request && record.Callbacks != 0)
            {
                --record.Callbacks;
                if (status != RasterStatus::Ready)
                {
                    record.Status = status;
                }
                else if (record.Callbacks == 0 && record.Status == RasterStatus::Pending)
                {
                    record.Status = RasterStatus::Ready;
                }
                return;
            }
        }
    }
}
void RasterFail(uint32 request, RasterStatus status) noexcept
{
    for (auto& records : gRecords)
    {
        for (auto& record : records)
        {
            if (record.Owner != 0 && record.Request == request)
            {
                record.Status = status;
                return;
            }
        }
    }
}
bool PrepareRasterFrame() noexcept
{
    return backend::RasterPrepareStorage() == RasterStatus::Ready;
}
void RasterBeginFrame() noexcept
{
    gFrameSubmission = 0;
    gDrawCount = 0;
    gRasterPass = {};
    gGraphExecuting = false;
    gSurfaceColorDefined = true;
    gSurfaceDepthDefined = false;
    Collect();
}
void RasterEndFrame(bool accepted) noexcept
{
    if (gFrameSubmission != 0 && accepted)
    {
        (void)gSubmissions.Take();
        if (gFirstAcceptedSubmission == 0)
        {
            gFirstAcceptedSubmission = gFrameSubmission;
        }
        gLastAcceptedSubmission = gFrameSubmission;
        backend::RasterSubmit(gFrameSubmission);
    }
    if (!accepted)
    {
        backend::RasterDiscardSubmission();
    }
    for (auto& records : gRecords)
    {
        for (auto& record : records)
        {
            if (!record.InFrame)
            {
                continue;
            }
            if (accepted)
            {
                record.LastUse = gFrameSubmission;
                if (&records == &gRecords[static_cast<usize>(RasterKind::Texture)])
                {
                    record.TextureUse = record.FrameTextureUse;
                    record.ColorDefined = record.FrameColorDefined;
                    record.DepthDefined = record.FrameDepthDefined;
                    ++record.TextureRevision;
                }
            }
            if (accepted && record.FrameBufferWritten)
            {
                ++record.BufferRevision;
            }
            record.FrameBufferWritten = false;
            record.InFrame = false;
            --record.References;
        }
    }
    gFrameSubmission = 0;
    Collect();
}
void ReleaseRasterResources() noexcept
{
    // Shutdown owns all physical release. Native reset waits or drops the lost
    // device; browser callbacks retain their stable bookkeeping until delivery.
    backend::RayDrain();
    backend::RasterReset();
    backend::TimingReset();
    backend::LifetimeReset();
    ResetLifetimeRecords();
    ResetGraphRecords();
    for (usize kind = KINDS; kind-- > 0;)
    {
        for (usize slot = 0; slot < gRecords[kind].GetSize(); ++slot)
        {
            auto& record = gRecords[kind][slot];
            if (record.Owner == 0)
            {
                continue;
            }
            backend::RasterDestroy(static_cast<RasterKind>(kind), slot);
            ReleaseResourceStorage(record.StorageBytes);
            const auto generation = RasterNextGeneration(record.Generation);
            ReleaseHostStorage(record.AuxiliaryHostBytes);
            record.CpuTexture.Reset();
            record.Ray.Reset();
            record = {};
            record.Generation = generation;
        }
    }
    backend::RasterShutdown();
    gFirstAcceptedSubmission = 0;
    gLastAcceptedSubmission = 0;
    gFrameSubmission = 0;
    gDrawCount = 0;
}
} // namespace internal
RasterStatus GetAllocationTelemetry(DeviceHandle device, AllocationTelemetry& output) noexcept
{
    const auto status = Admission(device, false);
    if (status == RasterStatus::InvalidHandle)
    {
        return status;
    }
    output = {};
    if (status != RasterStatus::Ready)
    {
        return status;
    }
    static uint64 lastOwner = 0, peak = 0, blockPeak = 0;
    const auto owner = internal::DeviceOwner();
    if (lastOwner != owner)
    {
        lastOwner = owner;
        peak = 0;
        blockPeak = 0;
    }
    output.ApiBytesAvailable = backend::Kind() == Backend::Metal || backend::Kind() == Backend::Vulkan;
    for (usize kind = 0; kind < 2; ++kind)
    {
        for (usize slot = 0; slot < gRecords[kind].GetSize(); ++slot)
        {
            const auto& record = gRecords[kind][slot];
            if (record.Owner == 0)
            {
                continue;
            }
            output.LogicalBytes += record.StorageBytes;
            uint64 bytes = 0;
            if (!backend::RasterAllocationBytes(static_cast<RasterKind>(kind), slot, bytes))
            {
                output.ApiBytesAvailable = false;
                continue;
            }
            if (!foundation::TryAdd(output.ApiBytes, bytes, output.ApiBytes) ||
                (!record.Published && !foundation::TryAdd(output.RetiringApiBytes, bytes, output.RetiringApiBytes)))
            {
                output = {};
                return RasterStatus::CapacityExceeded;
            }
        }
    }
    if (!output.ApiBytesAvailable)
    {
        output.ApiBytes = 0;
        output.RetiringApiBytes = 0;
    }
    else if (output.ApiBytes > peak)
    {
        peak = output.ApiBytes;
    }
    output.PeakObservedApiBytes = peak;
    output.AllocatorBlockBytesAvailable = backend::AllocatorBlockBytes(output.AllocatorBlockBytes);
    if (output.AllocatorBlockBytesAvailable && output.AllocatorBlockBytes > blockPeak)
    {
        blockPeak = output.AllocatorBlockBytes;
    }
    output.PeakObservedAllocatorBlockBytes = output.AllocatorBlockBytesAvailable ? blockPeak : 0;
    return RasterStatus::Ready;
}
RasterStatus GetStorageUsage(DeviceHandle device, StorageUsage& output) noexcept
{
    const auto status = Admission(device, false);
    if (status == RasterStatus::InvalidHandle)
    {
        return status;
    }
    output = {};
    if (status != RasterStatus::Ready)
    {
        return status;
    }
    output = CurrentStorageUsage();
    for (auto& records : gRecords)
    {
        for (const auto& record : records)
        {
            if (record.Owner == 0)
            {
                continue;
            }
            if (record.Published)
            {
                ++output.PublishedRecords;
            }
            else
            {
                ++output.DetachedRecords;
            }
            if (record.Status == RasterStatus::Pending || record.Callbacks != 0)
            {
                ++output.PendingRecords;
            }
        }
    }
    return RasterStatus::Ready;
}
RasterStatus GetStatus(DeviceHandle device, BufferHandle handle) noexcept
{
    const auto admission = Admission(device, false);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    Collect();
    const auto current = Admission(device, false);
    if (current != RasterStatus::Ready)
    {
        return current;
    }
    const auto* record = Resolve(handle, RasterKind::Buffer);
    return record == nullptr ? RasterStatus::InvalidHandle : record->Status;
}
RasterStatus Destroy(DeviceHandle device, BufferHandle& handle) noexcept
{
    const auto admission = Admission(device);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    auto* record = Resolve(handle, RasterKind::Buffer);
    if (record == nullptr)
    {
        return RasterStatus::InvalidHandle;
    }
    record->Published = false;
    --record->References;
    handle = {};
    Collect();
    return RasterStatus::Ready;
}
RasterStatus GetStatus(DeviceHandle device, TextureHandle handle) noexcept
{
    const auto admission = Admission(device, false);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    Collect();
    const auto current = Admission(device, false);
    if (current != RasterStatus::Ready)
    {
        return current;
    }
    const auto* record = Resolve(handle, RasterKind::Texture);
    return record == nullptr ? RasterStatus::InvalidHandle : record->Status;
}
RasterStatus Destroy(DeviceHandle device, TextureHandle& handle) noexcept
{
    const auto admission = Admission(device);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    auto* record = Resolve(handle, RasterKind::Texture);
    if (record == nullptr)
    {
        return RasterStatus::InvalidHandle;
    }
    if (record->ServiceOwned)
    {
        return RasterStatus::InvalidState;
    }
    record->Published = false;
    --record->References;
    handle = {};
    Collect();
    return RasterStatus::Ready;
}
RasterStatus GetStatus(DeviceHandle device, TextureViewHandle handle) noexcept
{
    const auto admission = Admission(device, false);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    Collect();
    const auto current = Admission(device, false);
    if (current != RasterStatus::Ready)
    {
        return current;
    }
    const auto* record = Resolve(handle, RasterKind::View);
    return record == nullptr ? RasterStatus::InvalidHandle : record->Status;
}
RasterStatus Destroy(DeviceHandle device, TextureViewHandle& handle) noexcept
{
    const auto admission = Admission(device);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    auto* record = Resolve(handle, RasterKind::View);
    if (record == nullptr)
    {
        return RasterStatus::InvalidHandle;
    }
    record->Published = false;
    --record->References;
    handle = {};
    Collect();
    return RasterStatus::Ready;
}
RasterStatus GetStatus(DeviceHandle device, SamplerHandle handle) noexcept
{
    const auto admission = Admission(device, false);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    Collect();
    const auto current = Admission(device, false);
    if (current != RasterStatus::Ready)
    {
        return current;
    }
    const auto* record = Resolve(handle, RasterKind::Sampler);
    return record == nullptr ? RasterStatus::InvalidHandle : record->Status;
}
RasterStatus Destroy(DeviceHandle device, SamplerHandle& handle) noexcept
{
    const auto admission = Admission(device);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    auto* record = Resolve(handle, RasterKind::Sampler);
    if (record == nullptr)
    {
        return RasterStatus::InvalidHandle;
    }
    record->Published = false;
    --record->References;
    handle = {};
    Collect();
    return RasterStatus::Ready;
}
RasterStatus GetStatus(DeviceHandle device, RasterShaderHandle handle) noexcept
{
    const auto admission = Admission(device, false);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    Collect();
    const auto current = Admission(device, false);
    if (current != RasterStatus::Ready)
    {
        return current;
    }
    const auto* record = Resolve(handle, RasterKind::Shader);
    return record == nullptr ? RasterStatus::InvalidHandle : record->Status;
}
RasterStatus Destroy(DeviceHandle device, RasterShaderHandle& handle) noexcept
{
    const auto admission = Admission(device);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    auto* record = Resolve(handle, RasterKind::Shader);
    if (record == nullptr)
    {
        return RasterStatus::InvalidHandle;
    }
    record->Published = false;
    --record->References;
    handle = {};
    Collect();
    return RasterStatus::Ready;
}
RasterStatus GetStatus(DeviceHandle device, BindingLayoutHandle handle) noexcept
{
    const auto admission = Admission(device, false);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    Collect();
    const auto current = Admission(device, false);
    if (current != RasterStatus::Ready)
    {
        return current;
    }
    const auto* record = Resolve(handle, RasterKind::Layout);
    return record == nullptr ? RasterStatus::InvalidHandle : record->Status;
}
RasterStatus Destroy(DeviceHandle device, BindingLayoutHandle& handle) noexcept
{
    const auto admission = Admission(device);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    auto* record = Resolve(handle, RasterKind::Layout);
    if (record == nullptr)
    {
        return RasterStatus::InvalidHandle;
    }
    record->Published = false;
    --record->References;
    handle = {};
    Collect();
    return RasterStatus::Ready;
}
RasterStatus GetStatus(DeviceHandle device, BindingSetHandle handle) noexcept
{
    const auto admission = Admission(device, false);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    Collect();
    const auto current = Admission(device, false);
    if (current != RasterStatus::Ready)
    {
        return current;
    }
    const auto* record = Resolve(handle, RasterKind::Set);
    return record == nullptr ? RasterStatus::InvalidHandle : record->Status;
}
RasterStatus Destroy(DeviceHandle device, BindingSetHandle& handle) noexcept
{
    const auto admission = Admission(device);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    auto* record = Resolve(handle, RasterKind::Set);
    if (record == nullptr)
    {
        return RasterStatus::InvalidHandle;
    }
    record->Published = false;
    --record->References;
    handle = {};
    Collect();
    return RasterStatus::Ready;
}
RasterStatus GetStatus(DeviceHandle device, RasterPipelineHandle handle) noexcept
{
    const auto admission = Admission(device, false);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    Collect();
    const auto current = Admission(device, false);
    if (current != RasterStatus::Ready)
    {
        return current;
    }
    const auto* record = Resolve(handle, RasterKind::Pipeline);
    return record == nullptr ? RasterStatus::InvalidHandle : record->Status;
}
RasterStatus Destroy(DeviceHandle device, RasterPipelineHandle& handle) noexcept
{
    const auto admission = Admission(device);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    auto* record = Resolve(handle, RasterKind::Pipeline);
    if (record == nullptr)
    {
        return RasterStatus::InvalidHandle;
    }
    if (record->ServiceOwned)
    {
        return RasterStatus::InvalidState;
    }
    record->Published = false;
    --record->References;
    handle = {};
    Collect();
    return RasterStatus::Ready;
}
} // namespace ludus::graphics::rhi

#include "internal/lifetime_services.h"

#include "internal/compute_services.h"

#include "internal/ordered_graph.h"

#include "internal/ray_services.h"

namespace ludus::graphics::rhi
{
namespace
{
RasterStatus CpuTextureContent(const Record& record, uint32 mip, TextureCpuContent& output) noexcept
{
    if (record.Status != RasterStatus::Ready)
    {
        return record.Status == RasterStatus::Pending ? RasterStatus::NotReady : record.Status;
    }
    if (!record.CpuTexture)
    {
        return RasterStatus::Unsupported;
    }
    if (mip >= record.Texture.MipLevels)
    {
        return RasterStatus::InvalidDescription;
    }
    const auto& source = *record.CpuTexture;
    uint32 width = record.Texture.Width, height = record.Texture.Height;
    for (uint32 level = 0; level < mip; ++level)
    {
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
    }
    const auto& range = source.Mips[mip];
    output = {source.Bytes.GetData() + range.Offset,
              range.RowPitch * height,
              range.RowPitch,
              width,
              height,
              record.Texture.Format};
    return RasterStatus::Ready;
}
} // namespace
RasterStatus
GetTextureCpuContent(DeviceHandle device, TextureHandle handle, uint32 mip, TextureCpuContent& output) noexcept
{
    const auto status = Admission(device, false);
    if (status != RasterStatus::Ready)
    {
        return status;
    }
    Collect();
    const auto* record = Resolve(handle, RasterKind::Texture);
    return record == nullptr ? RasterStatus::InvalidHandle : CpuTextureContent(*record, mip, output);
}
RasterStatus
GetTextureCpuContent(DeviceHandle device, TextureViewHandle handle, uint32 mip, TextureCpuContent& output) noexcept
{
    const auto status = Admission(device, false);
    if (status != RasterStatus::Ready)
    {
        return status;
    }
    Collect();
    const auto* record = Resolve(handle, RasterKind::View);
    if (record == nullptr)
    {
        return RasterStatus::InvalidHandle;
    }
    if (record->Status != RasterStatus::Ready)
    {
        return record->Status == RasterStatus::Pending ? RasterStatus::NotReady : record->Status;
    }
    return CpuTextureContent(At(RasterKind::Texture, record->TextureSlot), mip, output);
}
} // namespace ludus::graphics::rhi
