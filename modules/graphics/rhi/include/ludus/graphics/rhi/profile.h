#pragma once

#include <ludus/foundation/base/types.h>

namespace ludus::graphics::rhi
{
/// Portable color/data formats; one layer and one sample, with immutable mip chains.
enum class RasterFormat : ludus::foundation::uint8
{
    /// Four unsigned normalized channels, interpreted as linear light.
    Rgba8Unorm,
    /// Four unsigned normalized channels; RGB decodes sRGB during sampling, alpha remains linear.
    Rgba8Srgb,
    /// Single unsigned normalized coverage/data channel; sampled only, never an attachment.
    R8Unorm,
    /// Two normalized linear data channels; sampled only, never an attachment.
    Rg8Unorm,
};
/// Enabled bounded raster profile, published only for a ready device.
struct RasterCapabilities final
{
    /// Maximum bytes per buffer; zero when unavailable.
    ludus::foundation::usize MaxBufferSize = 0;
    /// Maximum width/height of sampled textures.
    ludus::foundation::uint32 MaxTextureDimension2D = 0;
    /// Uniform range offset alignment in bytes.
    ludus::foundation::usize UniformOffsetAlignment = 0;
    /// Maximum bound uniform range in bytes.
    ludus::foundation::usize MaxUniformRange = 0;
    /// Maximum records of each resource kind, including retained/detached records.
    ludus::foundation::uint32 ResourcesPerKind = 0;
    /// Maximum accepted draws in a frame; exhaustion rejects before native work.
    ludus::foundation::uint32 DrawsPerFrame = 0;
};
/// Bounded single-queue buffer compute profile; zero fields mean unavailable.
/// Storage textures, atomics between queues and indirect dispatch are deferred.
/// Layouts contain at most four storage bindings and eight total buffer bindings.
struct ComputeCapabilities final
{
    /// Maximum storage binding range in bytes.
    ludus::foundation::usize MaxStorageRange = 0;
    /// Storage binding offset alignment in bytes.
    ludus::foundation::usize StorageOffsetAlignment = 0;
    /// Maximum local threads along each axis.
    ludus::foundation::uint32 MaxWorkgroupSize[3]{};
    /// Maximum product of local workgroup dimensions.
    ludus::foundation::uint32 MaxWorkgroupInvocations = 0;
    /// Maximum dispatched workgroups along each axis.
    ludus::foundation::uint32 MaxDispatch[3]{};
};
/// Implemented texture operations; native support alone never enables an engine API.
/// Storage and general copy uses currently remain false on every backend.
struct TextureUses final
{
    /// Complete original-format sampled views.
    bool Sampled = false;
    /// Linear filtering of sampled texels.
    bool Filterable = false;
    /// Single color attachment with private depth, also usable for sampling.
    bool ColorAttachment = false;
    /// Premultiplied-alpha blending into that color attachment.
    bool Blendable = false;
    /// Shader storage texture access; currently unimplemented.
    bool Storage = false;
    /// General texture-copy source; readback of the frame does not imply this use.
    bool CopySource = false;
    /// General texture-copy destination; creation uploads do not imply this use.
    bool CopyDestination = false;
};
/// Enabled format capabilities, bounded by both the engine and negotiated backend.
/// Zero means unavailable. Dimensions are pixels; counts are not reservations.
struct FormatCapabilities final
{
    /// Supported operations for this format.
    TextureUses Uses;
    /// Maximum sampled width/height for this format's complete mip chain.
    ludus::foundation::uint32 MaxSampledDimension2D = 0;
    /// Maximum attachment width/height, including its private depth allocation.
    ludus::foundation::uint32 MaxAttachmentDimension2D = 0;
    /// Maximum sampled mip count, additionally bounded by the texture dimensions.
    ludus::foundation::uint32 MaxMipLevels = 0;
    /// Attachment sample counts as a bit mask: bit value 1 means one sample, 4 means four.
    /// The current profile supports only 1; zero when no attachment is supported.
    ludus::foundation::uint32 SampleCounts = 0;
};
/// Fixed public raster interface bounds, independently of larger native adapter limits.
struct RasterInterfaceCapabilities final
{
    /// Maximum group-zero binding entries.
    ludus::foundation::uint32 MaxBindings = 0;
    /// Maximum vertex/instance streams.
    ludus::foundation::uint32 MaxVertexBuffers = 0;
    /// Maximum float vertex attributes.
    ludus::foundation::uint32 MaxVertexAttributes = 0;
    /// Maximum four-byte-aligned vertex stride in bytes; 252 on WebGL 2, 256 elsewhere.
    ludus::foundation::uint32 MaxVertexStride = 0;
    /// Maximum color attachments in one raster pass.
    ludus::foundation::uint32 MaxColorAttachments = 0;
    /// Maximum texture array layers; array views are not implemented.
    ludus::foundation::uint32 MaxTextureLayers = 0;
};
/// Immutable effective profile snapshot while the device is ready; zero otherwise.
/// Formats are indexed by RasterFormat's underlying value, in declaration order.
/// This describes enabled engine operations, not an unbounded raw adapter catalog.
struct DeviceProfile final
{
    /// Negotiated bounded resource/draw limits.
    RasterCapabilities Raster;
    /// Public interface limits; zero if portable raster is unavailable.
    RasterInterfaceCapabilities Interface;
    /// Negotiated bounded buffer-compute limits; zero if unavailable.
    ComputeCapabilities Compute;
    /// Four format entries indexed by RasterFormat, including unsupported uses.
    FormatCapabilities Formats[4]{};
};
/// Raster admission constraints; zero fields impose no additional requirement.
/// Nonzero fields imply required portable raster for required profile admission.
struct RasterRequirements final
{
    /// Minimum allocation size in bytes that the profile must accept.
    ludus::foundation::usize MinBufferSize = 0;
    /// Minimum sampled texture width/height in pixels.
    ludus::foundation::uint32 MinTextureDimension2D = 0;
    /// Largest acceptable uniform offset alignment in bytes, not a minimum.
    ludus::foundation::usize MaxUniformOffsetAlignment = 0;
    /// Minimum uniform binding range in bytes.
    ludus::foundation::usize MinUniformRange = 0;
    /// Minimum total records per kind, including retained records.
    ludus::foundation::uint32 MinResourcesPerKind = 0;
    /// Minimum accepted draws per frame.
    ludus::foundation::uint32 MinDrawsPerFrame = 0;
    /// Minimum group-zero binding entries.
    ludus::foundation::uint32 MinBindings = 0;
    /// Minimum vertex/instance streams.
    ludus::foundation::uint32 MinVertexBuffers = 0;
    /// Minimum vertex attributes.
    ludus::foundation::uint32 MinVertexAttributes = 0;
    /// Minimum accepted vertex stride in bytes.
    ludus::foundation::uint32 MinVertexStride = 0;
    /// Minimum color attachments per pass.
    ludus::foundation::uint32 MinColorAttachments = 0;
    /// Minimum texture array layers.
    ludus::foundation::uint32 MinTextureLayers = 0;
};
/// Compute admission constraints; zero fields impose no additional requirement.
/// Nonzero fields imply required compute for required profile admission.
struct ComputeRequirements final
{
    /// Minimum storage binding range in bytes.
    ludus::foundation::usize MinStorageRange = 0;
    /// Largest acceptable storage offset alignment in bytes.
    ludus::foundation::usize MaxStorageOffsetAlignment = 0;
    /// Minimum supported local dimension on each axis; not one combined workgroup.
    ludus::foundation::uint32 MinWorkgroupSize[3]{};
    /// Minimum supported product of local dimensions.
    ludus::foundation::uint32 MinWorkgroupInvocations = 0;
    /// Minimum supported dispatched group count along each axis.
    ludus::foundation::uint32 MinDispatch[3]{};
};
/// Required or preferred use of one known format. Uses are checked together.
struct FormatRequirement final
{
    /// Format identity; malformed values reject before any backend request.
    RasterFormat Format = RasterFormat::Rgba8Unorm;
    /// Requested implemented uses; at least one must be selected.
    TextureUses Uses;
    /// Minimum width/height in pixels for every selected sampled/attachment use.
    ludus::foundation::uint32 MinDimension2D = 0;
    /// Minimum sampled mip count; attachments support only one.
    ludus::foundation::uint32 MinMipLevels = 1;
    /// Required attachment sample count; a power of two, 1..64. No MSAA is implied.
    ludus::foundation::uint32 SampleCount = 1;
};
/// Copied bounded required/preferred profile constraints. Unused array entries are ignored.
/// Required constraints reject before Ready; preferred constraints only produce diagnostics.
struct DeviceProfileRequirements final
{
    /// Additional raster limits.
    RasterRequirements Raster;
    /// Additional compute limits.
    ComputeRequirements Compute;
    /// At most four unique known format requests.
    FormatRequirement Formats[4]{};
    /// Number of valid entries in Formats; larger counts or duplicates are malformed.
    ludus::foundation::uint32 FormatCount = 0;
};
/// First unmet constraint, in raster-field order, compute-field order, then format array order.
enum class ProfileRequirement : ludus::foundation::uint8
{
    /// Every requested constraint was met, or no diagnostic is available yet.
    None,
    /// Buffer allocation size is insufficient.
    BufferSize,
    /// General sampled texture dimension is insufficient.
    TextureDimension,
    /// Uniform offsets require a larger alignment than accepted.
    UniformAlignment,
    /// Uniform binding range is insufficient.
    UniformRange,
    /// Total resource records per kind are insufficient.
    ResourceCount,
    /// Draw capacity is insufficient.
    DrawCount,
    /// Binding entry capacity is insufficient.
    BindingCount,
    /// Vertex stream capacity is insufficient.
    VertexBufferCount,
    /// Vertex attribute capacity is insufficient.
    VertexAttributeCount,
    /// Vertex stride bound is insufficient.
    VertexStride,
    /// Color attachment capacity is insufficient.
    ColorAttachmentCount,
    /// Texture layer capacity is insufficient.
    TextureLayerCount,
    /// Storage binding range is insufficient or compute is unavailable.
    StorageRange,
    /// Storage offsets require a larger alignment than accepted.
    StorageAlignment,
    /// A local workgroup axis bound is insufficient.
    WorkgroupSize,
    /// Workgroup invocation capacity is insufficient.
    WorkgroupInvocations,
    /// A dispatch axis bound is insufficient.
    DispatchCount,
    /// Sampled use is unavailable.
    Sampled,
    /// Linear filtering is unavailable.
    Filterable,
    /// Color attachment use is unavailable.
    ColorAttachment,
    /// Attachment blending is unavailable.
    Blendable,
    /// Storage texture use is unavailable.
    Storage,
    /// General texture-copy source use is unavailable.
    CopySource,
    /// General texture-copy destination use is unavailable.
    CopyDestination,
    /// The format's requested dimensions exceed its selected-use bound.
    FormatDimension,
    /// The format's mip count is insufficient for the requested use.
    MipCount,
    /// The format's attachment sample count is unsupported.
    SampleCount,
};
/// Bounded negotiation diagnostic; no strings or borrowed request data.
struct ProfileDiagnostic final
{
    /// First unmet constraint; None means the other fields have no diagnostic meaning.
    ProfileRequirement Requirement = ProfileRequirement::None;
    /// Format identity for format constraints; otherwise unused.
    RasterFormat Format = RasterFormat::Rgba8Unorm;
    /// Zero-based X/Y/Z axis for WorkgroupSize or DispatchCount; otherwise unused.
    ludus::foundation::uint32 Axis = 0;
    /// Requested threshold/count; use flags report 1. Sizes/alignments are bytes.
    ludus::foundation::uint64 Requested = 0;
    /// Effective bound; use flags report 0. SampleCount reports the supported count mask.
    ludus::foundation::uint64 Available = 0;
};
} // namespace ludus::graphics::rhi
