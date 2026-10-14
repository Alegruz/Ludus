#include <ludus/graphics/renderer/static_lighting.hpp>

#include <ludus/content/scene_format/luscn.h>
#include <ludus/foundation/base/compiler.h>
#include <ludus/foundation/base/pointer.hpp>
#include <ludus/foundation/containers/array.hpp>
#include <ludus/foundation/containers/static_array.hpp>
#include <ludus/foundation/math/status.hpp>
#include <ludus/foundation/math/summation.hpp>
#include <ludus/runtime/resources/scene.h>

#include <new>

#include "internal/reference_data.h"

namespace ludus::graphics::renderer
{
using namespace foundation;
namespace math = foundation::math;
namespace format = content::scene_format;
using Status = rhi::RasterStatus;
namespace
{
constexpr uint32 BAKE_RANDOM_DOMAIN = 0x42414B45; // Independent addressed BAKE stream.
constexpr usize TRIANGLE_CORNERS = 3;
constexpr float32 AFFINE_ERROR_ALLOWANCE = 1e-6F; // Conservative FP32 transformed-position allowance.
struct ChartRecord final
{
    format::BakeChartLayout Layout;
    Array<uint8> Pixels;
    math::Vector3 Scale{};
    uint32 FirstTriangle = 0;
    uint64 SourceId = 0;
    math::Affine3 Transform;
};
} // namespace
struct StaticLightingBake::State final
{
    StaticLightingBakeDescription Description;
    internal::ReferenceJob Job;
    Array<internal::ReferenceSurfaceData> Surfaces;
    Array<internal::ReferenceSum> Irradiance;
    Array<ChartRecord> Charts;
    uint32 Triangle = 0;
    uint32 Sample = 0;
    Status Progress = Status::Pending;

