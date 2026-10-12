#pragma once

#include <ludus/foundation/base/types.h>

namespace ludus::graphics::rhi
{
/// Allocation/sampled-view dimensionality; cube faces use +X,-X,+Y,-Y,+Z,-Z order.
enum class TextureDimension : ludus::foundation::uint8
{
    /// One two-dimensional image.
    D2,
    /// Fixed array of two-dimensional images; layers do not shrink with mips.
    D2Array,
    /// Six square two-dimensional faces.
    Cube,
    /// Volume; depth shrinks with each mip and cannot be selected as array layers.
    D3,
};
/// Original sampled color/data formats; dimensions and uses are independently negotiated.
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
    /// Linear binary16 sampled RGBA; attachment/filter/blend support is capability-gated.
    /// Original binary16 upload bits are accepted without conversion; attachment use remains separately enabled.
    Rgba16Float,
    /// BC1 RGBA normalized 4x4 blocks (8 bytes), sampled when enabled; no automatic compression.
    Bc1RgbaUnorm,
    /// BC3 RGBA normalized 4x4 blocks (16 bytes), sampled when enabled; no automatic compression.
    Bc3RgbaUnorm,
};
/// Copied R6 storage maxima and explicit byte budgets. Configure before startup.
/// Counts are limits, not reservations; creation may fail under byte/allocation pressure.
/// Defaults preserve the original general profiles. The fullscreen facade is separate.
/// Counts must be nonzero and below UINT32_MAX; byte budgets must be nonzero.
/// Stable metadata pages persist across device restart; startup rejects a host budget
/// smaller than already committed metadata. Native resources still retire on shutdown.
struct StorageLimits final
{
    /// Maximum live/pending/retained records of each general raster/compute resource kind.
    ludus::foundation::uint32 ResourcesPerKind = 64;
    /// Maximum live/pending/retained records of each optional RT0 resource kind.
    /// Native metadata uses stable budgeted pages, independent of raster resource counts.
    ludus::foundation::uint32 RayResourcesPerKind = 64;
    /// Requested maximum static TLAS instances; RayCapabilities reports the backend-clamped limit.
    /// Instance inputs, opacity metadata, structures and scratch also require explicit byte budgets.
    ludus::foundation::uint32 RayInstances = 256;
    /// Maximum accepted direct/indirect packets in one frame.
    ludus::foundation::uint32 DrawsPerFrame = 1024;
    /// Maximum independently retained command batches.
    ludus::foundation::uint32 Batches = 4;
    /// Maximum packets per batch or graph, including culled packets.
    ludus::foundation::uint32 DrawsPerPlan = 256;
    /// Maximum independently owned graph plans.
    ludus::foundation::uint32 Graphs = 4;
    /// Maximum imported/transient logical resources per graph.
    ludus::foundation::uint32 ResourcesPerGraph = 16;
    /// Maximum authored passes and resource versions per graph.
    ludus::foundation::uint32 PassesPerGraph = 32;
    /// Maximum semantic dependency entries per graph.
    ludus::foundation::uint32 DependenciesPerGraph = 512;
    /// Maximum retained transient texture-pool entries.
    ludus::foundation::uint32 TransientTextures = 8;
    /// Maximum cached format/load/store/depth render-pass combinations on Vulkan.
    /// Other backends encode this state directly and do not consume these records.
    ludus::foundation::uint32 RasterPassStates = 192;
    /// Maximum simultaneous submission completion records on callback backends.
    ludus::foundation::uint32 Submissions = 64;
    /// R8 simultaneous requests per transfer direction, including cancelled in-flight slices; 1..4.
    ludus::foundation::uint32 TransferSlices = 4;
    /// R8 bytes per staging slice; four-byte aligned, 256..256 KiB. Separate from ResourceBytes.
    ludus::foundation::usize TransferBytes = ludus::foundation::usize{256} * 1024;
    /// Host storage budget in bytes: registry/backend metadata, plans, scratch and index copies.
    /// Includes retained page slack and growth overlap; excludes allocator overhead and driver allocations.
    ludus::foundation::usize HostBytes = ludus::foundation::usize{64} * 1024 * 1024;
    /// Logical buffer/texture byte budget, including pending/detached records and private depth.
    /// Excludes native padding, driver allocation metadata, facade targets and transfer-ring staging.
    ludus::foundation::uint64 ResourceBytes = ludus::foundation::uint64{512} * 1024 * 1024;
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
    /// Enabled attribute locations, bounded at sixteen; L4 requires all sixteen.
    ludus::foundation::uint32 MaxVertexAttributes = 0;
    /// RGBA16F attachment supports sampling, linear filtering and blending.
    /// False selects the authored linear RGBA8 fallback; no automatic backend switch.
    bool HdrColor = false;
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
/// Storage remains unimplemented; R8 enables bounded RGBA8Unorm copy uses.
struct TextureUses final
{
    /// Original-format sampled views in the enabled dimensions.
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
    /// Maximum enabled sampled array layers; one means D2 only.
    ludus::foundation::uint32 MaxArrayLayers = 0;
    /// Maximum enabled cubemap face extent; zero means unsupported.
    ludus::foundation::uint32 MaxCubeDimension = 0;
    /// Maximum enabled volume extent along every axis; zero means unsupported.
    ludus::foundation::uint32 MaxDimension3D = 0;
    /// Selected mip/layer color views are implemented for this format.
    bool PartialViews = false;
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
    /// Maximum enabled texture array layers across supported formats.
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
    /// Immutable configured resource/plan/staging maxima and budgets; all zero outside Ready.
    StorageLimits Storage
    {
        .ResourcesPerKind = 0,
        .RayResourcesPerKind = 0,
        .RayInstances = 0,
        .DrawsPerFrame = 0,
        .Batches = 0,
        .DrawsPerPlan = 0,
        .Graphs = 0,
        .ResourcesPerGraph = 0,
        .PassesPerGraph = 0,
        .DependenciesPerGraph = 0,
        .TransientTextures = 0,
        .RasterPassStates = 0,
        .Submissions = 0,
        .TransferSlices = 0,
        .TransferBytes = 0,
        .HostBytes = 0,
        .ResourceBytes = 0,
    };
    /// Seven format entries indexed by RasterFormat, including unsupported uses.
    FormatCapabilities Formats[7]{};
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
    /// Required sampled allocation dimension; attachment requests require D2.
    TextureDimension Dimension = TextureDimension::D2;
    /// Minimum array layer count or volume depth; zero adds no count constraint.
    ludus::foundation::uint32 MinDepthOrLayers = 0;
    /// Require implemented sampled mip/layer subrange views of this format.
    bool PartialViews = false;
};
/// Copied bounded required/preferred profile constraints. Unused array entries are ignored.
/// Required constraints reject before Ready; preferred constraints only produce diagnostics.
struct DeviceProfileRequirements final
{
    /// Additional raster limits.
    RasterRequirements Raster;
    /// Additional compute limits.
    ComputeRequirements Compute;
    /// At most seven unique known format requests.
    FormatRequirement Formats[7]{};
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
    /// Requested original-format sampled subrange views are disabled.
    PartialViews,
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
