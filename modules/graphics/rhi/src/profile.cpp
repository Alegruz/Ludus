#include "internal/profile.h"
#include "internal/raster.h"
#include "internal/storage.h"

namespace ludus::graphics::rhi::internal
{
using namespace foundation;
namespace
{
bool Any(const TextureUses& uses) noexcept
{
    return uses.Sampled || uses.Filterable || uses.ColorAttachment || uses.Blendable || uses.Storage ||
           uses.CopySource || uses.CopyDestination;
}
} // namespace
bool ValidProfileRequirements(const DeviceProfileRequirements& requirements) noexcept
{
    if (requirements.FormatCount > 7)
    {
        return false;
    }
    for (uint32 i = 0; i < requirements.FormatCount; ++i)
    {
        const auto& entry = requirements.Formats[i];
        if (static_cast<uint8>(entry.Format) > static_cast<uint8>(RasterFormat::Bc3RgbaUnorm) || !Any(entry.Uses) ||
            static_cast<uint8>(entry.Dimension) > static_cast<uint8>(TextureDimension::D3) ||
            ((entry.Uses.ColorAttachment || entry.Uses.Blendable) && entry.Dimension == TextureDimension::D3) ||
            (entry.Dimension == TextureDimension::D2 && entry.MinDepthOrLayers > 1) ||
            (entry.Dimension == TextureDimension::Cube && entry.MinDepthOrLayers > 6) || entry.MinMipLevels == 0 ||
            entry.SampleCount == 0 || entry.SampleCount > 64 || (entry.SampleCount & (entry.SampleCount - 1)) != 0 ||
            (entry.SampleCount != 1 && !entry.Uses.ColorAttachment && !entry.Uses.Blendable))
        {
            return false;
        }
        for (uint32 j = 0; j < i; ++j)
        {
            if (entry.Format == requirements.Formats[j].Format)
            {
                return false;
            }
        }
    }
    return true;
}
bool NeedsRaster(const DeviceProfileRequirements& requirements) noexcept
{
    const auto& r = requirements.Raster;
    return requirements.FormatCount != 0 || r.MinBufferSize != 0 || r.MinTextureDimension2D != 0 ||
           r.MaxUniformOffsetAlignment != 0 || r.MinUniformRange != 0 || r.MinResourcesPerKind != 0 ||
           r.MinDrawsPerFrame != 0 || r.MinBindings != 0 || r.MinVertexBuffers != 0 || r.MinVertexAttributes != 0 ||
           r.MinVertexStride != 0 || r.MinColorAttachments != 0 || r.MinTextureLayers != 0;
}
bool NeedsCompute(const DeviceProfileRequirements& requirements) noexcept
{
    const auto& c = requirements.Compute;
    if (c.MinStorageRange != 0 || c.MaxStorageOffsetAlignment != 0 || c.MinWorkgroupInvocations != 0)
    {
        return true;
    }
    for (usize i = 0; i < 3; ++i)
    {
        if (c.MinWorkgroupSize[i] != 0 || c.MinDispatch[i] != 0)
        {
            return true;
        }
    }
    return false;
}
ProfileDiagnostic CheckProfile(const DeviceProfileRequirements& requirements, const DeviceProfile& profile) noexcept
{
    if (requirements.Raster.MinBufferSize != 0 && requirements.Raster.MinBufferSize > profile.Raster.MaxBufferSize)
    {
        return
        {
            .Requirement = ProfileRequirement::BufferSize,
            .Requested = requirements.Raster.MinBufferSize,
            .Available = profile.Raster.MaxBufferSize,
        };
    }
    if (requirements.Raster.MinTextureDimension2D != 0 &&
        requirements.Raster.MinTextureDimension2D > profile.Raster.MaxTextureDimension2D)
    {
        return
        {
            .Requirement = ProfileRequirement::TextureDimension,
            .Requested = requirements.Raster.MinTextureDimension2D,
            .Available = profile.Raster.MaxTextureDimension2D,
        };
    }
    if (requirements.Raster.MaxUniformOffsetAlignment != 0 &&
        (profile.Raster.UniformOffsetAlignment == 0 ||
         profile.Raster.UniformOffsetAlignment > requirements.Raster.MaxUniformOffsetAlignment))
    {
        return
        {
            .Requirement = ProfileRequirement::UniformAlignment,
            .Requested = requirements.Raster.MaxUniformOffsetAlignment,
            .Available = profile.Raster.UniformOffsetAlignment,
        };
    }
    if (requirements.Raster.MinUniformRange != 0 &&
        requirements.Raster.MinUniformRange > profile.Raster.MaxUniformRange)
    {
        return
        {
            .Requirement = ProfileRequirement::UniformRange,
            .Requested = requirements.Raster.MinUniformRange,
            .Available = profile.Raster.MaxUniformRange,
        };
    }
    if (requirements.Raster.MinResourcesPerKind != 0 &&
        requirements.Raster.MinResourcesPerKind > profile.Raster.ResourcesPerKind)
    {
        return
        {
            .Requirement = ProfileRequirement::ResourceCount,
            .Requested = requirements.Raster.MinResourcesPerKind,
            .Available = profile.Raster.ResourcesPerKind,
        };
    }
    if (requirements.Raster.MinDrawsPerFrame != 0 &&
        requirements.Raster.MinDrawsPerFrame > profile.Raster.DrawsPerFrame)
    {
        return
        {
            .Requirement = ProfileRequirement::DrawCount,
            .Requested = requirements.Raster.MinDrawsPerFrame,
            .Available = profile.Raster.DrawsPerFrame,
        };
    }
    if (requirements.Raster.MinBindings != 0 && requirements.Raster.MinBindings > profile.Interface.MaxBindings)
    {
        return
        {
            .Requirement = ProfileRequirement::BindingCount,
            .Requested = requirements.Raster.MinBindings,
            .Available = profile.Interface.MaxBindings,
        };
    }
    if (requirements.Raster.MinVertexBuffers != 0 &&
        requirements.Raster.MinVertexBuffers > profile.Interface.MaxVertexBuffers)
    {
        return
        {
            .Requirement = ProfileRequirement::VertexBufferCount,
            .Requested = requirements.Raster.MinVertexBuffers,
            .Available = profile.Interface.MaxVertexBuffers,
        };
    }
    if (requirements.Raster.MinVertexAttributes != 0 &&
        requirements.Raster.MinVertexAttributes > profile.Interface.MaxVertexAttributes)
    {
        return
        {
            .Requirement = ProfileRequirement::VertexAttributeCount,
            .Requested = requirements.Raster.MinVertexAttributes,
            .Available = profile.Interface.MaxVertexAttributes,
        };
    }
    if (requirements.Raster.MinVertexStride != 0 &&
        requirements.Raster.MinVertexStride > profile.Interface.MaxVertexStride)
    {
        return
        {
            .Requirement = ProfileRequirement::VertexStride,
            .Requested = requirements.Raster.MinVertexStride,
            .Available = profile.Interface.MaxVertexStride,
        };
    }
    if (requirements.Raster.MinColorAttachments != 0 &&
        requirements.Raster.MinColorAttachments > profile.Interface.MaxColorAttachments)
    {
        return
        {
            .Requirement = ProfileRequirement::ColorAttachmentCount,
            .Requested = requirements.Raster.MinColorAttachments,
            .Available = profile.Interface.MaxColorAttachments,
        };
    }
    if (requirements.Raster.MinTextureLayers != 0 &&
        requirements.Raster.MinTextureLayers > profile.Interface.MaxTextureLayers)
    {
        return
        {
            .Requirement = ProfileRequirement::TextureLayerCount,
            .Requested = requirements.Raster.MinTextureLayers,
            .Available = profile.Interface.MaxTextureLayers,
        };
    }
    if (requirements.Compute.MinStorageRange != 0 &&
        requirements.Compute.MinStorageRange > profile.Compute.MaxStorageRange)
    {
        return
        {
            .Requirement = ProfileRequirement::StorageRange,
            .Requested = requirements.Compute.MinStorageRange,
            .Available = profile.Compute.MaxStorageRange,
        };
    }
    if (requirements.Compute.MaxStorageOffsetAlignment != 0 &&
        (profile.Compute.StorageOffsetAlignment == 0 ||
         profile.Compute.StorageOffsetAlignment > requirements.Compute.MaxStorageOffsetAlignment))
    {
        return
        {
            .Requirement = ProfileRequirement::StorageAlignment,
            .Requested = requirements.Compute.MaxStorageOffsetAlignment,
            .Available = profile.Compute.StorageOffsetAlignment,
        };
    }
    for (usize i = 0; i < 3; ++i)
    {
        if (requirements.Compute.MinWorkgroupSize[i] > profile.Compute.MaxWorkgroupSize[i])
        {
            return
            {
                .Requirement = ProfileRequirement::WorkgroupSize,
                .Axis = static_cast<uint32>(i),
                .Requested = requirements.Compute.MinWorkgroupSize[i],
                .Available = profile.Compute.MaxWorkgroupSize[i],
            };
        }
    }
    if (requirements.Compute.MinWorkgroupInvocations > profile.Compute.MaxWorkgroupInvocations)
    {
        return
        {
            .Requirement = ProfileRequirement::WorkgroupInvocations,
            .Requested = requirements.Compute.MinWorkgroupInvocations,
            .Available = profile.Compute.MaxWorkgroupInvocations,
        };
    }
    for (usize i = 0; i < 3; ++i)
    {
        if (requirements.Compute.MinDispatch[i] > profile.Compute.MaxDispatch[i])
        {
            return
            {
                .Requirement = ProfileRequirement::DispatchCount,
                .Axis = static_cast<uint32>(i),
                .Requested = requirements.Compute.MinDispatch[i],
                .Available = profile.Compute.MaxDispatch[i],
            };
        }
    }
    for (uint32 i = 0; i < requirements.FormatCount; ++i)
    {
        const auto& request = requirements.Formats[i];
        const auto& format = profile.Formats[static_cast<usize>(request.Format)];
        if (request.Uses.Sampled && !format.Uses.Sampled)
        {
            return { .Requirement = ProfileRequirement::Sampled, .Format = request.Format, .Requested = 1 };
        }
        if (request.Uses.Filterable && !format.Uses.Filterable)
        {
            return { .Requirement = ProfileRequirement::Filterable, .Format = request.Format, .Requested = 1 };
        }
        if (request.Uses.ColorAttachment && !format.Uses.ColorAttachment)
        {
            return { .Requirement = ProfileRequirement::ColorAttachment, .Format = request.Format, .Requested = 1 };
        }
        if (request.Uses.Blendable && !format.Uses.Blendable)
        {
            return { .Requirement = ProfileRequirement::Blendable, .Format = request.Format, .Requested = 1 };
        }
        if (request.Uses.Storage && !format.Uses.Storage)
        {
            return { .Requirement = ProfileRequirement::Storage, .Format = request.Format, .Requested = 1 };
        }
        if (request.Uses.CopySource &&
            (!format.Uses.CopySource || request.Dimension != TextureDimension::D2 || request.SampleCount != 1))
        {
            return { .Requirement = ProfileRequirement::CopySource, .Format = request.Format, .Requested = 1 };
        }
        if (request.Uses.CopyDestination &&
            (!format.Uses.CopyDestination || request.Dimension != TextureDimension::D2 || request.SampleCount != 1))
        {
            return { .Requirement = ProfileRequirement::CopyDestination, .Format = request.Format, .Requested = 1 };
        }
        if (request.PartialViews && !format.PartialViews)
        {
            return { .Requirement = ProfileRequirement::PartialViews, .Format = request.Format, .Requested = 1 };
        }
        if (request.AttachmentViews && !format.AttachmentViews)
        {
            return { .Requirement = ProfileRequirement::AttachmentViews, .Format = request.Format, .Requested = 1 };
        }
        if (request.DepthSampled && !format.DepthSampled)
        {
            return { .Requirement = ProfileRequirement::DepthSampled, .Format = request.Format, .Requested = 1 };
        }
        if (request.StencilAttachment && !format.StencilAttachment)
        {
            return { .Requirement = ProfileRequirement::StencilAttachment, .Format = request.Format, .Requested = 1 };
        }
        if (request.StencilSampled && !format.StencilSampled)
        {
            return { .Requirement = ProfileRequirement::StencilSampled, .Format = request.Format, .Requested = 1 };
        }
        const auto sampledLayers = request.Dimension == TextureDimension::D3 ? format.MaxDimension3D
                                   : request.Dimension == TextureDimension::Cube
                                       ? (format.MaxCubeDimension != 0 ? 6U : 0U)
                                       : format.MaxArrayLayers;
        const auto layers =
            (request.Uses.ColorAttachment || request.Uses.Blendable) && format.MaxAttachmentLayers < sampledLayers
                ? format.MaxAttachmentLayers
                : sampledLayers;
        if (request.MinDepthOrLayers > layers)
        {
            return
            {
                .Requirement = ProfileRequirement::TextureLayerCount,
                .Format = request.Format,
                .Requested = request.MinDepthOrLayers,
                .Available = layers,
            };
        }
        const bool sampled = request.Uses.Sampled || request.Uses.Filterable;
        const bool attachment = request.Uses.ColorAttachment || request.Uses.Blendable;
        const uint32 sampledDimension = request.Dimension == TextureDimension::D3     ? format.MaxDimension3D
                                        : request.Dimension == TextureDimension::Cube ? format.MaxCubeDimension
                                        : request.Dimension == TextureDimension::D2Array && format.MaxArrayLayers < 2
                                            ? 0U
                                            : format.MaxSampledDimension2D;
        if (sampled && sampledDimension == 0)
        {
            return { .Requirement = ProfileRequirement::FormatDimension, .Format = request.Format, .Requested = 1 };
        }
        uint32 dimension = sampled ? sampledDimension : format.MaxAttachmentDimension2D;
        const auto attachmentDimension = request.Dimension == TextureDimension::Cube ? format.MaxAttachmentCubeDimension
                                                                                     : format.MaxAttachmentDimension2D;
        if (attachment && attachmentDimension == 0)
        {
            return { .Requirement = ProfileRequirement::FormatDimension, .Format = request.Format, .Requested = 1 };
        }
        if (attachment && attachmentDimension < dimension)
        {
            dimension = attachmentDimension;
        }
        if (request.MinDimension2D > dimension)
        {
            return
            {
                .Requirement = ProfileRequirement::FormatDimension,
                .Format = request.Format,
                .Requested = request.MinDimension2D,
                .Available = dimension,
            };
        }
        uint32 extentMips = 1;
        for (uint32 extent = sampledDimension; extent > 1; extent >>= 1U)
        {
            ++extentMips;
        }
        const uint32 sampledMips = format.MaxMipLevels < extentMips ? format.MaxMipLevels : extentMips;
        const uint32 mips = request.SampleCount > 1                                     ? 1
                            : attachment && format.MaxAttachmentMipLevels < sampledMips ? format.MaxAttachmentMipLevels
                                                                                        : sampledMips;
        if (request.MinMipLevels > mips)
        {
            return
            {
                .Requirement = ProfileRequirement::MipCount,
                .Format = request.Format,
                .Requested = request.MinMipLevels,
                .Available = mips,
            };
        }
        const auto sampleCounts =
            request.Dimension != TextureDimension::D2 || sampled || request.DepthSampled || request.StencilSampled
                ? format.SampleCounts & 1U
                : format.SampleCounts;
        if (attachment && (sampleCounts & request.SampleCount) == 0)
        {
            return
            {
                .Requirement = ProfileRequirement::SampleCount,
                .Format = request.Format,
                .Requested = request.SampleCount,
                .Available = sampleCounts,
            };
        }
    }
    return {};
}
DeviceProfile QueryDeviceProfile() noexcept
{
    DeviceProfile result;
    result.Raster = backend::RasterLimits();
    result.Storage = CurrentStorageLimits();
    if (result.Raster.ResourcesPerKind > result.Storage.ResourcesPerKind)
    {
        result.Raster.ResourcesPerKind = result.Storage.ResourcesPerKind;
    }
    if (result.Raster.DrawsPerFrame > result.Storage.DrawsPerFrame)
    {
        result.Raster.DrawsPerFrame = result.Storage.DrawsPerFrame;
    }
    if (result.Raster.MaxUniformRange < 16384)
    {
        return {};
    }
    result.Interface = {static_cast<uint32>(RASTER_BINDING_COUNT),
                        2,
                        result.Raster.MaxVertexAttributes,
                        backend::RasterVertexStrideLimit(),
                        backend::RasterColorAttachmentLimit(),
                        1};
    result.Compute = backend::ComputeLimits();
    result.Binding = backend::BindingLimits();
    for (usize i = 0; i < 7; ++i)
    {
        result.Formats[i] = backend::FormatLimits(static_cast<RasterFormat>(i));
        if (result.Formats[i].MaxArrayLayers > result.Interface.MaxTextureLayers)
        {
            result.Interface.MaxTextureLayers = result.Formats[i].MaxArrayLayers;
        }
    }
    result.Raster.HdrColor = result.Formats[static_cast<usize>(RasterFormat::Rgba16Float)].Uses.Blendable;
    return result;
}
FormatCapabilities PortableFormatLimits(RasterFormat format, const RasterCapabilities& limits) noexcept
{
    if (limits.MaxUniformRange < 16384 || limits.MaxTextureDimension2D == 0 ||
        static_cast<uint8>(format) > static_cast<uint8>(RasterFormat::Bc3RgbaUnorm))
    {
        return {};
    }
    const bool hdr = format == RasterFormat::Rgba16Float;
    const bool attachment =
        !CompressedTexture(format) &&
        ((hdr && limits.HdrColor) || format == RasterFormat::Rgba8Unorm || format == RasterFormat::Rgba8Srgb);
    uint32 levels = 1;
    for (auto extent = limits.MaxTextureDimension2D; extent > 1; extent /= 2)
    {
        ++levels;
    }
    return
    {
        .Uses =
        {
            .Sampled = true,
            .Filterable = true,
            .ColorAttachment = attachment,
            .Blendable = attachment,
            .CopySource = format == RasterFormat::Rgba8Unorm,
            .CopyDestination = format == RasterFormat::Rgba8Unorm,
        },
        .MaxSampledDimension2D = limits.MaxTextureDimension2D,
        .MaxAttachmentDimension2D = attachment ? limits.MaxTextureDimension2D : 0,
        .MaxMipLevels = levels < 16 ? levels : 16,
        .SampleCounts = attachment ? 1U : 0U,
        .MaxArrayLayers = 1,
    };
}
} // namespace ludus::graphics::rhi::internal

