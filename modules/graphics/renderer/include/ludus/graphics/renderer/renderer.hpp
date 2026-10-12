#pragma once

#include <ludus/foundation/base/types.h>

#include <ludus/foundation/base/pointer.hpp>
#include <ludus/foundation/math/matrix.hpp>
#include <ludus/foundation/math/transform.hpp>
#include <ludus/foundation/math/vector.hpp>
#include <ludus/graphics/renderer/lighting.hpp>
#include <ludus/graphics/renderer/materials.hpp>
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
/// Borrowed static position/optional-UV triangle-list geometry, copied by CreateMesh.
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
    /// Optional finite top-left UVs, exactly VertexCount records; null supplies zero UV.
    /// Copied into the shared position/UV mesh stream during creation.
    const ludus::foundation::math::Vector2* Texcoords = nullptr;
    /// Optional finite nonzero mesh-local normals, normalized while copying. Lit snapshots require them.
    const ludus::foundation::math::Vector3* Normals = nullptr;
    /// Optional finite bake-chart UVs, exactly VertexCount records; null uses Texcoords.
    const ludus::foundation::math::Vector2* BakeTexcoords = nullptr;
};
/// Ordinary scene values; renderer has no ECS, gameplay camera or UI dependency.
struct SceneItem final
{
    /// Ready owned mesh at snapshot creation; accepted snapshots retain its incarnation.
    Mesh Geometry{};
    /// Optional coarse mesh incarnation, retained with Geometry. Opaque camera views only.
    /// Both meshes must use compatible material UVs/normals; shadow views retain full detail.
    Mesh LodGeometry{};
    /// Select coarse detail below this projected bounding-box diameter in pixels, positive when authored.
    ludus::foundation::float32 LodPixels = 0;
    /// Caller-owned previous coarse selection for hysteresis; copied with this snapshot.
    bool PreviousLod = false;
    /// Local-to-world for Opaque; local-to-canonical-clip for Overlay (view matrix ignored).
    ludus::foundation::math::Affine3 Transform{};
    /// Copied flat color; default is the error material.
    FlatMaterial Material{};
    /// Nonzero unique application source identity within this snapshot, for diagnostics.
    ludus::foundation::uint64 SourceId = 0;
    /// Pass and ordering semantics.
    Layer Pass = Layer::Opaque;
    /// Optional ready material incarnation replacing FlatMaterial entirely. Null keeps
    /// the L0 flat/error material. Snapshot retains its exact texture/program version.
    MaterialVersion Surface{};
    /// Exact bake scene revision; zero declares no valid baked chart.
    ludus::foundation::uint64 BakeSceneRevision = 0;
    /// Exact bake dependency revision; mismatch selects environment and dynamic direct lighting.
    ludus::foundation::uint64 BakeDependencyRevision = 0;
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
    /// L5 diagnostic: evaluate all admitted lights while retaining ordinary culling/batching.
    bool DirectLighting = false;
    /// Opt-in screen-size LOD; Reference bypasses LOD, ShadowCasters always uses full geometry.
    bool EnableLod = false;
    /// Physical viewport dimensions used only for LOD; both positive when EnableLod is true.
    ludus::foundation::uint32 LodWidth = 0;
    /// Physical viewport height for LOD, positive when EnableLod is true.
    ludus::foundation::uint32 LodHeight = 0;
    /// Fractional transition band [0,0.5]; previous selection comes from SceneItem::PreviousLod.
    ludus::foundation::float32 LodHysteresis = 0.1F;
    /// Schedule a bounded R2 upload version instead of immediate immutable creation.
    /// PrepareView then returns Pending; poll GetStatus between frames before drawing.
    /// Caller snapshots/arrays may be released immediately; accepted GPU versions never change.
    bool ScheduledUpload = false;
    /// Defaults preserve existing L0 projection behavior; reverse-Z requires InitializeViews.
    DepthConvention Depth = DepthConvention::Conventional;
    /// Select five-plane culling for an infinite reverse-Z perspective projection.
    bool InfiniteFar = false;
    /// Finite world-space camera position in meters, [-1000000,1000000]; required for lit shading.
    ludus::foundation::math::Vector3 CameraPosition{0, 0, 1};
    /// Prepare an opaque/masked caster view; skips overlays and disables camera visibility shortcuts.
    /// ShadowAtlas preparation sets this; ordinary callers should keep false.
    bool ShadowCasters = false;
    /// Use constant CameraForward for orthographic specular evaluation; perspective uses CameraPosition.
    bool OrthographicCamera = false;
    /// Nonzero finite world direction from camera into scene, normalized during preparation.
    ludus::foundation::math::Vector3 CameraForward{0, 0, -1};
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
    /// Visible lit instances using environment because an authored bake key/transform mismatched.
    ludus::foundation::uint32 BakeFallbacks = 0;
    /// Visible lit instances using unshadowed direct light after conservative caster invalidation.
    ludus::foundation::uint32 ShadowFallbacks = 0;
    /// Number of conservative 16-object groups rejected before individual AABB tests.
    ludus::foundation::uint32 CulledGroups = 0;
    /// Number of individual AABB tests; fully inside/rejected groups avoid them.
    ludus::foundation::uint32 BoundsTests = 0;
    /// Number of visible objects selecting coarse detail.
    ludus::foundation::uint32 LodObjects = 0;
    /// Coarse selections in the same order as SourceIds; feed back by source identity for hysteresis.
    bool SelectedLods[256]{};
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
    /// Eight prepared views, including up to four shadow light views.
    ludus::foundation::uint32 Views = 8;
    /// At most 256 scene objects and draw packets per snapshot/view.
    ludus::foundation::uint32 Objects = 256;
    /// Four retained target presentations, each consuming one RHI vertex buffer and binding set.
    ludus::foundation::uint32 Presentations = 4;
    /// Two overlapping prewarmed shader/layout incarnations for reload.
    ludus::foundation::uint32 MaterialPrograms = 2;
    /// Eight live/retained immutable parameter and texture versions.
    ludus::foundation::uint32 Materials = 8;
    /// Two resident/retained immutable shadow atlases for overlap and replacement.
    ludus::foundation::uint32 ShadowAtlases = 2;
};
/// Portable flat/unlit/lit renderer, one owner thread per device.
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
    /// Validate the installed unlit material ABI and prewarm every legal surface/offscreen
    /// conventional/reverse-Z/painter pipeline through R2's bounded cache, between frames.
    /// Two logical program versions; all six pipelines must be Ready before material creation.
    /// Null output required; Ready/Pending grants ownership. Failures preserve output.
    /// No driver pipeline work occurs in DrawView/Submit; no persistent native cache is implied.
    [[nodiscard]] rhi::RasterStatus PrewarmMaterialProgram(const rhi::RasterShaderDescription& vertex,
                                                           const rhi::RasterShaderDescription& fragment,
                                                           MaterialProgram& output) noexcept;
    /// Advance candidate prewarming between frames; Ready is immutable and allocation-free.
    [[nodiscard]] rhi::RasterStatus GetStatus(MaterialProgram handle) noexcept;
    /// Detach program ownership; earlier material/snapshot/GPU uses retain its resources.
    [[nodiscard]] rhi::RasterStatus Release(MaterialProgram& handle) noexcept;
    /// Copy parameters, sampler and exact sampled view; eight logical material versions.
    /// Requires a Ready owned program and Ready RGBA8 texture (or null white fallback).
    /// Ready/Pending grants ownership; invalid input/capacity preserves null output.
    [[nodiscard]] rhi::RasterStatus
    CreateMaterial(MaterialProgram program, const MaterialDescription& description, MaterialVersion& output) noexcept;
    /// Complete candidate binding creation between frames, without blocking.
    [[nodiscard]] rhi::RasterStatus GetStatus(MaterialVersion handle) noexcept;
    /// Atomically publish a Ready candidate and detach current (which may be null).
    /// Both are owned by this renderer and must be different. Failure preserves both;
    /// earlier snapshots/views retain current's old contents. Invalid reload never swaps.
    [[nodiscard]] rhi::RasterStatus PublishMaterial(MaterialVersion& candidate, MaterialVersion& current) noexcept;
    /// Detach material ownership; earlier snapshot/view/GPU uses retain the exact version.
    [[nodiscard]] rhi::RasterStatus Release(MaterialVersion& handle) noexcept;
    /// Enable linear lighting output and opaque shadow rendering after InitializeViews is Ready.
    /// Uses renderer_output.slang and renderer_shadow.slang; Ready/Pending owns setup resources.
    /// HdrColor enables prewarmed RGBA16F flat pipelines; RGBA8 remains the authored fallback.
    /// Failures preserve earlier profiles; a failed published setup requires Reset.
    [[nodiscard]] rhi::RasterStatus InitializeLighting(const rhi::RasterShaderDescription& outputVertex,
                                                       const rhi::RasterShaderDescription& outputFragment,
                                                       const rhi::RasterShaderDescription& shadowFragment) noexcept;
    /// Advance lighting setup without blocking, between frames.
    [[nodiscard]] rhi::RasterStatus PollLighting() noexcept;
    /// Validate renderer_lit.slang or renderer_clustered.slang and prewarm linear RGBA8/RGBA16F plus masked shadows.
    /// Clustered programs require eight bindings and an 8800-byte fragment uniform range;
    /// pass LitMaterialDescription::Clusters when creating their materials. Unsupported capabilities reject.
    /// Shadow fragment uses the same typed uniform schema and a subset of the lit bindings.
    /// Sixteen vertex locations required; HDR variants are omitted when HdrColor is false.
    /// Ready/Pending grants program ownership; failure preserves null output.
    [[nodiscard]] rhi::RasterStatus PrewarmLitProgram(const rhi::RasterShaderDescription& vertex,
                                                      const rhi::RasterShaderDescription& fragment,
                                                      const rhi::RasterShaderDescription& shadowFragment,
                                                      MaterialProgram& output) noexcept;
    /// Copy an immutable lit material and normalized bounded light list, between frames.
    /// Report and null output are preserved on failure. Shadows must be rendered and Ready;
    /// bake/shadow views are retained even after their owners are released.
    [[nodiscard]] rhi::RasterStatus CreateLitMaterial(MaterialProgram program,
                                                      const LitMaterialDescription& description,
                                                      MaterialVersion& output,
                                                      LightSelection& report) noexcept;
    /// Copy light regions and prepare retained conventional-depth caster views/atlas, between frames.
    /// Invalid gutters/overlap, unsupported masked-unlit casters or point maps preserve null output.
    /// Ready/Pending grants ownership; drawing is required before material sampling is admitted.
    [[nodiscard]] rhi::RasterStatus
    PrepareShadowAtlas(Snapshot handle, const ShadowAtlasDescription& description, ShadowAtlas& output) noexcept;
    /// Poll retained atlas/instance setup. Ready does not mean it has been rendered yet.
    [[nodiscard]] rhi::RasterStatus GetStatus(ShadowAtlas handle) const noexcept;
    /// Render all prepared regions in an acquired frame; no allocation or shader creation.
    /// Only one accepted draw per immutable atlas. A partial failure permanently rejects reuse;
    /// recreate a candidate. Caller must end/submit the frame before material creation.
    [[nodiscard]] rhi::RasterStatus DrawShadowAtlas(ShadowAtlas handle) noexcept;
    /// Detach atlas ownership; materials retain the exact rendered texture and caster version.
    [[nodiscard]] rhi::RasterStatus Release(ShadowAtlas& handle) noexcept;
    /// Prepare retained linear scene/display source and immutable exposure/output parameters.
    /// Accepts RGBA8 or supported RGBA16F attachments. Encode=false is required for drawing
    /// into a linear display target; UI can blend there before a second encode-only presentation.
    [[nodiscard]] rhi::RasterStatus PrepareLightingPresentation(rhi::TextureHandle source,
                                                                const ViewMapping& mapping,
                                                                const LightingOutput& lighting,
                                                                Presentation& output) noexcept;
    /// Compose a Ready lighting presentation in an acquired frame; select surface or linear RGBA8.
    /// Actual surface encoding selects shader or hardware conversion, exactly once. No blending.
    /// Draw into a display intermediate with Encode=false, then UI, then an encode-only surface pass.
    [[nodiscard]] rhi::RasterStatus DrawLightingPresentation(Presentation handle,
                                                             const rhi::RasterPassDescription& description =
                                                             {
                                                                 .ColorLoad = rhi::RasterLoad::Load,
                                                             }) noexcept;
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
    [[nodiscard]] rhi::RasterStatus PrewarmProgram(const rhi::RasterShaderDescription& vertex,
                                                   const rhi::RasterShaderDescription& fragment,
                                                   const rhi::RasterShaderDescription* shadow,
                                                   MaterialProgram& output) noexcept;
    [[nodiscard]] rhi::RasterStatus CreateMaterialData(MaterialProgram program,
                                                       const MaterialDescription& description,
                                                       const ludus::foundation::uint8* bytes,
                                                       ludus::foundation::usize size,
                                                       const LitMaterialDescription* lighting,
                                                       MaterialVersion& output,
                                                       const LightClusters* clusters = nullptr) noexcept;
    [[nodiscard]] rhi::RasterStatus PreparePresentationData(rhi::TextureHandle source,
                                                            const ViewMapping& mapping,
                                                            const LightingOutput* lighting,
                                                            Presentation& output) noexcept;
    struct State;
    ludus::foundation::core::UniquePtr<State> mState;
};
} // namespace ludus::graphics::renderer
