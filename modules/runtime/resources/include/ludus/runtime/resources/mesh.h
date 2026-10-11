#pragma once

#include <ludus/foundation/base/core.h>

#include <ludus/foundation/math/vector.hpp>
#include <ludus/runtime/resources/manager.h>

namespace ludus::resources
{
/// Borrowed immutable CPU triangle geometry. No GPU ownership or file-format promise.
/// A payload borrowed through Manager::Borrow remains valid only while its lease is held.
/// Renderer::CreateMesh copies these arrays; renderer/RHI own subsequent GPU retirement.
struct MeshDefinition final
{
    const foundation::math::Vector3* Positions = nullptr; ///< Finite mesh-local XYZ, world units chosen by authoring.
    usize VertexCount = 0;                                ///< 1..4096 vertices in this initial profile.
    const uint32* Indices = nullptr;                      ///< Triangle list; every index is below VertexCount.
    usize IndexCount = 0;                                 ///< Positive multiple of three, at most 12288.
    const foundation::math::Vector2* Texcoords = nullptr; ///< Optional finite UVs, VertexCount elements.
    const foundation::math::Vector3* Normals = nullptr;   ///< Optional finite nonzero normals, VertexCount elements.
};
/// Explicit composition input, independent of a source decoder or filesystem.
struct MeshSource final
{
    content::Resource Resource; ///< Mesh kind, exact catalog ID and normalized relative path.
    MeshDefinition Geometry;    ///< Arrays must remain immutable and alive through adapter destruction.
};
/// CPU mesh-definition adapter: at most 16 sources and 16 simultaneous candidate/resident slots.
/// All methods/callbacks run on the Manager control owner, outside render/audio critical sections.
/// Initialize validates geometry and copies metadata; it borrows immutable source arrays.
/// Begin allocates candidate arrays explicitly; each Poll copies at most 128 vertices
/// and 384 indices. Retirement frees only CPU copies, synchronously, with no submitted
/// CPU borrowers after the last lease. This adapter never installs or charges GPU resources.
/// Context and source arrays outlive Manager::Shutdown returning Ok. No file I/O, decoder,
/// global registration, source mutation, dependency scheduling or reload is implied.
class MeshDefinitionAdapter final
{
public:
    /// Construct without allocation or ownership.
    MeshDefinitionAdapter() noexcept = default;
    /// Free fixed control storage. @pre All manager candidates have retired.
    ~MeshDefinitionAdapter() noexcept;
    /// Context identity cannot be copied.
    MeshDefinitionAdapter(const MeshDefinitionAdapter&) = delete;
    /// Context identity cannot be assigned.
    MeshDefinitionAdapter& operator=(const MeshDefinitionAdapter&) = delete;
    /// Validate and copy 1..16 source metadata into fixed control storage.
    /// Invalid/duplicate IDs, mismatched kind, bad topology, nonfinite attributes,
    /// empty normals or limits return Failed; allocation returns OutOfMemory.
    /// @pre Every manager using an earlier Descriptor has completed Shutdown (Ok).
    /// Live candidates prevent reinitialization (InvalidState). Failure preserves
    /// a previous drained profile. Success obtains a fresh nonreused epoch.
    [[nodiscard]] Status Initialize(const MeshSource* sources, usize count) noexcept;
    /// Return callbacks for the copied immutable profile; use only after Initialize succeeds.
    /// Peak reserves the largest source's CPU array bytes for each operation; preparation
    /// is zero because arrays are allocated directly as candidate/resident storage.
    /// Context control allocation and borrowed source storage are outside these reservations.
    [[nodiscard]] Adapter Descriptor() noexcept;
    /// Fixed control bytes allocated after Initialize (excluding allocator headers/sources).
    [[nodiscard]] usize ControlBytes() const noexcept;

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
