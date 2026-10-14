#pragma once

#include <ludus/foundation/base/compiler.h>
#include <ludus/foundation/base/core.h>

#include <ludus/foundation/containers/borrowed_view.hpp>
#include <ludus/foundation/math/transform.hpp>
#include <ludus/foundation/math/vector.hpp>
#include <ludus/graphics/renderer/materials.hpp>
#include <ludus/graphics/rhi/raster.h>

namespace ludus::resources
{
struct SceneDefinition;
}
namespace ludus::graphics::renderer
{
/// Current World material terms for one placed object, copied at Initialize.
/// Texture indices refer to the retained SceneDefinition CPU texture array.
/// This override uses MaterialDescription's white untextured base; an empty
/// override list retains SceneDefinition's legacy black untextured contract.
struct StaticLightingSurfaceDescription final
{
    foundation::uint32 Texture =
        ~foundation::uint32{0};                 ///< CPU scene texture index or all ones for the untextured base.
    foundation::math::Vector4 Tint{1, 1, 1, 1}; ///< Linear RGB/alpha multiplier in [0,1].
    foundation::math::Vector4 UvTransform{1,
                                          1,
                                          0,
                                          0};    ///< Base texture UV scale XY and offset ZW, finite and bounded to1e6.
    foundation::float32 Metallic = 0;            ///< Metallic fraction in [0,1].
    foundation::float32 Roughness = .5F;         ///< Perceptual roughness in [.045,1].
    foundation::math::Vector3 Emission{};        ///< Resolved linear RGB nits, each channel in [0,10000].
    bool IsEmissionTwoSided = true;              ///< Geometric emission sidedness.
    bool ShouldRepeatTexture = false;            ///< Repeat UV addressing; false clamps.
    MaterialAlpha Alpha = MaterialAlpha::Opaque; ///< Opaque or Masked transport; Painter returns Unsupported.
    foundation::float32 AlphaCutoff = .5F;       ///< Masked alpha threshold in [0,1].
};
/// Explicit static diffuse transport quality and exact authoring provenance.
struct StaticLightingBakeDescription final
{
    foundation::uint32 SamplesPerTriangle = 64;      ///< Cosine-hemisphere samples per triangle, 1..65536.
    foundation::uint32 MaxDepth = 4;                 ///< Scattering-event limit, 2..64; introduces truncation bias.
    foundation::float32 SecondaryDistance = 1000000; ///< Ray range in metres, (0,2e6].
    foundation::uint64 Seed = 0;                     ///< Deterministic addressed Philox seed.
    foundation::uint64 SceneRevision = 0;            ///< Nonzero version; changes require a fresh job.
    foundation::uint64 DependencyRevision = 0;       ///< Nonzero exact cooked dependency version.
    /// Optional synchronous borrowed current-World overrides, exactly one per
    /// SceneDefinition::Object. Resolved emission power must already include
    /// the current placement's world area and side count. Initialize copies all
    /// terms, so the override array need not outlive that call.
    foundation::BorrowedView<const StaticLightingSurfaceDescription> Surfaces;
    foundation::usize CpuByteBudget =
        foundation::usize{256} * 1024 * 1024; ///< Upper bound on owned job arrays, including BVH scratch.
};
/// Borrowed completed chart payload; caller uploads it on the RHI owner thread.
/// No GPU readback, texture owner or display image is produced by the bake.
struct StaticLightingChart final
{
    foundation::uint32 Width = 0;  ///< Power-of-two chart width in texels.
    foundation::uint32 Height = 0; ///< Power-of-two chart height in texels.
    foundation::BorrowedView<const foundation::uint8>
        Pixels; ///< RGBA8 linear irradiance with replicated per-triangle gutters; borrow ends at job destruction.
    foundation::math::Vector3 Scale{1, 1, 1}; ///< Multiply normalized texture RGB by this irradiance scale.
    foundation::math::Vector4 UvTransform{1,
                                          1,
                                          0,
                                          0};  ///< Transform from helper chart UVs to sampled atlas coordinates.
    foundation::uint64 SourceId = 0;           ///< Exact placed instance identity.
    foundation::uint64 SceneRevision = 0;      ///< Must match the drawn extraction's revision.
    foundation::uint64 DependencyRevision = 0; ///< Must match the retained cooked dependency revision.
    foundation::math::Affine3 Transform{};     ///< Exact mesh-to-world used by the bake.
};
/// CPU-only worker-owned incremental static diffuse bake. Charts include all
/// visible static area/environment direct diffuse and later indirect transport;
/// analytic direct diffuse/specular stays raster. Set BakedLighting's
/// AreTriangleChartsEnabled and HasStaticAreaDirect, and DirectLightId=0.
/// Area specular is outside this profile. Per-triangle centroid irradiance is
/// coarse spatial reconstruction; sample count controls transport variance.
/// CPU-only worker-owned incremental static diffuse bake. No Renderer, GPU,
/// Qt or native ray-query state is touched. Use serial calls on one worker;
/// transfer the completed job to the composition owner after worker completion.
/// Source scene storage stays immutable and alive until job destruction because
/// retained CPU texture bytes are borrowed. Cancel by ceasing work and destroying
/// on that worker; discard obsolete revision results before publication.
class StaticLightingBake final
{
public:
    /// Construct an unbound job without allocation.
    LUDUS_INLINE StaticLightingBake() noexcept = default;
    /// Release only CPU storage; must not race Initialize/Step/GetChart.
    ~StaticLightingBake() noexcept;
    /// Worker job identity cannot be copied.
    StaticLightingBake(const StaticLightingBake&) = delete;
    /// Worker job identity cannot be assigned.
    StaticLightingBake& operator=(const StaticLightingBake&) = delete;
    /// Build an owned world-space BVH and triangle chart accumulators on a worker.
    /// @pre scene satisfies SceneDefinition::Decode bounds and stays immutable during the job.
    /// Legacy empty bake coordinates are accepted; its upload adapter must assign
    /// the shared triangle chart helper coordinates to deindexed mesh vertices.
    /// Returns Pending on accepted preparation; failure preserves an unbound job.
    [[nodiscard]] rhi::RasterStatus Initialize(const resources::SceneDefinition& scene,
                                               const StaticLightingBakeDescription& description) noexcept;
    /// Evaluate at most maxPathSamples complete cosine paths; positive count required.
    /// Pending keeps uncommitted charts private; Ready means all charts are encoded.
    /// Each path has bounded depth; initialization/final encoding belong on a worker.
    /// Invalid contributions permanently fail this candidate; never publish it.
    [[nodiscard]] rhi::RasterStatus Step(foundation::uint32 maxPathSamples) noexcept;
    /// Borrow one placed object's completed linear chart; no allocation or GPU call.
    /// NotReady/invalid index preserves outChart. DirectLightId for uploaded
    /// BakedLighting must be zero: charts contain static area/environment direct diffuse plus indirect irradiance.
    [[nodiscard]] rhi::RasterStatus GetChart(foundation::usize objectIndex,
                                             StaticLightingChart& outChart) const noexcept;

private:
    struct State;
    State* mStateOrNull = nullptr;
};
} // namespace ludus::graphics::renderer
