#pragma once

#include <ludus/foundation/base/types.h>

#include <ludus/graphics/rhi/compute.h>

namespace ludus::graphics::rhi
{
namespace internal
{
/// Private access to ordered graph identities.
struct GraphAccess;
} // namespace internal
/// Full-attachment initialization at a raster pass boundary.
enum class RasterLoad : ludus::foundation::uint8
{
    /// Initialize the complete attachment with the specified clear value.
    Clear,
    /// Preserve defined contents; undefined contents are an error.
    Load,
    /// Previous contents are undefined; partial draws do not initialize the image.
    Discard,
};
/// Attachment validity after a raster pass.
enum class RasterStore : ludus::foundation::uint8
{
    /// Preserve initialized contents for later passes or executions.
    Store,
    /// Contents become undefined even when the pass cleared them.
    Discard,
};
/// Semantic whole-color-image use, translated privately to backend synchronization.
enum class RasterTextureUse : ludus::foundation::uint8
{
    /// Newly allocated storage has no prior use or defined contents.
    Undefined,
    /// Read in the vertex shader.
    SampledVertex,
    /// Read in the fragment shader.
    SampledFragment,
    /// Read in either raster shader stage.
    SampledBoth,
    /// Read/write as a color attachment.
    ColorAttachment,
};
/// One raster pass: one color attachment and its private, same-size depth image.
/// Null Color selects the acquired surface. One mip/layer/sample; no resolve,
/// stencil, storage, partial initialization or depth sampling in this profile.
struct RasterPassDescription final
{
    /// Owned attachment texture, or null for the acquired surface.
    TextureHandle Color{};
    /// Full-color initialization contract.
    RasterLoad ColorLoad = RasterLoad::Clear;
    /// Whether initialized color survives the pass.
    RasterStore ColorStore = RasterStore::Store;
    /// Linear clear RGBA values, each finite and in [0,1].
    ludus::foundation::float32 Clear[4]{0, 0, 0, 1};
    /// Depth initialization; clear always uses canonical depth one.
    RasterLoad DepthLoad = RasterLoad::Clear;
    /// Whether initialized depth survives for a later pass on this attachment.
    RasterStore DepthStore = RasterStore::Discard;
    /// Reject depth-writing pipelines; Load is required for read-only depth.
    bool DepthReadOnly = false;
};
/// Snapshot from the single RHI registry; describes accepted same-queue work,
/// including pending GPU uses, rather than presentation or CPU frame numbers.
struct RasterTextureState final
{
    /// Non-wrapping mutation revision; compare when importing into a graph.
    ludus::foundation::uint64 Revision = 0;
    /// Most recent accepted use; null means no submitted frame use.
    SubmissionToken Dependency{};
    /// Last semantic color use, including sampled bindings of direct draws.
    RasterTextureUse Use = RasterTextureUse::Undefined;
    /// Clear/upload followed by Store establishes full valid color contents.
    bool ColorDefined = false;
    /// Clear followed by Store establishes full valid private depth contents.
    bool DepthDefined = false;
};
/// Query an owned texture on the owner thread; failure preserves output.
[[nodiscard]] RasterStatus GetTextureState(DeviceHandle, TextureHandle, RasterTextureState&) noexcept;
/// Switch passes inside an acquired frame. Validates load/feedback/state before
/// native calls; ends the previous pass and starts this one. Direct draws must
/// match its attachment format. Rejected calls preserve the current pass.
[[nodiscard]] RasterStatus BeginRasterPass(DeviceHandle, const RasterPassDescription&) noexcept;
/// Opaque graph identity, retaining copied packets and resource leases. Owner-thread
/// only; copies do not transfer ownership. Execute consumes it once encoding begins.
struct OrderedGraph final
{
private:
    ludus::foundation::uint64 Owner = 0;
    ludus::foundation::uint64 Generation = 0;
    ludus::foundation::uint32 Slot = 0;
    /// Grants the private compiler access to the graph incarnation.
    friend struct internal::GraphAccess;
};
/// Resource version scoped to one graph incarnation; zero-initialized is invalid.
struct GraphVersion final
{
private:
    ludus::foundation::uint64 Owner = 0;
    ludus::foundation::uint64 Generation = 0;
    ludus::foundation::uint32 Graph = 0;
    ludus::foundation::uint32 Resource = 0;
    ludus::foundation::uint32 Version = 0;
    /// Grants the private compiler access to resource/version provenance.
    friend struct internal::GraphAccess;
};
/// Actual use of a declared buffer range or complete color texture.
enum class GraphAccessMode : ludus::foundation::uint8
{
    /// Vertex stream read; range must cover the packet's slice.
    Vertex,
    /// Index read; range must cover every indexed byte.
    Index,
    /// Reflected uniform range read.
    Uniform,
    /// Reflected texture binding read, including inactive extra bindings.
    Sampled,
    /// Attachment write to a new version; Clear or Discard.
    ColorWrite,
    /// Attachment Load of the preceding version and write of a new version.
    ColorReadWrite,
    /// Read-only compute storage range.
    StorageRead,
    /// Read/write compute storage range, writing the next in-place version.
    StorageReadWrite,
    /// Indexed indirect argument range.
    Indirect,
};
/// One declared use; whole textures and conservative whole-buffer hazards.
struct GraphUse final
{
    /// Read version, or newly reserved output version for attachment/storage writes.
    GraphVersion Resource{};
    /// Required resource role and access.
    GraphAccessMode Access = GraphAccessMode::Sampled;
    /// Buffer start in bytes; zero for textures.
    ludus::foundation::usize Offset = 0;
    /// Nonzero buffer length; zero for textures.
    ludus::foundation::usize Size = 0;
    /// Required stages for uniform/sample/storage uses; storage requires Compute.
    /// Vertex/index/indirect and attachment uses ignore it.
    RasterVisibility Visibility = RasterVisibility::Both;
};
/// Copied authored pass; no callbacks, lambdas or borrowed data survive AddGraphPass.
struct GraphPassDescription final
{
    /// Stable diagnostic name, 1..63 bytes; copied, not retained.
    const char* Name = nullptr;
    /// Optional source path, at most 127 bytes; copied for reports.
    const char* Source = nullptr;
    /// One-based source line, or zero when unavailable.
    ludus::foundation::uint32 Line = 0;
    /// Actual attachment/load/store state. Surface passes are present roots.
    RasterPassDescription Attachment;
    /// Complete use declarations; at most sixteen, unique physical resources.
    const GraphUse* Uses = nullptr;
    /// Number of Uses; pointer may be null only when zero.
    ludus::foundation::usize UseCount = 0;
    /// Complete immutable draws, copied/retained and preflighted before execution.
    const RasterDraw* Draws = nullptr;
    /// Number of Draws; graph total at most 256.
    ludus::foundation::usize DrawCount = 0;
    /// Observable work which must survive culling.
    bool SideEffect = false;
    /// Optional single compute dispatch. Non-null forbids raster attachments/draws;
    /// copied and retained before return. Null selects the existing raster pass.
    const ComputeDispatch* Dispatch = nullptr;
};
/// Resource roots observable after this execution.
enum class GraphRoot : ludus::foundation::uint8
{
    /// An external consumer will use this version.
    Export,
    /// Future-frame history write, even if unused by this frame.
    History,
    /// Readback consumer; scheduling/copy-out remains the R2 service's job.
    Readback,
};
/// Semantic dependency reason, independent of backend synchronization enums.
enum class GraphHazard : ludus::foundation::uint8
{
    /// Earlier writer must make data visible to this reader.
    ReadAfterWrite,
    /// Earlier reader must finish before this in-place overwrite.
    WriteAfterRead,
    /// Ordered in-place writes, even when contents are discarded.
    WriteAfterWrite,
};
/// One bounded report entry; pass indices are zero-based authored positions.
struct GraphDependency final
{
    /// Producer/reader pass, or 32 for the external import contract.
    ludus::foundation::uint32 Before = 32;
    /// Dependent pass, or 32 for the final external use.
    ludus::foundation::uint32 After = 32;
    /// Zero-based logical resource index.
    ludus::foundation::uint32 Resource = 0;
    /// RAW, WAR or WAW reason.
    GraphHazard Hazard = GraphHazard::ReadAfterWrite;
    /// Earlier semantic access.
    GraphAccessMode Source = GraphAccessMode::Sampled;
    /// Later semantic access.
    GraphAccessMode Destination = GraphAccessMode::Sampled;
};
/// Deterministic report copied to caller storage; no heap allocations or strings.
struct GraphReport final
{
    /// Authored passes admitted to this graph (maximum 32).
    ludus::foundation::uint32 PassCount = 0;
    /// Passes retained by roots, or all passes in reference mode.
    ludus::foundation::uint32 LivePasses = 0;
    /// Number of semantic dependencies (maximum 512).
    ludus::foundation::uint32 DependencyCount = 0;
    /// Failing pass/resource, or 32/16 when not applicable.
    ludus::foundation::uint32 ErrorPass = 32;
    /// Failing resource or 16 when not applicable.
    ludus::foundation::uint32 ErrorResource = 16;
    /// Conservative whole-buffer tracking is always used in this initial profile.
    bool WholeBufferHazards = true;
    /// Authored pass diagnostic names.
    char Names[32][64]{};
    /// Copied setup source locations.
    char Sources[32][128]{};
    /// Copied setup line numbers.
    ludus::foundation::uint32 Lines[32]{};
    /// True for passes removed by root analysis.
    bool Culled[32]{};
    /// First live use for each resource, or 32 if unused.
    ludus::foundation::uint32 FirstUse[16]{};
    /// Last live use for each resource, or 32 if unused.
    ludus::foundation::uint32 LastUse[16]{};
    /// Semantic dependencies in stable pass/resource/hazard order.
    GraphDependency Dependencies[512]{};
};
/// Reserve one of four bounded graphs; null output required, failure preserves it.
[[nodiscard]] RasterStatus CreateOrderedGraph(DeviceHandle, OrderedGraph&) noexcept;
/// Import current buffer contents and freeze their accepted revision, retaining the
/// physical record. Output version starts at zero. No duplicate physical imports;
/// at most sixteen graph resources. Compute writes invalidate earlier frozen plans.
[[nodiscard]] RasterStatus ImportGraphBuffer(DeviceHandle, OrderedGraph, BufferHandle, GraphVersion&) noexcept;
/// Import a persistent texture using an exact registry snapshot. Stale snapshots
/// reject; final use must be a sampled stage or ColorAttachment. Same-queue pending
/// dependencies need no host wait. Pool textures cannot be imported by another graph.
[[nodiscard]] RasterStatus ImportGraphTexture(DeviceHandle,
                                              OrderedGraph,
                                              TextureHandle,
                                              const RasterTextureState&,
                                              RasterTextureUse finalUse,
                                              GraphVersion&) noexcept;
/// Acquire a matching complete attachment object from an eight-object bounded pool.
/// Reuse requires no other leases and actual GPU completion. Contents begin undefined.
/// Returned texture is borrowed from the graph/pool: Destroy rejects it; create views
/// and binding sets normally and destroy those owners to permit later pooling. A stale
/// borrowed texture cannot be used once its graph is discarded/consumed. No heap aliasing.
/// Allocation occurs during setup, before compilation or frame execution; Pending owns
/// both outputs and can be polled using GetStatus on the texture before setup continues.
[[nodiscard]] RasterStatus
CreateGraphTexture(DeviceHandle, OrderedGraph, const TextureDescription&, GraphVersion&, TextureHandle&) noexcept;
/// Reserve the next attachment or storage-buffer version without new physical storage.
/// May reserve before its producer is added; compilation verifies authored ordering.
[[nodiscard]] RasterStatus NextGraphVersion(DeviceHandle, OrderedGraph, GraphVersion, GraphVersion&) noexcept;
/// Copy one ordered pass and retain its validated packets. Failure changes no graph
/// state. Uses/declarations are verified by CompileOrderedGraph before any GPU work.
[[nodiscard]] RasterStatus AddGraphPass(DeviceHandle, OrderedGraph, const GraphPassDescription&) noexcept;
/// Mark a version as externally observable. Transient pool resources cannot escape;
/// History/Export require persistent textures or buffers. Compilation rejects undefined roots.
[[nodiscard]] RasterStatus AddGraphRoot(DeviceHandle, OrderedGraph, GraphVersion, GraphRoot) noexcept;
/// Validate versions, actual packet uses, initialization, capacities and roots, then
/// report dependencies/lifetimes. Reference mode keeps all otherwise legal passes.
/// Failure preserves setup for repair/discard and writes its diagnostic report. Success
/// freezes setup; no execution occurs. Whole resources remain physically distinct.
[[nodiscard]] RasterStatus
CompileOrderedGraph(DeviceHandle, OrderedGraph, GraphReport&, bool reference = false) noexcept;
/// Preflight frozen imports/packets and backend pass objects, acquire a frame, encode
/// authored live order and submit once. Preflight/target skip preserves graph/token for
/// retry; once encoding begins graph is consumed even on failure. Partial backend work
/// faults the session. Success publishes GPU completion and commits the existing ledger.
[[nodiscard]] RasterStatus ExecuteOrderedGraph(DeviceHandle, SurfaceHandle, OrderedGraph&, SubmissionToken&) noexcept;
/// Drop all graph packet/resource leases without GPU work; clears handle on success.
/// Invalid handle or open frame preserves it. Pool reuse still waits for completion.
[[nodiscard]] RasterStatus DiscardOrderedGraph(DeviceHandle, OrderedGraph&) noexcept;
} // namespace ludus::graphics::rhi