    LUDUS_INLINE State() noexcept
    {
        Job.IsCpuOnly = true;
    }
};
StaticLightingBake::~StaticLightingBake() noexcept
{
    delete mStateOrNull;
}
Status StaticLightingBake::Initialize(const resources::SceneDefinition& scene,
                                      const StaticLightingBakeDescription& description) noexcept
{
    if (mStateOrNull != nullptr)
    {
        return Status::InvalidState;
    }
    if (description.SceneRevision == 0 || description.DependencyRevision == 0 || description.SamplesPerTriangle == 0 ||
        description.SamplesPerTriangle > 65536 || description.MaxDepth < 2 || description.MaxDepth > 64 ||
        math::IsFinite(description.SecondaryDistance) == false || description.SecondaryDistance <= 0 ||
        description.SecondaryDistance > 2000000 || scene.Objects.IsEmpty() == true ||
        scene.Objects.GetSize() > format::MAX_OBJECTS || scene.Materials.GetSize() > format::MAX_MATERIALS ||
        scene.Lights.GetSize() > format::MAX_LIGHTS || description.Surfaces.IsValid() == false ||
        (description.Surfaces.IsEmpty() == false && description.Surfaces.GetSize() != scene.Objects.GetSize()))
    {
        return Status::InvalidDescription;
    }
    usize triangleCount = 0;
    uint64 pixelBytes = 0;
    for (const resources::SceneObject& object : scene.Objects)
    {
        if (object.SourceId == 0 || object.Mesh >= scene.Meshes.GetSize() ||
            object.Material >= scene.Materials.GetSize())
        {
            return Status::InvalidDescription;
        }
        const resources::SceneMesh& mesh = scene.Meshes[object.Mesh];
        if (mesh.Indices.IsEmpty() == true || mesh.Indices.GetSize() > format::MAX_INDICES ||
            mesh.Indices.GetSize() % TRIANGLE_CORNERS != 0 || mesh.Normals.GetSize() != mesh.Positions.GetSize() ||
            mesh.Texcoords.GetSize() != mesh.Positions.GetSize())
        {
            return Status::InvalidDescription;
        }
        format::BakeChartLayout layout;
        const uint32 triangles = static_cast<uint32>(mesh.Indices.GetSize() / TRIANGLE_CORNERS);
        if (format::TryMakeBakeChartLayout(triangles, layout) == false)
        {
            return Status::CapacityExceeded;
        }
        triangleCount += triangles;
        pixelBytes += uint64(layout.Width) * layout.Height * format::TEXEL_BYTES;
    }
    const usize objectCount = scene.Objects.GetSize();
    const usize environmentCount = scene.Environment.Pixels.GetSize();
    // Peak includes temporary BVH scratch/order and retained node capacity.
    const uint64 requiredBytes =
        sizeof(State) + pixelBytes +
        uint64(objectCount) *
            (sizeof(ChartRecord) + sizeof(internal::ReferenceSurfaceData) + sizeof(internal::ReferenceInstance)) +
        uint64(triangleCount) * (sizeof(internal::ReferenceTriangle) + sizeof(internal::ReferenceSum) +
                                 3 * sizeof(uint32) + 2 * sizeof(internal::ReferenceBvhNode)) +
        sizeof(internal::EnvironmentData) + uint64(environmentCount) * (sizeof(math::Vector4) + sizeof(float64));
    if (triangleCount > ~uint32{0} / 2 || requiredBytes > description.CpuByteBudget)
    {
        return Status::CapacityExceeded;
    }
    core::UniquePtr<State> state;
    state.Reset(new (std::nothrow) State);
    if (state.Get() == nullptr)
    {
        return Status::OutOfMemory;
    }
    state->Description = description;
    state->Description.Surfaces = {}; // All caller overrides are copied during this call, never retained.
    internal::ReferenceJob& job = state->Job;
    job.Description.MaxDepth = description.MaxDepth;
    job.Description.RouletteDepth = 3;
    job.Description.SecondaryDistance = description.SecondaryDistance;
    job.Description.Environment = scene.Environment.Rgb;
    if (math::TryMakeRandomKey(description.Seed, BAKE_RANDOM_DOMAIN, job.Key) != math::MathStatus::Success)
    {
        return Status::InvalidDescription;
    }
    for (const resources::SceneLight& source : scene.Lights)
    {
        if (source.IsEnabled == false)
        {
            continue;
        }
        const float32 intensity =
            math::Max(source.IntensityRgb.X, math::Max(source.IntensityRgb.Y, source.IntensityRgb.Z));
        DirectLight& light = job.Lights[job.Description.LightCount++];
        light.SourceId = source.SourceId;
        light.Kind = static_cast<LightKind>(source.Kind);
        light.Color = intensity > 0 ? source.IntensityRgb / intensity : math::Vector3{};
        light.Intensity = intensity;
        light.Position = source.PositionWorld;
        light.Direction = source.DirectionWorld;
        light.Range = source.Range;
        light.InnerCos = source.InnerCos;
        light.OuterCos = source.OuterCos;
    }
    LightSelection normalized;
    const Status lightStatus = NormalizeDirectLights(job.Lights, job.Description.LightCount, normalized);
    if (lightStatus != Status::Ready)
    {
        return lightStatus;
    }
    for (usize i = 0; i < normalized.Count; ++i)
    {
        job.Lights[i] = normalized.Lights[i];
    }
    if (environmentCount != 0)
    {
        internal::EnvironmentData* environmentOrNull = new (std::nothrow) internal::EnvironmentData;
        if (environmentOrNull == nullptr)
        {
            return Status::OutOfMemory;
        }
        job.EnvironmentOrNull = environmentOrNull;
        environmentOrNull->IsCpuOnly = true;
        environmentOrNull->Width = scene.Environment.Width;
        environmentOrNull->Height = scene.Environment.Height;
        if (environmentOrNull->Width == 0 || environmentOrNull->Height == 0 ||
            usize(environmentOrNull->Width) * environmentOrNull->Height != environmentCount)
        {
            return Status::InvalidDescription;
        }
        if (environmentOrNull->Pixels.TryResize(environmentCount) == false)
        {
            return Status::OutOfMemory;
        }
        for (usize i = 0; i < environmentCount; ++i)
        {
            const math::Vector3 radiance = math::Hadamard(scene.Environment.Pixels[i], scene.Environment.Rgb);
            if (internal::ReferenceColor(radiance, 1e12F) == false)
            {
                return Status::InvalidDescription;
            }
            environmentOrNull->Pixels[i] = {radiance.X, radiance.Y, radiance.Z, 1};
        }
        const Status environmentStatus = internal::BuildEnvironmentDistribution(*environmentOrNull);
        if (environmentStatus != Status::Ready)
        {
            return environmentStatus;
        }
    }
    if (state->Surfaces.TryResize(objectCount) == false || state->Charts.TryResize(objectCount) == false ||
        state->Irradiance.TryResize(triangleCount) == false || job.Scene.Instances.TryResize(objectCount) == false ||
        job.Scene.Triangles.TryResize(triangleCount) == false ||
        job.Scene.Emitters.TryEnsureCapacity(triangleCount) == false)
    {
        return Status::OutOfMemory;
    }
    job.Scene.InstanceCount = objectCount;
    uint32 triangleIndex = 0;
    for (usize objectIndex = 0; objectIndex < objectCount; ++objectIndex)
    {
        const resources::SceneObject& object = scene.Objects[objectIndex];
        const resources::SceneMaterial& material = scene.Materials[object.Material];
        const resources::SceneMesh& mesh = scene.Meshes[object.Mesh];
        internal::ReferenceInstance& instance = job.Scene.Instances[objectIndex];
        internal::ReferenceSurfaceData& surface = state->Surfaces[objectIndex];
        ChartRecord& chart = state->Charts[objectIndex];
        instance.Surface = &surface;
        instance.SourceId = object.SourceId;
        instance.FirstTriangle = triangleIndex;
        instance.TriangleCount = mesh.Indices.GetSize() / TRIANGLE_CORNERS;
        chart.FirstTriangle = triangleIndex;
        chart.SourceId = object.SourceId;
        chart.Transform = object.Transform;
        static_cast<void>(format::TryMakeBakeChartLayout(static_cast<uint32>(instance.TriangleCount), chart.Layout));
        if (chart.Pixels.TryResize(usize(chart.Layout.Width) * chart.Layout.Height * format::TEXEL_BYTES) == false)
        {
            return Status::OutOfMemory;
        }
        const bool hasOverride = description.Surfaces.IsEmpty() == false;
        const uint32 textureIndex = hasOverride == true ? description.Surfaces[objectIndex].Texture : material.Texture;
        surface.Bsdf = {{1, 1, 1}, material.Metallic, material.Roughness};
        surface.Emission = material.Emission;
        surface.IsEmissionTwoSided = material.IsEmissionTwoSided;
        surface.Sampler.Filter = rhi::RasterFilter::Linear;
        surface.Sampler.U = surface.Sampler.V =
            material.Repeat == true ? rhi::RasterAddress::Repeat : rhi::RasterAddress::Clamp;
        if (hasOverride == true)
        {
            const StaticLightingSurfaceDescription& override = description.Surfaces[objectIndex];
            if (internal::ReferenceColor({override.Tint.X, override.Tint.Y, override.Tint.Z}, 1) == false ||
                math::IsFinite(override.Tint.W) == false || override.Tint.W < 0 || override.Tint.W > 1 ||
                math::IsFinite(override.UvTransform) == false || math::Abs(override.UvTransform.X) > 1000000 ||
                math::Abs(override.UvTransform.Y) > 1000000 || math::Abs(override.UvTransform.Z) > 1000000 ||
                math::Abs(override.UvTransform.W) > 1000000 || math::IsFinite(override.Metallic) == false ||
                override.Metallic < 0 || override.Metallic > 1 || math::IsFinite(override.Roughness) == false ||
                override.Roughness < .045F || override.Roughness > 1 ||
                internal::ReferenceColor(override.Emission, 10000) == false ||
                math::IsFinite(override.AlphaCutoff) == false || override.AlphaCutoff < 0 || override.AlphaCutoff > 1 ||
                static_cast<uint8>(override.Alpha) > static_cast<uint8>(MaterialAlpha::Painter))
            {
                return Status::InvalidDescription;
            }
            if (override.Alpha == MaterialAlpha::Painter)
            {
                return Status::Unsupported;
            }
            surface.Bsdf = {{override.Tint.X, override.Tint.Y, override.Tint.Z}, override.Metallic, override.Roughness};
            surface.Tint = override.Tint;
            surface.UvTransform = override.UvTransform;
            surface.Emission = override.Emission;
            surface.IsEmissionTwoSided = override.IsEmissionTwoSided;
            surface.Alpha = override.Alpha;
            surface.AlphaCutoff = override.AlphaCutoff;
            surface.Sampler.U = surface.Sampler.V =
                override.ShouldRepeatTexture == true ? rhi::RasterAddress::Repeat : rhi::RasterAddress::Clamp;
        }
        if (textureIndex != format::NO_TEXTURE)
        {
            if (textureIndex >= scene.Textures.GetSize())
            {
                return Status::InvalidDescription;
            }
            const resources::SceneTexture& texture = scene.Textures[textureIndex];
            instance.Texture = {texture.Pixels.GetData(),
                                texture.Pixels.GetSize(),
                                usize(texture.Width) * format::TEXEL_BYTES,
                                texture.Width,
                                texture.Height,
                                rhi::RasterFormat::Rgba8Srgb};
            surface.HasTexture = true;
        }
        else if (hasOverride == false)
        {
            surface.Bsdf.BaseColor = {};
        }
        math::Matrix3 normalMeshToWorld;
        if (math::TryNormalMatrix(object.Transform, {}, normalMeshToWorld) != math::MathStatus::Success)
        {
            return Status::InvalidDescription;
        }
        math::CompensatedSum objectArea;
        for (usize primitive = 0; primitive < instance.TriangleCount; ++primitive)
        {
            internal::ReferenceTriangle& triangle = job.Scene.Triangles[triangleIndex];
            triangle.Instance = static_cast<uint32>(objectIndex);
            triangle.Primitive = static_cast<uint32>(primitive);
            for (usize corner = 0; corner < TRIANGLE_CORNERS; ++corner)
            {
                const uint32 vertex = mesh.Indices[primitive * TRIANGLE_CORNERS + corner];
                if (vertex >= mesh.Positions.GetSize())
                {
                    return Status::InvalidDescription;
                }
                triangle.Positions[corner] = math::TransformPoint(object.Transform, mesh.Positions[vertex]);
                triangle.Normals[corner] = internal::ReferenceNormalize(normalMeshToWorld * mesh.Normals[vertex]);
                triangle.Uvs[corner] = mesh.Texcoords[vertex];
                if (mesh.BakeTexcoords.IsEmpty() == false)
                {
                    float32 expectedU = 0;
                    float32 expectedV = 0;
                    if (mesh.BakeTexcoords.GetSize() != mesh.Positions.GetSize() ||
                        format::TryGetBakeChartUv(chart.Layout,
                                                  static_cast<uint32>(primitive),
                                                  static_cast<uint32>(corner),
                                                  expectedU,
                                                  expectedV) == false ||
                        math::Abs(mesh.BakeTexcoords[vertex].X - expectedU) > 1e-6F ||
                        math::Abs(mesh.BakeTexcoords[vertex].Y - expectedV) > 1e-6F)
                    {
                        return Status::Unsupported;
                    }
                }
                triangle.Colors[corner] = {1, 1, 1, 1};
                const math::Vector3 positionWorld = triangle.Positions[corner];
                if (math::IsFinite(positionWorld) == false)
                {
                    return Status::InvalidDescription;
                }
                const math::Vector3 errorWorld =
                    internal::ComputeReferencePositionError(object.Transform, mesh.Positions[vertex]);
                triangle.Error = {math::Max(triangle.Error.X, errorWorld.X),
                                  math::Max(triangle.Error.Y, errorWorld.Y),
                                  math::Max(triangle.Error.Z, errorWorld.Z)};
            }
            const math::Vector3 edgeWorld1 = triangle.Positions[1] - triangle.Positions[0];
            const math::Vector3 edgeWorld2 = triangle.Positions[2] - triangle.Positions[0];
            triangle.Area = math::Length(math::Cross(edgeWorld1, edgeWorld2)) * .5F;
            const math::Vector3 normalMesh =
                math::Cross(mesh.Positions[mesh.Indices[primitive * TRIANGLE_CORNERS + 1]] -
                                mesh.Positions[mesh.Indices[primitive * TRIANGLE_CORNERS]],
                            mesh.Positions[mesh.Indices[primitive * TRIANGLE_CORNERS + 2]] -
                                mesh.Positions[mesh.Indices[primitive * TRIANGLE_CORNERS]]);
            triangle.GeometricNormal = internal::ReferenceNormalize(normalMeshToWorld * normalMesh);
            const float32 extent = math::Max(math::Length(edgeWorld1), math::Length(edgeWorld2));
            triangle.Error = triangle.Error + math::Vector3{extent, extent, extent} * (2 * AFFINE_ERROR_ALLOWANCE);
            if (math::IsFinite(triangle.Area) == false || triangle.Area <= 0 ||
                math::Length(triangle.GeometricNormal) == 0)
            {
                return Status::InvalidDescription;
            }
            objectArea.Add(float64(triangle.Area));
            if ((surface.Emission.X > 0 || surface.Emission.Y > 0 || surface.Emission.Z > 0) &&
                job.Scene.Emitters.TryAdd(triangleIndex) == false)
            {
                return Status::OutOfMemory;
            }
            ++triangleIndex;
        }
        if (hasOverride == false && material.HasEmissionPower == true)
        {
            if (math::IsFinite(objectArea.GetResult()) == false)
            {
                return Status::CapacityExceeded;
            }
            const float32 luminance = math::Dot(material.Emission, math::Vector3{.2126F, .7152F, .0722F});
            if (luminance > 0)
            {
                surface.Emission =
                    material.Emission *
                    float32(float64(material.EmissionPower) / (float64(luminance) * math::kPi * objectArea.GetResult() *
                                                               (material.IsEmissionTwoSided == true ? 2.0 : 1.0)));
            }
        }
        if (internal::ReferenceColor(surface.Emission, 10000) == false)
        {
            return Status::CapacityExceeded;
        }
    }
    const Status bvhStatus = internal::BuildReferenceBvh(job.Scene);
    if (bvhStatus != Status::Ready)
    {
        return bvhStatus;
    }
    mStateOrNull = state.Release();
    return Status::Pending;
}
Status StaticLightingBake::Step(uint32 maxPathSamples) noexcept
{
    if (mStateOrNull == nullptr || maxPathSamples == 0)
    {
        return Status::InvalidState;
    }
    State& state = *mStateOrNull;
    if (state.Progress != Status::Pending)
    {
        return state.Progress;
    }
    for (uint32 i = 0; i < maxPathSamples && state.Triangle < state.Job.Scene.Triangles.GetSize(); ++i)
    {
        math::Vector3 irradiance;
        const Status status =
            internal::EvaluateReferenceDiffuseProbe(state.Job,
                                                    { .TriangleIndex = state.Triangle, .SampleIndex = state.Sample },
                                                    irradiance);
        if (status != Status::Ready)
        {
            state.Progress = status;
            return status;
        }
        state.Irradiance[state.Triangle].Add(irradiance);
        ++state.Sample;
        if (state.Sample == state.Description.SamplesPerTriangle)
        {
            state.Sample = 0;
            ++state.Triangle;
        }
    }
    if (state.Triangle < state.Job.Scene.Triangles.GetSize())
    {
        return Status::Pending;
    }
    for (ChartRecord& chart : state.Charts)
    {
        for (usize i = 0; i < chart.Layout.TriangleCount; ++i)
        {
            const math::Vector3 mean =
                state.Irradiance[chart.FirstTriangle + i].Value / float32(state.Description.SamplesPerTriangle);
            if (internal::ReferenceColor(mean, 10000) == false)
            {
                state.Progress = math::IsFinite(mean) == true ? Status::CapacityExceeded : Status::Failed;
                return state.Progress;
            }
            chart.Scale = {math::Max(chart.Scale.X, mean.X),
                           math::Max(chart.Scale.Y, mean.Y),
                           math::Max(chart.Scale.Z, mean.Z)};
        }
        if (internal::ReferenceColor(chart.Scale, 10000) == false)
        {
            state.Progress = Status::CapacityExceeded;
            return state.Progress;
        }
        constexpr float32 MIN_IRRADIANCE_SCALE =
            1e-6F; // Avoid zero-channel division without losing weak illumination to a unit-scale floor.
        chart.Scale = {math::Max(chart.Scale.X, MIN_IRRADIANCE_SCALE),
                       math::Max(chart.Scale.Y, MIN_IRRADIANCE_SCALE),
                       math::Max(chart.Scale.Z, MIN_IRRADIANCE_SCALE)};
        const uint32 columns = chart.Layout.Width / format::BAKE_CHART_TILE_PIXELS;
        for (usize i = 0; i < chart.Layout.TriangleCount; ++i)
        {
            const math::Vector3 mean =
                state.Irradiance[chart.FirstTriangle + i].Value / float32(state.Description.SamplesPerTriangle);
            const StaticArray<uint8, format::TEXEL_BYTES> rgba{
                static_cast<uint8>(math::Clamp(mean.X / chart.Scale.X * 255.F + .5F, 0.F, 255.F)),
                static_cast<uint8>(math::Clamp(mean.Y / chart.Scale.Y * 255.F + .5F, 0.F, 255.F)),
                static_cast<uint8>(math::Clamp(mean.Z / chart.Scale.Z * 255.F + .5F, 0.F, 255.F)),
                255};
            const usize tileX = (i % columns) * format::BAKE_CHART_TILE_PIXELS;
            const usize tileY = (i / columns) * format::BAKE_CHART_TILE_PIXELS;
            for (usize y = 0; y < format::BAKE_CHART_TILE_PIXELS; ++y)
            {
                for (usize x = 0; x < format::BAKE_CHART_TILE_PIXELS; ++x)
                {
                    const usize offset = ((tileY + y) * chart.Layout.Width + tileX + x) * format::TEXEL_BYTES;
                    for (usize channel = 0; channel < format::TEXEL_BYTES; ++channel)
                    {
                        chart.Pixels[offset + channel] = rgba[channel];
                    }
                }
            }
        }
    }
    state.Progress = Status::Ready;
    return Status::Ready;
}
Status StaticLightingBake::GetChart(usize objectIndex, StaticLightingChart& outChart) const noexcept
{
    if (mStateOrNull == nullptr)
    {
        return Status::InvalidState;
    }
    if (mStateOrNull->Progress != Status::Ready)
    {
        return mStateOrNull->Progress == Status::Pending ? Status::NotReady : mStateOrNull->Progress;
    }
    if (objectIndex >= mStateOrNull->Charts.GetSize())
    {
        return Status::InvalidDescription;
    }
    const ChartRecord& chart = mStateOrNull->Charts[objectIndex];
    outChart =
    {
        .Width = chart.Layout.Width,
        .Height = chart.Layout.Height,
        .Pixels = {chart.Pixels.GetData(), chart.Pixels.GetSize()},
        .Scale = chart.Scale,
        .UvTransform = {1, 1, 0, 0},
        .SourceId = chart.SourceId,
        .SceneRevision = mStateOrNull->Description.SceneRevision,
        .DependencyRevision = mStateOrNull->Description.DependencyRevision,
        .Transform = chart.Transform,
    };
    return Status::Ready;
}
} // namespace ludus::graphics::renderer