namespace ludus::graphics::rhi::internal
{
namespace
{
bool gIsBindlessRequested = false;
bool gIsBindlessRequired = false;
StorageLimits gStorageLimits;
StorageUsage gStorageUsage;
RasterStatus gStorageFailure = RasterStatus::CapacityExceeded;
#if defined(LUDUS_RHI_TEST_STORAGE)
bool gFailStorageAllocation = false;
#endif
} // namespace
bool IsBindlessRequested() noexcept
{
    return gIsBindlessRequested;
}
bool IsBindlessRequired() noexcept
{
    return gIsBindlessRequired;
}
void SetBindlessRequest(bool isRequested, bool isRequired) noexcept
{
    gIsBindlessRequested = isRequested;
    gIsBindlessRequired = isRequired;
}
const StorageLimits& CurrentStorageLimits() noexcept
{
    return gStorageLimits;
}
bool ValidStorageLimits(const StorageLimits& limits) noexcept
{
    constexpr foundation::uint32 invalid = ~foundation::uint32{0};
    const foundation::uint32 counts[]{limits.ResourcesPerKind,
                                      limits.BindingDescriptors,
                                      limits.DescriptorsPerSet,
                                      limits.RayResourcesPerKind,
                                      limits.RayInstances,
                                      limits.DrawsPerFrame,
                                      limits.Batches,
                                      limits.DrawsPerPlan,
                                      limits.Graphs,
                                      limits.ResourcesPerGraph,
                                      limits.PassesPerGraph,
                                      limits.DependenciesPerGraph,
                                      limits.TransientTextures,
                                      limits.RasterPassStates,
                                      limits.Submissions};
    for (const auto count : counts)
    {
        if (count == 0 || count == invalid)
        {
            return false;
        }
    }
    return limits.HostBytes != 0 && limits.ResourceBytes != 0 && limits.TransferSlices > 0 &&
           limits.TransferSlices <= 4 && limits.TransferBytes >= 256 &&
           limits.TransferBytes <= foundation::usize{256} * 1024 && limits.TransferBytes % 4 == 0;
}
bool ConfigureStorage(const StorageLimits& limits) noexcept
{
    if (!ValidStorageLimits(limits) || limits.HostBytes < gStorageUsage.HostBytes ||
        limits.ResourceBytes < gStorageUsage.ResourceBytes)
    {
        return false;
    }
    gStorageLimits = limits;
    return true;
}
RasterStatus StorageFailure() noexcept
{
    return gStorageFailure;
}
bool ReserveHostStorage(foundation::usize bytes) noexcept
{
#if defined(LUDUS_RHI_TEST_STORAGE)
    if (gFailStorageAllocation)
    {
        gFailStorageAllocation = false;
        gStorageFailure = RasterStatus::OutOfMemory;
        return false;
    }
#endif
    gStorageFailure = FrameOpen() ? RasterStatus::InvalidState : RasterStatus::CapacityExceeded;
    if (FrameOpen() || bytes > gStorageLimits.HostBytes - gStorageUsage.HostBytes)
    {
        return false;
    }
    gStorageUsage.HostBytes += bytes;
    if (gStorageUsage.HostBytes > gStorageUsage.PeakHostBytes)
    {
        gStorageUsage.PeakHostBytes = gStorageUsage.HostBytes;
    }
    return true;
}
void ReleaseHostStorage(foundation::usize bytes) noexcept
{
    gStorageUsage.HostBytes -= bytes;
}
void StorageAllocationFailed() noexcept
{
    gStorageFailure = RasterStatus::OutOfMemory;
}
void StorageCapacityFailed() noexcept
{
    gStorageFailure = RasterStatus::CapacityExceeded;
}
#if defined(LUDUS_RHI_TEST_STORAGE)
void FailNextStorageAllocation() noexcept
{
    gFailStorageAllocation = true;
}
#endif
bool ReserveResourceStorage(foundation::uint64 bytes) noexcept
{
    gStorageFailure = RasterStatus::CapacityExceeded;
    if (bytes > gStorageLimits.ResourceBytes - gStorageUsage.ResourceBytes)
    {
        return false;
    }
    gStorageUsage.ResourceBytes += bytes;
    if (gStorageUsage.ResourceBytes > gStorageUsage.PeakResourceBytes)
    {
        gStorageUsage.PeakResourceBytes = gStorageUsage.ResourceBytes;
    }
    return true;
}
void ReleaseResourceStorage(foundation::uint64 bytes) noexcept
{
    gStorageUsage.ResourceBytes -= bytes;
}
StorageUsage CurrentStorageUsage() noexcept
{
    return gStorageUsage;
}
} // namespace ludus::graphics::rhi::internal
