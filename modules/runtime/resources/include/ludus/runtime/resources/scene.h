#pragma once

#include <ludus/foundation/base/core.h>

#include <ludus/foundation/containers/array.hpp>
#include <ludus/foundation/math/transform.hpp>
#include <ludus/foundation/math/vector.hpp>
#include <ludus/runtime/resources/manager.h>

namespace ludus::resources
{
/// CPU-owned RGBA8 sRGB mip zero. Rows are tightly packed, top to bottom.
struct SceneTexture final
{
    uint32 Width = 0;                ///< Positive width, at most 4096 texels.
    uint32 Height = 0;               ///< Positive height, at most 4096 texels.
    foundation::Array<uint8> Pixels; ///< Exactly Width*Height*4 bytes; at most 16 MiB.
};
/// Frozen RGB metallic/roughness reflection and two-sided emission profile.
struct SceneMaterial final
{
    uint32 Texture = ~uint32{0};          ///< Scene-local texture index; all ones means black untextured surface.
    bool Repeat = false;                  ///< Repeat UV addressing; false clamps at texture borders.
    float32 Metallic = 0;                 ///< Metallic fraction in [0,1].
    float32 Roughness = .5F;              ///< Perceptual roughness in [0.045,1], matching renderer admission.
    foundation::math::Vector3 Emission{}; ///< Scene-linear RGB emitted radiance, each channel in [0,10000].
};
/// Owned immutable CPU mesh arrays. Renderer installation copies these attributes.
struct SceneMesh final
{
    foundation::Array<foundation::math::Vector3> Positions; ///< Finite mesh-local positions.
    foundation::Array<foundation::math::Vector3> Normals;   ///< Finite nonzero authored shading normals.
    foundation::Array<foundation::math::Vector2> Texcoords; ///< Finite mip-zero texture coordinates.
    foundation::Array<uint32> Indices;                      ///< Triangle list, bounded by Positions count.
};
/// Transient resolved binding; persist resource IDs in the level, never these indices.
struct SceneObject final
{
    uint32 Mesh = 0;                     ///< Index into SceneDefinition::Meshes.
    uint32 Material = 0;                 ///< Index into SceneDefinition::Materials.
    foundation::math::Affine3 Transform; ///< Finite nonsingular mesh-local to world transform.
};
/// Fully resident initial reference profile. Independent of Qt, source formats and GPU handles.
/// Decode on a worker, then publish immutable storage on the Manager control owner.
/// Runtime reconstruction uses exact project artifact revisions; no source parser is called.
struct SceneDefinition final
{
    foundation::Array<SceneTexture> Textures;   ///< At most eight textures, 32 MiB combined pixels.
    foundation::Array<SceneMaterial> Materials; ///< At most eight distinct materials.
    foundation::Array<SceneMesh> Meshes;        ///< At most six distinct meshes, 500000 combined vertices.
    foundation::Array<SceneObject> Objects;     ///< At most 128 independent instances.
    foundation::math::Vector3 Eye;              ///< Authored perspective pinhole in world units.
    foundation::math::Vector3 Target;           ///< Finite look-at target distinct from Eye.
    foundation::math::Vector3 Up;               ///< Camera up direction not parallel to its forward direction.
    float32 VerticalFov = 1;                    ///< Perspective vertical field of view in radians, in (0,pi).
    /// Decode LUSCN01 versions one/two little-endian interchange, at most 64 MiB. No file I/O or source import.
    /// Failure preserves output; allocation failures return OutOfMemory. The caller owns all
    /// resulting arrays and must keep them immutable while any adapter/lease references them.
    [[nodiscard]] static Status Decode(const uint8* bytes, usize count, SceneDefinition& output) noexcept;
    /// Known owned array bytes, excluding allocator metadata and driver allocations.
    [[nodiscard]] uint64 Bytes() const noexcept;
};
/// Aggregate CPU scene adapter for the fully resident profile; GPU ownership is separate.
/// Uses one prevalidated immutable definition borrowed from its composition owner. The owner
/// keeps it alive through Manager::Shutdown returning Ok. No I/O, decoder, allocation or waits
/// occur in callbacks. The renderer copies geometry/textures and owns submitted-use retirement.
/// This aggregate is a transient render binding, not the project catalog's per-asset graph.
class SceneDefinitionAdapter final
{
public:
    /// Construct an unbound context without allocation.
    SceneDefinitionAdapter() noexcept = default;
    /// Release control storage. @pre Every manager using Descriptor has drained Shutdown (Ok).
    ~SceneDefinitionAdapter() noexcept;
    /// Context identities cannot be copied.
    SceneDefinitionAdapter(const SceneDefinitionAdapter&) = delete;
    /// Context identities cannot be assigned.
    SceneDefinitionAdapter& operator=(const SceneDefinitionAdapter&) = delete;
    /// Bind a predecoded scene and an exact derived snapshot ID. An initialized adapter
    /// cannot be rebound. @pre scene is an unchanged successful Decode result. Type must be CookedScene; invalid input
    /// preserves the adapter.
    [[nodiscard]] Status Initialize(const content::Resource& resource, const SceneDefinition& scene) noexcept;
    /// Copied callback table; CPU bytes remain composition-owned, outside manager reservations.
    /// @pre Initialize succeeded; adapter and scene outlive the manager and its leases.
    [[nodiscard]] Adapter Descriptor() noexcept;

private:
    struct State;
    State* mState = nullptr;
    static uint64 Epoch(void* context) noexcept;
    static Status Begin(void* context, const content::Resource& resource, void*& candidate) noexcept;
    static Status Poll(void* context, void* candidate, const void*& payload) noexcept;
    static void Cancel(void* context, void* candidate) noexcept;
    static Status Retire(void* context, void* candidate) noexcept;
    static Status Validate(void* context, const void* payload) noexcept;
};
} // namespace ludus::resources
