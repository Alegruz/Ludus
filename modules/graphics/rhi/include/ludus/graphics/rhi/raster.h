#pragma once

#include <ludus/foundation/base/types.h>

#include <ludus/graphics/rhi/device.h>
#include <ludus/graphics/rhi/render.h>

#include <span>
#include <string_view>

namespace ludus::graphics::rhi
{
namespace internal
{
/// Private access to portable resource identities; not an application API.
struct RasterAccess;
} // namespace internal
/// Opaque buffer identity, scoped to a device, slot and non-wrapping generation.
/// Copies do not transfer ownership. Destroy invalidates resolution immediately;
/// binding/pipeline and submitted uses keep the underlying record alive.
struct BufferHandle final
{
private:
    ludus::foundation::uint64 Owner = 0;
    ludus::foundation::uint64 Generation = 0;
    ludus::foundation::uint32 Slot = 0;
    /// Grants the private registry access to assign and validate this incarnation.
    friend struct internal::RasterAccess;
};
/// Opaque two-dimensional texture identity, scoped to a device, slot and non-wrapping generation.
/// Copies do not transfer ownership. Destroy invalidates resolution immediately;
/// binding/pipeline and submitted uses keep the underlying record alive.
struct TextureHandle final
{
private:
    ludus::foundation::uint64 Owner = 0;
    ludus::foundation::uint64 Generation = 0;
    ludus::foundation::uint32 Slot = 0;
    /// Grants the private registry access to assign and validate this incarnation.
    friend struct internal::RasterAccess;
};
/// Opaque complete sampled texture view identity, scoped to a device, slot and non-wrapping generation.
/// Copies do not transfer ownership. Destroy invalidates resolution immediately;
/// binding/pipeline and submitted uses keep the underlying record alive.
struct TextureViewHandle final
{
private:
    ludus::foundation::uint64 Owner = 0;
    ludus::foundation::uint64 Generation = 0;
    ludus::foundation::uint32 Slot = 0;
    /// Grants the private registry access to assign and validate this incarnation.
    friend struct internal::RasterAccess;
};
/// Opaque sampler identity, scoped to a device, slot and non-wrapping generation.
/// Copies do not transfer ownership. Destroy invalidates resolution immediately;
/// binding/pipeline and submitted uses keep the underlying record alive.
struct SamplerHandle final
{
private:
    ludus::foundation::uint64 Owner = 0;
    ludus::foundation::uint64 Generation = 0;
    ludus::foundation::uint32 Slot = 0;
    /// Grants the private registry access to assign and validate this incarnation.
    friend struct internal::RasterAccess;
};
/// Opaque reflected shader stage identity, scoped to a device, slot and non-wrapping generation.
/// Copies do not transfer ownership. Destroy invalidates resolution immediately;
/// binding/pipeline and submitted uses keep the underlying record alive.
struct RasterShaderHandle final
{
private:
    ludus::foundation::uint64 Owner = 0;
    ludus::foundation::uint64 Generation = 0;
    ludus::foundation::uint32 Slot = 0;
    /// Grants the private registry access to assign and validate this incarnation.
    friend struct internal::RasterAccess;
};
/// Opaque binding layout identity, scoped to a device, slot and non-wrapping generation.
/// Copies do not transfer ownership. Destroy invalidates resolution immediately;
/// binding/pipeline and submitted uses keep the underlying record alive.
struct BindingLayoutHandle final
{
private:
    ludus::foundation::uint64 Owner = 0;
    ludus::foundation::uint64 Generation = 0;
    ludus::foundation::uint32 Slot = 0;
    /// Grants the private registry access to assign and validate this incarnation.
    friend struct internal::RasterAccess;
};
/// Opaque immutable binding snapshot identity, scoped to a device, slot and non-wrapping generation.
/// Copies do not transfer ownership. Destroy invalidates resolution immediately;
/// binding/pipeline and submitted uses keep the underlying record alive.
struct BindingSetHandle final
{
private:
    ludus::foundation::uint64 Owner = 0;
    ludus::foundation::uint64 Generation = 0;
    ludus::foundation::uint32 Slot = 0;
    /// Grants the private registry access to assign and validate this incarnation.
    friend struct internal::RasterAccess;
};
/// Opaque immutable graphics pipeline identity, scoped to a device, slot and non-wrapping generation.
/// Copies do not transfer ownership. Destroy invalidates resolution immediately;
/// binding/pipeline and submitted uses keep the underlying record alive.
struct RasterPipelineHandle final
{
private:
    ludus::foundation::uint64 Owner = 0;
    ludus::foundation::uint64 Generation = 0;
    ludus::foundation::uint32 Slot = 0;
    /// Grants the private registry access to assign and validate this incarnation.
    friend struct internal::RasterAccess;
};
/// Explicit portable-resource result. Only Ready/Pending publish ownership.
enum class RasterStatus : ludus::foundation::uint8
{
    /// Operation completed, or an asynchronous resource is usable.
    Ready,
    /// Output owns a resource awaiting backend validation; poll GetStatus.
    Pending,
    /// Device startup or a dependency is pending; retry after it becomes ready.
    NotReady,
    /// Backend validation or creation failed; a published resource must be destroyed.
    Failed,
    /// Null, stale, foreign-owner or wrong-incarnation handle.
    InvalidHandle,
    /// Operation conflicts with an open frame or an already-owned output.
    InvalidState,
    /// Malformed description, range, packing or layout mismatch.
    InvalidDescription,
    /// Requested format or operation lies outside the enabled raster profile.
    Unsupported,
    /// All bounded records are live or retained; no reservation was published.
    CapacityExceeded,
    /// An owner or slot generation cannot advance without wrapping.
    IdentityExhausted,
    /// Native allocation failed; no output ownership was published.
    OutOfMemory,
    /// The device was lost; recreate resources in a new session.
    DeviceLost,
};
/// Buffer creation role, fixed for its lifetime. Storage roles permit graph compute writes.
enum class BufferRole : ludus::foundation::uint8
{
    /// Per-vertex or per-instance attributes.
    Vertex,
    /// Mesh-local unsigned 16-bit indices.
    Index16,
    /// Mesh-local unsigned 32-bit indices.
    Index32,
    /// Uniform bytes with independently verified shader packing.
    Uniform,
    /// Mutable compute storage, initialized by complete creation bytes.
    Storage,
    /// Mutable compute storage also consumed as vertex/instance records.
    StorageVertex,
    /// Mutable compute storage also consumed as one indexed indirect command.
    StorageIndirect,
};
/// Immutable buffer description; complete initial bytes are consumed before return.
struct BufferDescription final
{
    /// Legal use of the created buffer.
    BufferRole Role = BufferRole::Vertex;
    /// Allocation and initial-content length in bytes; nonzero and bounded by capabilities.
    ludus::foundation::usize Size = 0;
};
/// Supported color formats; all textures have one mip, one layer and one sample.
enum class RasterFormat : ludus::foundation::uint8
{
    /// Four unsigned normalized channels, interpreted as linear light.
    Rgba8Unorm,
    /// Four unsigned normalized channels; RGB decodes sRGB during sampling, alpha remains linear.
    Rgba8Srgb,
    /// Single unsigned normalized coverage/data channel; sampled only, never an attachment.
    R8Unorm,
};
/// Two-dimensional sampled texture or mutable color attachment description.
struct TextureDescription final
{
    /// Pixel width, nonzero and bounded by enabled limits.
    ludus::foundation::uint32 Width = 0;
    /// Pixel height, nonzero and bounded by enabled limits.
    ludus::foundation::uint32 Height = 0;
    /// Sampling conversion; no automatic upload conversion occurs.
    RasterFormat Format = RasterFormat::Rgba8Unorm;
    /// Allocate undefined color and private depth attachments instead of uploading bytes.
    /// Attachment textures remain sampleable; clear/store defines color contents.
    bool Attachment = false;
};
/// Borrowed complete R8 or RGBA8 upload, copied/consumed before CreateTexture returns.
struct TextureUpload final
{
    /// Rows in top-left image order, including optional padding between rows.
    std::span<const ludus::foundation::uint8> Bytes;
    /// Bytes between rows; multiple of four and at least Width times the format channel count.
    ludus::foundation::usize RowPitch = 0;
};
/// Portable minification/magnification filter; the one-mip profile has no mip interpolation.
enum class RasterFilter : ludus::foundation::uint8
{
    /// Select one texel.
    Nearest,
    /// Interpolate adjacent texels in linear light.
    Linear,
};
/// Portable address mode applied independently along the two texture axes.
enum class RasterAddress : ludus::foundation::uint8
{
    /// Clamp coordinates to edge texels.
    Clamp,
    /// Repeat the texture at integer coordinate boundaries.
    Repeat,
};
/// Immutable sampler state; anisotropy and comparison sampling are unsupported.
struct SamplerDescription final
{
    /// Magnification and minification filtering.
    RasterFilter Filter = RasterFilter::Nearest;
    /// Horizontal addressing.
    RasterAddress U = RasterAddress::Clamp;
    /// Vertical addressing.
    RasterAddress V = RasterAddress::Clamp;
};
/// Resource kind reflected at a binding in group/set zero.
enum class RasterBindingKind : ludus::foundation::uint8
{
    /// Read-only uniform buffer range.
    UniformBuffer,
    /// Non-multisampled, float-sampled two-dimensional texture.
    Texture2D,
    /// Non-comparison sampler.
    Sampler,
    /// Read-only compute storage buffer range.
    StorageRead,
    /// Read/write compute storage buffer range; shader write races remain kernel obligations.
    StorageReadWrite,
};
/// Shader stage visibility mask.
enum class RasterVisibility : ludus::foundation::uint8
{
    /// Visible to the vertex stage only.
    Vertex = 1,
    /// Visible to the fragment stage only.
    Fragment = 2,
    /// Visible to both raster stages.
    Both = 3,
    /// Compute stage only; no raster/storage writes in this profile.
    Compute = 4,
};
/// One immutable layout entry; binding numbers must be unique and less than eight.
struct RasterBinding final
{
    /// Group/set-zero binding number.
    ludus::foundation::uint32 Binding = 0;
    /// Resource type; resource arrays and storage textures are unsupported.
    RasterBindingKind Kind = RasterBindingKind::UniformBuffer;
    /// Stages allowed to use this entry.
    RasterVisibility Visibility = RasterVisibility::Both;
    /// Minimum occupied uniform bytes or storage element stride from target reflection;
    /// zero for textures/samplers. Storage ranges must contain at least one element.
    ludus::foundation::usize MinSize = 0;
};
/// Portable float vertex attribute width.
enum class RasterVertexFormat : ludus::foundation::uint8
{
    /// Two IEEE binary32 components.
    Float2,
    /// Three IEEE binary32 components.
    Float3,
    /// Four IEEE binary32 components.
    Float4,
};
/// One reflected vertex-stage input; system-value inputs are excluded.
struct RasterShaderInput final
{
    /// Vertex attribute location, below eight.
    ludus::foundation::uint32 Location = 0;
    /// Exact float component count expected by the selected target artifact.
    RasterVertexFormat Format = RasterVertexFormat::Float3;
};
/// Per-target interface metadata emitted by the offline shader tool.
/// Hand-authored artifacts must supply independently verified matching reflection.
struct RasterShaderDescription final
{
    /// Target artifact and explicit entry; UniformSize is unused by the raster API.
    ShaderDescription Artifact{};
    /// Reflected float vertex inputs; empty for fragment stages. Copied before return.
    std::span<const RasterShaderInput> Inputs;
    /// Binding requirements for this stage; copied before return.
    std::span<const RasterBinding> Bindings;
    /// GLSL ES uniform block names, indexed by binding number; empty for other kinds.
    std::string_view UniformBlocks[8]{};
    /// Combined GLSL ES sampled-uniform names, indexed by texture binding number.
    std::string_view TextureNames[8]{};
    /// Sampler binding paired with each GLSL ES texture; irrelevant for other targets.
    ludus::foundation::uint32 TextureSamplers[8]{};
    /// Independently reflected local dimensions for Compute; ignored for raster.
    /// Positive dimensions/product must fit the negotiated compute limits.
    ludus::foundation::uint32 WorkgroupSize[3]{1, 1, 1};
};
/// One immutable binding snapshot entry; exactly the resource matching Kind is used.
struct RasterBindingResource final
{
    /// Layout binding number.
    ludus::foundation::uint32 Binding = 0;
    /// Uniform buffer identity; ignored for texture/sampler entries.
    BufferHandle Buffer{};
    /// Uniform byte offset, aligned to the enabled range alignment.
    ludus::foundation::usize Offset = 0;
    /// Uniform byte range, nonzero and at least the layout's reflected minimum.
    ludus::foundation::usize Size = 0;
    /// Sampled complete view identity; ignored for other entries.
    TextureViewHandle Texture{};
    /// Sampler identity; ignored for other entries.
    SamplerHandle Sampler{};
};
/// One immutable vertex attribute; locations must be unique and below eight.
struct RasterVertexAttribute final
{
    /// Shader input location.
    ludus::foundation::uint32 Location = 0;
    /// Vertex stream index, zero or one.
    ludus::foundation::uint32 Stream = 0;
    /// Byte offset within a record, multiple of four.
    ludus::foundation::uint32 Offset = 0;
    /// Component count/type.
    RasterVertexFormat Format = RasterVertexFormat::Float3;
};
/// Immutable vertex-stream layout.
struct RasterVertexStream final
{
    /// Bytes between records, nonzero, multiple of four, at most 256.
    ludus::foundation::uint32 Stride = 0;
    /// False advances per vertex; true advances per instance with divisor one.
    bool PerInstance = false;
};
/// Color attachment compatibility baked into an immutable raster pipeline.
enum class RasterTarget : ludus::foundation::uint8
{
    /// Acquired session target's negotiated format.
    Surface,
    /// Linear RGBA8 offscreen color attachment.
    Rgba8Unorm,
    /// sRGB RGBA8 offscreen color attachment.
    Rgba8Srgb,
};
/// Canonical [0,1] depth comparison; reversal is explicit rather than inferred from a matrix.
enum class RasterDepthCompare : ludus::foundation::uint8
{
    /// Pass when incoming depth is strictly smaller (conventional depth).
    Less,
    /// Pass on smaller or equal depth.
    LessEqual,
    /// Pass when incoming depth is strictly larger (reverse-Z).
    Greater,
    /// Pass on larger or equal depth.
    GreaterEqual,
    /// Accept every depth value; DepthWrite still controls writes.
    Always,
};
/// Immutable triangle-list pipeline, independent of particular buffers or bindings.
/// Target selects the negotiated surface or an explicit offscreen color format,
/// with one sample and private depth. Dimensions are dynamic; format changes need
/// a compatible pipeline. Depth uses canonical [0,1] coordinates.
struct RasterPipelineDescription final
{
    /// Ready vertex stage retained by the pipeline.
    RasterShaderHandle Vertex{};
    /// Ready fragment stage retained by the pipeline.
    RasterShaderHandle Fragment{};
    /// Exact immutable binding-layout identity retained by the pipeline.
    BindingLayoutHandle Layout{};
    /// Up to two streams; copied before return.
    std::span<const RasterVertexStream> Streams;
    /// Up to eight attributes; copied before return.
    std::span<const RasterVertexAttribute> Attributes;
    /// Enable the explicit comparison and optional depth writes; false preserves depth.
    bool Depth = false;
    /// Enable premultiplied-alpha blending in the target's linear domain.
    bool Blend = false;
    /// Color format compatibility; dimensions are dynamic and not part of the key.
    RasterTarget Target = RasterTarget::Surface;
    /// When Depth is enabled, false tests depth without modifying it.
    bool DepthWrite = true;
    /// Comparison when Depth is enabled; defaults preserve R1 conventional depth.
    RasterDepthCompare DepthCompare = RasterDepthCompare::Less;
};
/// One vertex-stream slice, borrowed during DrawIndexed and retained through submission.
struct RasterVertexSlice final
{
    /// Vertex-role buffer identity.
    BufferHandle Buffer{};
    /// Byte offset, multiple of four.
    ludus::foundation::usize Offset = 0;
    /// Byte length of the accessible slice; no implicit access to the remaining buffer.
    ludus::foundation::usize Size = 0;
};
/// Complete indexed/instanced draw packet. First instance and base vertex are zero.
/// Validation precedes native calls; previous accepted draws are not rolled back
/// by a later rejected packet. Mesh-local indices must address VertexCount records.
struct RasterDraw final
{
    /// Ready immutable pipeline.
    RasterPipelineHandle Pipeline{};
    /// Ready snapshot with the exact pipeline layout.
    BindingSetHandle Bindings{};
    /// One slice for every pipeline stream, in stream order.
    std::span<const RasterVertexSlice> Vertices;
    /// Index16/Index32 buffer.
    BufferHandle Indices{};
    /// Byte offset into the index buffer, aligned to its element width.
    ludus::foundation::usize IndexOffset = 0;
    /// Number of indices, positive and a multiple of three.
    ludus::foundation::uint32 IndexCount = 0;
    /// Number of accessible vertex records; each index is checked at buffer creation/draw.
    ludus::foundation::uint32 VertexCount = 0;
    /// Number of instances, positive; instance streams need this many records.
    ludus::foundation::uint32 InstanceCount = 1;
    /// Optional StorageIndirect buffer. Non-null selects one indirect command;
    /// counts above are conservative bounds, not CPU copies of GPU arguments.
    BufferHandle Indirect{};
    /// Four-byte-aligned command offset, covering IndexedIndirectArguments.
    ludus::foundation::usize IndirectOffset = 0;
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
/// Query the enabled raster profile. Invalid owner preserves output; non-ready devices
/// return their state and zero the capability value. All operations are owner-thread only.
[[nodiscard]] RasterStatus GetRasterCapabilities(DeviceHandle, RasterCapabilities&) noexcept;
/// Create a buffer from exactly Size initial bytes; creation is forbidden in an open frame.
/// Failure preserves a null output. Index content is copied into bounded engine storage
/// for draw-range checks; vertex/uniform input is consumed by the backend before return.
[[nodiscard]] RasterStatus
CreateBuffer(DeviceHandle, const BufferDescription&, std::span<const ludus::foundation::uint8>, BufferHandle&) noexcept;
/// Create a sampled texture from complete RGBA8 rows, or an undefined attachment
/// with empty upload and zero pitch. Invalid description preserves output.
[[nodiscard]] RasterStatus
CreateTexture(DeviceHandle, const TextureDescription&, const TextureUpload&, TextureHandle&) noexcept;
/// Return whether the texture identity is the null surface selector; no ownership/state query.
[[nodiscard]] bool IsNull(TextureHandle) noexcept;
/// Copy the immutable description of an owned texture; owner-thread only, including open frames.
/// Invalid/unready handles preserve output; does not expose native objects or extend ownership.
[[nodiscard]] RasterStatus GetTextureDescription(DeviceHandle, TextureHandle, TextureDescription&) noexcept;
/// Create a complete, original-format view, retaining its texture. Partial sampled ranges,
/// format reinterpretation and separate depth views are unsupported.
[[nodiscard]] RasterStatus CreateTextureView(DeviceHandle, TextureHandle, TextureViewHandle&) noexcept;
/// Create immutable filter/address state; no engine allocations on a warmed resource path.
[[nodiscard]] RasterStatus CreateSampler(DeviceHandle, const SamplerDescription&, SamplerHandle&) noexcept;
/// Create a reflected stage. Pending owns its output; copied metadata survives caller storage.
[[nodiscard]] RasterStatus
CreateRasterShader(DeviceHandle, const RasterShaderDescription&, RasterShaderHandle&) noexcept;
/// Create an immutable group-zero layout from at most eight unique entries.
[[nodiscard]] RasterStatus
CreateBindingLayout(DeviceHandle, std::span<const RasterBinding>, BindingLayoutHandle&) noexcept;
/// Create an immutable snapshot containing exactly one resource for every layout entry.
/// It retains its layout and all resources even after public destruction.
[[nodiscard]] RasterStatus
CreateBindingSet(DeviceHandle, BindingLayoutHandle, std::span<const RasterBindingResource>, BindingSetHandle&) noexcept;
/// Create an immutable triangle pipeline; dependencies must be ready. Binding contents
/// are supplied independently at draw time. Failure leaves null output and rolls back reservation.
[[nodiscard]] RasterStatus
CreateRasterPipeline(DeviceHandle, const RasterPipelineDescription&, RasterPipelineHandle&) noexcept;
/// Encode an indexed/instanced packet into the explicit device's open frame.
/// Retain all referenced records until backend completion; drawing cannot mix with
/// DrawFullscreen in the same frame. Rejected packets issue no native draw.
/// Sampled textures must have defined contents and a committed/draft sampled use
/// covering the binding visibility; otherwise InvalidDescription/InvalidState.
[[nodiscard]] RasterStatus DrawIndexed(DeviceHandle, const RasterDraw&) noexcept;
/// Query asynchronous validation for an owned Buffer record; stale/foreign handles are rejected.
[[nodiscard]] RasterStatus GetStatus(DeviceHandle, BufferHandle) noexcept;
/// Logically destroy an owned Buffer outside frames, zeroing the supplied handle.
/// Retained dependencies/GPU uses delay physical release. Invalid handles preserve output.
[[nodiscard]] RasterStatus Destroy(DeviceHandle, BufferHandle&) noexcept;
/// Query asynchronous validation for an owned Texture record; stale/foreign handles are rejected.
[[nodiscard]] RasterStatus GetStatus(DeviceHandle, TextureHandle) noexcept;
/// Logically destroy an owned Texture outside frames, zeroing the supplied handle.
/// Retained dependencies/GPU uses delay physical release. Invalid handles preserve output.
[[nodiscard]] RasterStatus Destroy(DeviceHandle, TextureHandle&) noexcept;
/// Query asynchronous validation for an owned TextureView record; stale/foreign handles are rejected.
[[nodiscard]] RasterStatus GetStatus(DeviceHandle, TextureViewHandle) noexcept;
/// Logically destroy an owned TextureView outside frames, zeroing the supplied handle.
/// Retained dependencies/GPU uses delay physical release. Invalid handles preserve output.
[[nodiscard]] RasterStatus Destroy(DeviceHandle, TextureViewHandle&) noexcept;
/// Query asynchronous validation for an owned Sampler record; stale/foreign handles are rejected.
[[nodiscard]] RasterStatus GetStatus(DeviceHandle, SamplerHandle) noexcept;
/// Logically destroy an owned Sampler outside frames, zeroing the supplied handle.
/// Retained dependencies/GPU uses delay physical release. Invalid handles preserve output.
[[nodiscard]] RasterStatus Destroy(DeviceHandle, SamplerHandle&) noexcept;
/// Query asynchronous validation for an owned RasterShader record; stale/foreign handles are rejected.
[[nodiscard]] RasterStatus GetStatus(DeviceHandle, RasterShaderHandle) noexcept;
/// Logically destroy an owned RasterShader outside frames, zeroing the supplied handle.
/// Retained dependencies/GPU uses delay physical release. Invalid handles preserve output.
[[nodiscard]] RasterStatus Destroy(DeviceHandle, RasterShaderHandle&) noexcept;
/// Query asynchronous validation for an owned BindingLayout record; stale/foreign handles are rejected.
[[nodiscard]] RasterStatus GetStatus(DeviceHandle, BindingLayoutHandle) noexcept;
/// Logically destroy an owned BindingLayout outside frames, zeroing the supplied handle.
/// Retained dependencies/GPU uses delay physical release. Invalid handles preserve output.
[[nodiscard]] RasterStatus Destroy(DeviceHandle, BindingLayoutHandle&) noexcept;
/// Query asynchronous validation for an owned BindingSet record; stale/foreign handles are rejected.
[[nodiscard]] RasterStatus GetStatus(DeviceHandle, BindingSetHandle) noexcept;
/// Logically destroy an owned BindingSet outside frames, zeroing the supplied handle.
/// Retained dependencies/GPU uses delay physical release. Invalid handles preserve output.
[[nodiscard]] RasterStatus Destroy(DeviceHandle, BindingSetHandle&) noexcept;
/// Query asynchronous validation for an owned RasterPipeline record; stale/foreign handles are rejected.
[[nodiscard]] RasterStatus GetStatus(DeviceHandle, RasterPipelineHandle) noexcept;
/// Logically destroy an owned RasterPipeline outside frames, zeroing the supplied handle.
/// Retained dependencies/GPU uses delay physical release. Invalid handles preserve output.
[[nodiscard]] RasterStatus Destroy(DeviceHandle, RasterPipelineHandle&) noexcept;
} // namespace ludus::graphics::rhi
