#pragma once

#include <ludus/foundation/base/types.h>

#include <ludus/foundation/base/pointer.hpp>
#include <ludus/foundation/math/matrix.hpp>
#include <ludus/foundation/math/transform.hpp>
#include <ludus/foundation/math/vector.hpp>
#include <ludus/graphics/rhi/device.h>
#include <ludus/graphics/rhi/graph.h>
#include <ludus/graphics/rhi/lifetime.h>
#include <ludus/graphics/rhi/raster.h>

namespace ludus::graphics::renderer
{
namespace internal
{
/// Private access to renderer logical identities.
struct Access;
} // namespace internal
/// Renderer-owned mesh incarnation; copies do not transfer ownership.
struct Mesh final
{
private:
    ludus::foundation::uint64 Owner = 0, Generation = 0;
    ludus::foundation::uint32 Slot = 0;
    /// Grants the private renderer access to assign and validate this incarnation.
    friend struct internal::Access;
};
/// Immutable copied scene incarnation; Release detaches public ownership.
struct Snapshot final
{
private:
    ludus::foundation::uint64 Owner = 0, Generation = 0;
    ludus::foundation::uint32 Slot = 0;
    /// Grants the private renderer access to assign and validate this incarnation.
    friend struct internal::Access;
};
/// Immutable prepared view retaining its snapshot and static instance buffer.
struct PreparedView final
{
private:
    ludus::foundation::uint64 Owner = 0, Generation = 0;
    ludus::foundation::uint32 Slot = 0;
    /// Grants the private renderer access to assign and validate this incarnation.
    friend struct internal::Access;
};
/// Forward declaration of the shared logical/physical mapping contract in views.hpp.
struct ViewMapping;
/// Immutable sampled target and presentation geometry retained through GPU completion.
struct Presentation final
{
private:
    ludus::foundation::uint64 Owner = 0, Generation = 0;
    ludus::foundation::uint32 Slot = 0;
    /// Grants private access to the renderer incarnation.
    friend struct internal::Access;
};
/// Explicit pairing of projection matrices, clear values and comparisons.
enum class DepthConvention : ludus::foundation::uint8
{
    /// R1 compatibility: near zero, far one, clear one, strict Less.
    Conventional,
    /// Near one, far zero, clear zero, strict Greater.
    ReverseZ,
};
/// Opaque scene geometry precedes painter-ordered overlays.
enum class Layer : ludus::foundation::uint8
{
    /// Depth-tested, depth-writing, alpha-one geometry.
    Opaque,
    /// Depth-disabled, premultiplied-alpha geometry in snapshot input order.
    Overlay,
};
/// Flat material in linear RGB; default magenta identifies missing material authoring.
struct FlatMaterial final
{
    /// Straight RGBA in [0,1]; opacity must be one for Opaque. Renderer premultiplies Overlay RGB.
    ludus::foundation::math::Vector4 Color{1, 0, 1, 1};
};
/// Borrowed static position-only triangle-list geometry, copied by CreateMesh.
struct MeshDescription final
{
    /// Finite mesh-local XYZ positions, tightly packed Vector3 records.
    const ludus::foundation::math::Vector3* Positions = nullptr;
    /// Number of positions, positive and representable by uint32.
    ludus::foundation::usize VertexCount = 0;
    /// Mesh-local uint32 indices; every value must be below VertexCount.
    const ludus::foundation::uint32* Indices = nullptr;
    /// Positive multiple of three, representable by uint32.
    ludus::foundation::usize IndexCount = 0;
};
/// Ordinary scene values; renderer has no ECS, gameplay camera or UI dependency.
struct SceneItem final
{
    /// Ready owned mesh at snapshot creation; accepted snapshots retain its incarnation.
    Mesh Geometry{};
    /// Local-to-world for Opaque; local-to-canonical-clip for Overlay (view matrix ignored).
    ludus::foundation::math::Affine3 Transform{};
    /// Copied flat color; default is the error material.
    FlatMaterial Material{};
    /// Nonzero unique application source identity within this snapshot, for diagnostics.
    ludus::foundation::uint64 SourceId = 0;
    /// Pass and ordering semantics.
    Layer Pass = Layer::Opaque;
};
/// Finite canonical clip transform: XY [-1,1], Z [0,1], Y up.
struct ViewDescription final
{
    /// Finite world-to-clip matrix matching the explicit Depth convention.
    ludus::foundation::math::Matrix4 WorldToClip = ludus::foundation::math::Matrix4::Identity();
    /// Finite nonnegative world-unit margin for conservative opaque AABB culling.
    ludus::foundation::float32 CullMargin = 0;
    /// Keep source order and one draw per object, with visibility disabled, for image comparison.
    bool Reference = false;
    /// Schedule a bounded R2 upload version instead of immediate immutable creation.
    /// PrepareView then returns Pending; poll GetStatus between frames before drawing.
    /// Caller snapshots/arrays may be released immediately; accepted GPU versions never change.
    bool ScheduledUpload = false;
    /// Defaults preserve existing L0 projection behavior; reverse-Z requires InitializeViews.
    DepthConvention Depth = DepthConvention::Conventional;
    /// Select five-plane culling for an infinite reverse-Z perspective projection.
    bool InfiniteFar = false;
    /// Canonical overlay crop transform, applied independently of the world camera.
    ludus::foundation::math::Matrix4 OverlayToClip = ludus::foundation::math::Matrix4::Identity();
};
/// Bounded packet report, copied to caller storage; source IDs follow actual draw/instance order.
struct ViewReport final
{
    /// Number of submitted snapshot objects.
    ludus::foundation::uint32 Submitted = 0;
    /// Retained visible objects, including overlays.
    ludus::foundation::uint32 Visible = 0;
    /// Objects conservatively rejected by the CPU clip/frustum tests.
    ludus::foundation::uint32 Culled = 0;
    /// Indexed draws after adjacent compatible instancing.
    ludus::foundation::uint32 Draws = 0;
    /// Actual per-instance source order; first Visible entries are meaningful.
    ludus::foundation::uint64 SourceIds[256]{};
};
/// Fixed logical budgets; RHI resources shared with other consumers may exhaust earlier.
struct Limits final
{
    /// Six resident/retained meshes (two RHI buffers each).
    ludus::foundation::uint32 Meshes = 6;
    /// Four resident/retained snapshots.
    ludus::foundation::uint32 Snapshots = 4;
    /// Four prepared views (one RHI instance buffer each).
    ludus::foundation::uint32 Views = 4;
    /// At most 256 scene objects and draw packets per snapshot/view.
    ludus::foundation::uint32 Objects = 256;
    /// Four retained target presentations, each consuming one RHI vertex buffer and binding set.
    ludus::foundation::uint32 Presentations = 4;
};
/// Portable static flat/unlit renderer, one owner thread per device.
/// Setup copies data and may allocate/create GPU resources. Repeated Submit creates
/// no renderer resources or heap allocations; RHI retains accepted work through completion.
/// Setup/release and destruction require a closed RHI frame; DrawView/DrawPresentation
/// require an acquired open frame. Reset before destroying the device.
/// See the portable scene submission section in `docs/architecture/renderer-systems.md` for the canonical guide.
class Renderer final
{
public:
    /// Construct an uninitialized renderer; no allocation or global registration.
    Renderer() noexcept;
    /// Release renderer ownership; submitted GPU uses retain their own RHI leases.
    ~Renderer();
    /// Renderer identity and owner thread cannot be copied.
    Renderer(const Renderer&) = delete;
    /// Renderer identity and owner thread cannot be assigned.
    Renderer& operator=(const Renderer&) = delete;
    /// Initialize on a ready portable-raster device using the shipped renderer_flat.slang program.
    /// Ready/Pending publishes renderer ownership; failures leave it uninitialized.
    /// Shader inputs must match six shipped locations and have no resource bindings.
    /// @param device Borrowed ready device, which must outlive this renderer.
    /// @param vertex Cooked vertex description from the installed shader helper.
    /// @param fragment Cooked fragment description from the same flat program.
    [[nodiscard]] rhi::RasterStatus Initialize(rhi::DeviceHandle device,
                                               const rhi::RasterShaderDescription& vertex,
                                               const rhi::RasterShaderDescription& fragment) noexcept;
    /// Advance asynchronous shader/layout/pipeline setup without blocking. Ready permits scene setup.
    /// Pending/failure retains ownership until Reset; no automatic backend switch.
    [[nodiscard]] rhi::RasterStatus Poll() noexcept;
    /// Enable L1 surface/offscreen reverse-Z and target composition after Initialize is Ready.
    /// Uses the shipped renderer_composite.slang stages; Ready/Pending publishes setup ownership.
    /// Failure preserves the usable L0 profile; failed published setup requires Reset.
    [[nodiscard]] rhi::RasterStatus InitializeViews(const rhi::RasterShaderDescription& vertex,
                                                    const rhi::RasterShaderDescription& fragment) noexcept;
    /// Advance L1 resource/pipeline creation on later owner turns; no GPU wait or backend switch.
    [[nodiscard]] rhi::RasterStatus PollViews() noexcept;
    /// Release all public identities/resources; accepted CPU/GPU uses retire through RHI.
    /// @pre Owner thread, no open frame; invalidates every previously issued logical handle.
    void Reset() noexcept;
    /// Return fixed logical budgets (not a promise of unoccupied RHI capacity).
    [[nodiscard]] static Limits GetLimits() noexcept;
    /// Copy geometry and derive exact mesh-local bounds. Null output required.
    /// Ready/Pending publishes mesh ownership; rejected input preserves output.
    [[nodiscard]] rhi::RasterStatus CreateMesh(const MeshDescription& description, Mesh& output) noexcept;
    /// Poll owned mesh creation; stale/foreign or detached public identities reject.
    [[nodiscard]] rhi::RasterStatus GetStatus(Mesh handle) const noexcept;
    /// Detach mesh ownership and clear handle. Existing snapshots/views retain the old geometry.
    [[nodiscard]] rhi::RasterStatus Release(Mesh& handle) noexcept;
    /// Copy all items and retain mesh incarnations; accepts zero count with null items.
    /// Invalid IDs, duplicate source IDs, nonfinite transforms/colors, capacity or unready meshes
    /// reject atomically, preserving output and all existing scenes. Null output required.
    [[nodiscard]] rhi::RasterStatus
    CreateSnapshot(const SceneItem* items, ludus::foundation::usize count, Snapshot& output) noexcept;
    /// Atomically replace an owned snapshot with a complete copied dynamic scene.
    /// Earlier views retain exact snapshot/mesh versions. Admission failure preserves
    /// current; requires one free snapshot slot, between frames.
    [[nodiscard]] rhi::RasterStatus
    ReplaceSnapshot(const SceneItem* items, ludus::foundation::usize count, Snapshot& current) noexcept;
    /// Detach snapshot ownership and clear handle; existing prepared views remain valid.
    [[nodiscard]] rhi::RasterStatus Release(Snapshot& handle) noexcept;
    /// Cull, safely order and instance a copied view during setup, retaining the snapshot.
    /// Opaque mesh sorting is confined to mutually disjoint projected-bound runs; overlapping
    /// objects and overlays preserve source order. Reference disables sorting/culling/instancing.
    /// Ready/Pending publishes view ownership and report; failures preserve both outputs.
    /// @param handle Owned snapshot incarnation.
    /// @param description Copied view transform and reference policy.
    /// @param output Null prepared view receiving ownership; Pending requires GetStatus before Submit.
    /// @param report Copied visibility/draw/source diagnostics, unchanged on rejection.
    [[nodiscard]] rhi::RasterStatus
    PrepareView(Snapshot handle, const ViewDescription& description, PreparedView& output, ViewReport& report) noexcept;
    /// Poll the prepared immutable instance buffer without blocking.
    [[nodiscard]] rhi::RasterStatus GetStatus(PreparedView handle) const noexcept;
    /// Record/submit conventional-depth packets with R1 clear/depth behavior.
    /// Reverse-Z views return InvalidDescription; use DrawView in an acquired shared frame.
    /// @param handle Ready view. It remains reusable after successful submission.
    /// @param surface Borrowed surface owned by the initialized device.
    /// @param completion Null completion receiving accepted GPU work; may be published on partial failure.
    /// NotReady preserves a finished batch for retry. Other preflight failures drop that batch.
    /// Returned failure is independent of a possible non-null completion (R2 partial-work contract).
    [[nodiscard]] rhi::RasterStatus
    Submit(PreparedView handle, rhi::SurfaceHandle surface, rhi::SubmissionToken& completion) noexcept;
    /// Draw a ready view in an acquired frame using explicit attachment/load/store/rectangles.
    /// L1 must be Ready. Null pass.Color selects the acquired surface; offscreen targets must
    /// be linear RGBA8 attachments. View depth chooses Less/Greater and the matching clear.
    /// Clear is whole-attachment regardless of viewport/scissor; use offscreen composition
    /// for independently cleared neighboring views. Accepted packets retain resources until
    /// the caller ends the frame with R2 EndFrame. Later rejection does not undo earlier work.
    [[nodiscard]] rhi::RasterStatus DrawView(PreparedView handle,
                                             const rhi::RasterPassDescription& description) noexcept;
    /// Copy presentation geometry/mapping and retain the sampled linear RGBA8 attachment.
    /// Four presentation slots, one vertex buffer and binding snapshot each; shared RHI
    /// budgets may exhaust earlier. Ready/Pending publishes output; failure preserves it.
    /// Source must already be Ready, but may have undefined contents until a later draw.
    /// Its pixel aspect must equal the mapping's logical aspect; render resolution may differ.
    /// @param source Owned RHI attachment; may be destroyed after accepted preparation.
    /// @param mapping Resolved output mapping; recreated explicitly after drawable resize.
    /// @param output Null presentation receiving ownership.
    [[nodiscard]] rhi::RasterStatus
    PreparePresentation(rhi::TextureHandle source, const ViewMapping& mapping, Presentation& output) noexcept;
    /// Advance asynchronous presentation geometry/view/binding creation between frames;
    /// may create its binding snapshot once dependencies become Ready. Failure still requires Release.
    [[nodiscard]] rhi::RasterStatus GetStatus(Presentation handle) noexcept;
    /// Composite into the acquired surface, preserving its defined color outside the mapped
    /// viewport/scissor. Undefined sampled color rejects; resize mismatch returns NotReady
    /// before changing the current pass. Mapping uses the actual acquired extent.
    /// Caller ends the shared frame with R2 EndFrame; no renderer allocations during drawing.
    [[nodiscard]] rhi::RasterStatus DrawPresentation(Presentation handle) noexcept;
    /// Detach presentation CPU ownership; sampled texture/geometry retire after accepted GPU uses.
    [[nodiscard]] rhi::RasterStatus Release(Presentation& handle) noexcept;
    /// Drop a prepared view and any retry batch; clear handle. GPU retirement remains in RHI.
    [[nodiscard]] rhi::RasterStatus Release(PreparedView& handle) noexcept;

private:
    struct State;
    ludus::foundation::core::UniquePtr<State> mState;
};
} // namespace ludus::graphics::renderer
