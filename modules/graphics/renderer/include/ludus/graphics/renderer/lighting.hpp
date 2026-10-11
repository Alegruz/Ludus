#pragma once

#include <ludus/foundation/base/types.h>

#include <ludus/foundation/math/geometry.hpp>
#include <ludus/foundation/math/matrix.hpp>
#include <ludus/foundation/math/transform.hpp>
#include <ludus/foundation/math/vector.hpp>
#include <ludus/graphics/renderer/materials.hpp>
#include <ludus/graphics/rhi/graph.h>
#include <ludus/graphics/rhi/raster.h>

namespace ludus::graphics::renderer
{
namespace internal
{
struct Access;
}
/// Immutable caster snapshot, light projections and rendered RGBA8 depth atlas.
/// Release detaches ownership; lit materials retain this exact incarnation.
struct ShadowAtlas final
{
private:
    ludus::foundation::uint64 Owner = 0, Generation = 0;
    ludus::foundation::uint32 Slot = 0;
    /// Grants private access to the renderer incarnation.
    friend struct internal::Access;
};
/// Punctual RGB light model; distances and positions use meters.
enum class LightKind : ludus::foundation::uint8
{
    /// Uniform incident illuminance in lux; Direction points from light toward scene.
    Directional,
    /// Isotropic luminous intensity in candela, with bounded smooth range falloff.
    Point,
    /// On-axis candela with a squared smooth cone; angles expressed as cosines.
    Spot,
};
/// Copied finite light authoring data, independent of scene ownership.
struct DirectLight final
{
    /// Unique nonzero source identity within the submitted list; defines stable selection.
    ludus::foundation::uint64 SourceId = 0;
    /// Direct evaluation model.
    LightKind Kind = LightKind::Directional;
    /// Display-linear RGB hue, each channel in [0,1].
    ludus::foundation::math::Vector3 Color{1, 1, 1};
    /// Lux for directional, candela for point/spot; [0,1000000].
    ludus::foundation::float32 Intensity = 1;
    /// Position in meters, each component in [-1000000,1000000].
    ludus::foundation::math::Vector3 Position{};
    /// Nonzero finite direction for directional/spot; normalized during admission.
    ludus::foundation::math::Vector3 Direction{0, 0, -1};
    /// Point/spot range in meters, [0.01,1000000]; attenuation becomes zero at range.
    ludus::foundation::float32 Range = 10;
    /// Spot inner half-angle cosine; -1 < OuterCos < InnerCos <= 1.
    ludus::foundation::float32 InnerCos = 0.9F;
    /// Spot outer half-angle cosine, distinct from InnerCos.
    ludus::foundation::float32 OuterCos = 0.8F;
};
/// Stable bounded forward list; entries sorted by SourceId, not brightness or opaque depth.
struct LightSelection final
{
    /// At most eight selected normalized light descriptions.
    DirectLight Lights[8]{};
    /// Number of initialized entries in Lights.
    ludus::foundation::uint32 Count = 0;
    /// Submitted count minus Count; positive means a declared quality reduction.
    ludus::foundation::uint32 Dropped = 0;
};
/// Validate up to 128 lights and retain the eight smallest unique SourceIds.
/// No allocation; invalid/count overflow preserves output. Saturation succeeds with Dropped.
[[nodiscard]] rhi::RasterStatus
SelectDirectLights(const DirectLight* lights, ludus::foundation::usize count, LightSelection& output) noexcept;
/// Metadata accompanying one baked irradiance chart; no runtime bake or chart packing.
struct BakedLighting final
{
    /// Ready single-mip linear RGBA8 chart with replicated gutters; null declares environment fallback.
    rhi::TextureHandle Texture{};
    /// Linear irradiance multiplier in [0,10000]; texture stores normalized linear RGB.
    ludus::foundation::math::Vector3 Scale{1, 1, 1};
    /// Bake UV scale XY and offset ZW, selecting at least two interior texel centers per axis.
    /// Bounds must leave a replicated gutter texel plus half a texel on every side (1e-6 UV rounding tolerance).
    ludus::foundation::math::Vector4 UvTransform{1, 1, 0, 0};
    /// Exact nonzero source identity and authoring revisions required for bake validity.
    ludus::foundation::uint64 SourceId = 0;
    /// Scene bake revision; SceneItem must supply the same value.
    ludus::foundation::uint64 SceneRevision = 0;
    /// Cook dependency revision/hash; SceneItem must supply the same value.
    ludus::foundation::uint64 DependencyRevision = 0;
    /// Exact local-to-world used by the bake; moved objects use environment fallback.
    ludus::foundation::math::Affine3 Transform{};
    /// Optional baked direct-light identity; replace that light's diffuse term only for valid bakes.
    /// Zero means indirect-only. One chart replaces at most one direct diffuse term; dynamic direct specular remains.
    ludus::foundation::uint64 DirectLightId = 0;
};
/// Immutable unlit base schema plus the L4 metallic/roughness lighting terms.
struct LitMaterialDescription final
{
    /// Base texture, tint, alpha and sampler retain the L3 contracts.
    MaterialDescription Surface{};
    /// Metallic fraction in [0,1]; dielectric F0 is fixed at 0.04.
    ludus::foundation::float32 Metallic = 0;
    /// Perceptual roughness in [0.045,1]; alpha is its square.
    ludus::foundation::float32 Roughness = 0.5F;
    /// Linear surface emission in [0,10000]; does not create a scene light.
    ludus::foundation::math::Vector3 Emission{};
    /// Constant diffuse irradiance in [0,10000], used when a baked chart is absent/invalid.
    ludus::foundation::math::Vector3 EnvironmentIrradiance{};
    /// Constant filtered environment radiance in [0,10000]; single-scatter Fresnel approximation.
    ludus::foundation::math::Vector3 EnvironmentRadiance{};
    /// Borrowed list, validated and copied before CreateLitMaterial returns.
    const DirectLight* Lights = nullptr;
    /// At most 128 input lights; stable saturation retains eight and reports Dropped.
    ludus::foundation::usize LightCount = 0;
    /// Optional immutable bake chart/version with explicit direct-light replacement.
    BakedLighting Bake{};
    /// Ready rendered atlas; null supplies unshadowed direct lighting.
    ShadowAtlas Shadows{};
};
/// One atlas cell, authored from an unjittered light view; order selects overlapping cascades.
struct ShadowRegion final
{
    /// Exact directional or spot light description; point-light maps are Unsupported.
    DirectLight Light{};
    /// Canonical conventional-depth light projection, Z [0,1], XY [-1,1], Y up.
    ludus::foundation::math::Matrix4 WorldToClip = ludus::foundation::math::Matrix4::Identity();
    /// Inner drawing rectangle; must leave at least one texel of clear gutter on every side.
    rhi::RasterRectangle Interior{};
    /// Receiver bias in normalized light depth, [0,0.01], independent of camera reverse-Z.
    ludus::foundation::float32 Bias = 0.001F;
};
/// Bounded four-cell shadow atlas. Typical layout: two directional cascades and two spots.
struct ShadowAtlasDescription final
{
    /// RGBA8 atlas width, [4,2048], subject to device limits.
    ludus::foundation::uint32 Width = 256;
    /// RGBA8 atlas height, [4,2048], subject to device limits.
    ludus::foundation::uint32 Height = 256;
    /// Borrowed disjoint guttered regions; copied during preparation.
    const ShadowRegion* Regions = nullptr;
    /// Positive count, at most four; cascades may share one directional light identity.
    ludus::foundation::usize Count = 0;
};
/// Fit a stabilized directional light projection to finite receiver/caster bounds.
/// Snaps XY center to the inner-resolution texel grid, pads one texel and uses conventional
/// depth. Call with unjittered cascade bounds; no camera split policy is hidden here.
/// Invalid/degenerate/nonrepresentable input preserves output. Allocation-free.
[[nodiscard]] rhi::RasterStatus BuildDirectionalShadowMatrix(const ludus::foundation::math::Aabb3& bounds,
                                                             ludus::foundation::math::Vector3 direction,
                                                             ludus::foundation::uint32 resolution,
                                                             ludus::foundation::math::Matrix4& output) noexcept;
/// Display composition parameters for one immutable presentation version.
struct LightingOutput final
{
    /// Manual scene exposure multiplier, [0.000001,1000], applied exactly once.
    ludus::foundation::float32 Exposure = 1;
    /// Apply guarded per-channel Reinhard after exposure; false preserves linear RGB.
    bool ToneMap = true;
    /// Encode to sRGB only for a nonlinear unorm surface; hardware sRGB encodes instead.
    /// False selects a linear display intermediate, suitable for subsequent linear UI blending.
    bool Encode = true;
};
/// Independent CPU reference for the documented single-scatter GGX/Smith/Schlick + Lambert BRDF.
/// Base color and metallic [0,1], roughness [0.045,1]; directions normalized on admission.
/// Returns the BRDF times max(N dot L,0), without incident radiance; failure preserves output.
[[nodiscard]] rhi::RasterStatus EvaluateLitBrdf(ludus::foundation::math::Vector3 baseColor,
                                                ludus::foundation::float32 metallic,
                                                ludus::foundation::float32 roughness,
                                                ludus::foundation::math::Vector3 normal,
                                                ludus::foundation::math::Vector3 view,
                                                ludus::foundation::math::Vector3 light,
                                                ludus::foundation::math::Vector3& output) noexcept;
} // namespace ludus::graphics::renderer
