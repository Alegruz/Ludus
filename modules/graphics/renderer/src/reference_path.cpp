#include "internal/reference_data.h"

#include <ludus/foundation/math/status.hpp>

// Thanks to Matt Pharr, Wenzel Jakob and Greg Humphreys, "Physically Based
// Rendering: From Theory to Implementation", 4th edition (2023), section 13.4,
// https://pbr-book.org/4ed/Light_Transport_I_Surface_Reflection/A_Better_Path_Tracer:
// retain one light proposal/continuation, same-measure power MIS for both
// emitter/environment hits and next-event estimation, and compensated roulette.
// This RGB, fixed-mip, finite-range reference uses CPU transport with explicit
// asynchronous native query batches; it has no denoising, wavefront scheduler,
// path regularization, spectral/transmission model or hidden frame readback.
namespace ludus::graphics::renderer::internal
{
namespace
{
bool Positive(math::Vector3 value) noexcept
{
    return value.X > 0 || value.Y > 0 || value.Z > 0;
}
bool ValidContribution(math::Vector3 value) noexcept
{
    return math::IsFinite(value) && value.X >= 0 && value.Y >= 0 && value.Z >= 0;
}
rhi::RasterStatus Invalid(ReferenceJob& job, const ReferencePath& path) noexcept
{
    ++job.Info.InvalidContributions;
    job.Info.FailurePixel = path.Pixel;
    job.Info.FailureDepth = path.Depth;
    return rhi::RasterStatus::Failed;
}
void AddTransport(ReferencePath& path, math::Vector3 contribution, uint32 depth, bool emission) noexcept
{
    if (emission && depth == 0)
    {
        path.Emission = path.Emission + contribution;
    }
    else if ((emission && depth == 1) || (!emission && depth == 0))
    {
        path.Direct = path.Direct + contribution;
    }
    else
    {
        path.Indirect = path.Indirect + contribution;
    }
}
usize LightCount(const ReferenceJob& job) noexcept
{
    return job.Description.LightCount + job.Scene.Emitters.GetSize() +
           (Positive(job.Description.Environment) ? 1U : 0U);
}
math::Vector3 Position(const ReferenceTriangle& triangle, float32 u, float32 v) noexcept
{
    return triangle.Positions[0] * (1 - u - v) + triangle.Positions[1] * u + triangle.Positions[2] * v;
}
math::Vector2 Uv(const ReferenceTriangle& triangle, float32 u, float32 v) noexcept
{
    return triangle.Uvs[0] * (1 - u - v) + triangle.Uvs[1] * u + triangle.Uvs[2] * v;
}
float32 AreaPdf(const ReferenceTriangle& triangle, math::Vector3 from, math::Vector3 point, usize lights) noexcept
{
    const auto delta = point - from;
    const auto distanceSquared = math::Dot(delta, delta);
    const auto direction = ReferenceNormalize(delta);
    const auto cosine = math::Abs(math::Dot(triangle.GeometricNormal, -direction));
    if (lights == 0 || triangle.Area <= 0 || cosine <= 0)
    {
        return 0;
    }
    return distanceSquared / (float32(lights) * triangle.Area * cosine);
}
math::Vector3 Offset(math::Vector3 point, const ReferenceTriangle& triangle, math::Vector3 direction) noexcept
{
    math::Vector3 result = point;
    if (math::TryOffsetRayOrigin({point, triangle.Error, triangle.GeometricNormal, direction}, result) !=
        math::MathStatus::Success)
    {
        return {__builtin_nanf(""), 0, 0};
    }
    return result;
}
struct LightProposal final
{
    math::Vector3 Direction{}, Radiance{}, Point{}, Error{}, Normal{};
    float32 Distance = 0, Pdf = 0;
    bool Delta = false, FiniteEndpoint = false;
};
LightProposal SampleLight(const ReferenceJob& job, const ReferencePath& path, math::Vector3 point) noexcept
{
    LightProposal result;
    const auto count = LightCount(job);
    if (count == 0)
    {
        return result;
    }
    const auto dimension = static_cast<uint16>(2 + path.Depth * 8);
    // Use the owning unbiased bounded mapper for discrete emitter selection;
    // float multiplication/truncation would give unequal selector probabilities.
    uint32 selected = 0;
    if (math::TrySampleBounded(job.Key, {path.Pixel, path.Sample, dimension}, static_cast<uint32>(count), selected) !=
        math::MathStatus::Success)
    {
        result.Pdf = -1;
        return result;
    }
    if (selected < job.Description.LightCount)
    {
        const auto& light = job.Lights[selected];
        result.Delta = true;
        result.Pdf = 1 / float32(count);
        result.Radiance = light.Color * light.Intensity;
        if (light.Kind == LightKind::Directional)
        {
            result.Direction = -light.Direction;
            result.Distance = job.Description.SecondaryDistance;
        }
        else
        {
            const auto delta = light.Position - point;
            const auto square = math::Dot(delta, delta);
            if (square <= 0)
            {
                result.Pdf = -1;
                return result;
            }
            result.Distance = math::Sqrt(square);
            result.Direction = delta / result.Distance;
            result.Point = light.Position;
            result.FiniteEndpoint = true;
            if (result.Distance >= light.Range || result.Distance > job.Description.SecondaryDistance)
            {
                result.Radiance = {};
                return result;
            }
            const auto ratio = result.Distance / light.Range;
            const auto window = math::Max(0.0F, 1 - ratio * ratio * ratio * ratio);
            float32 angular = 1;
            if (light.Kind == LightKind::Spot)
            {
                const auto cosine = math::Dot(light.Direction, -result.Direction);
                const auto t = math::Clamp((cosine - light.OuterCos) / (light.InnerCos - light.OuterCos), 0.0F, 1.0F);
                angular = t * t;
            }
            result.Radiance = result.Radiance * (window * window * angular / math::Max(square, 1e-4F));
        }
        return result;
    }
    const auto emitter = usize(selected) - job.Description.LightCount;
    if (emitter < job.Scene.Emitters.GetSize())
    {
        const auto& triangle = job.Scene.Triangles[job.Scene.Emitters[emitter]];
        const auto root = math::Sqrt(ReferenceRandom(job, path, static_cast<uint16>(dimension + 1)));
        const auto u = root * (1 - ReferenceRandom(job, path, static_cast<uint16>(dimension + 2)));
        const auto v = root - u;
        result.Point = Position(triangle, u, v);
        const auto delta = result.Point - point;
        result.Distance = math::Length(delta);
        if (result.Distance <= 0)
        {
            result.Pdf = -1;
            return result;
        }
        result.Direction = delta / result.Distance;
        result.Error = triangle.Error;
        result.Normal = triangle.GeometricNormal;
        result.FiniteEndpoint = true;
        result.Pdf = AreaPdf(triangle, point, result.Point, count);
        const auto& instance = job.Scene.Instances[triangle.Instance];
        if (result.Distance <= job.Description.SecondaryDistance && ReferenceOpacity(instance, Uv(triangle, u, v)))
        {
            result.Radiance = instance.Surface->Emission;
        }
        return result;
    }
    const auto z = 1 - 2 * ReferenceRandom(job, path, static_cast<uint16>(dimension + 1));
    const auto radius = math::Sqrt(math::Max(0.0F, 1 - z * z));
    const auto angle = math::kTauF * ReferenceRandom(job, path, static_cast<uint16>(dimension + 2));
    result.Direction = {radius * math::Cos(angle), radius * math::Sin(angle), z};
    result.Distance = job.Description.SecondaryDistance;
    result.Radiance = job.Description.Environment;
    result.Pdf = 1 / (float32(count) * 4 * math::kPiF);
    return result;
}
} // namespace
rhi::RasterStatus PrepareReferenceVertex(ReferenceJob& job, ReferencePath& path, const rhi::RayResult& hit) noexcept
{
    path.HasShadow = path.HasNext = false;
    path.Active = false;
    const auto lights = LightCount(job);
    if (hit.Hit == 0)
    {
        const auto lightPdf = lights == 0 ? 0 : 1 / (float32(lights) * 4 * math::kPiF);
        const auto weight = path.Depth == 0 ? 1 : ReferencePowerWeight(path.PreviousPdf, lightPdf);
        AddTransport(path, math::Hadamard(path.Throughput, job.Description.Environment) * weight, path.Depth, true);
        return ValidContribution(path.Emission) && ValidContribution(path.Direct) && ValidContribution(path.Indirect)
                   ? rhi::RasterStatus::Ready
                   : Invalid(job, path);
    }
    if (hit.Hit != 1 || hit.Instance >= job.Scene.InstanceCount || !math::IsFinite(hit.Distance) ||
        !math::IsFinite(hit.U) || !math::IsFinite(hit.V) || hit.Distance < path.Ray.MinDistance ||
        hit.Distance > path.Ray.MaxDistance || hit.U < -1e-5F || hit.V < -1e-5F || hit.U + hit.V > 1.00001F)
    {
        return Invalid(job, path);
    }
    const auto& instance = job.Scene.Instances[hit.Instance];
    if (hit.Primitive >= instance.TriangleCount)
    {
        return Invalid(job, path);
    }
    const auto& triangle = job.Scene.Triangles[instance.FirstTriangle + hit.Primitive];
    const auto point = Position(triangle, hit.U, hit.V);
    const auto view = -ReferenceDirection(path.Ray);
    const auto& surface = *instance.Surface;
    if (Positive(surface.Emission))
    {
        const auto pdf = AreaPdf(triangle, path.PreviousPoint, point, lights);
        if (!math::IsFinite(pdf) || pdf < 0)
        {
            return Invalid(job, path);
        }
        const auto weight = path.Depth == 0 ? 1 : ReferencePowerWeight(path.PreviousPdf, pdf);
        AddTransport(path, math::Hadamard(path.Throughput, surface.Emission) * weight, path.Depth, true);
    }
    if (path.Depth >= job.Description.MaxDepth || math::Dot(triangle.GeometricNormal, view) <= 0)
    {
        return ValidContribution(path.Emission) && ValidContribution(path.Direct) && ValidContribution(path.Indirect)
                   ? rhi::RasterStatus::Ready
                   : Invalid(job, path);
    }
    math::Vector3 normal = ReferenceNormalize(triangle.Normals[0] * (1 - hit.U - hit.V) + triangle.Normals[1] * hit.U +
                                              triangle.Normals[2] * hit.V);
    const auto texel = SampleReferenceTexture(instance, Uv(triangle, hit.U, hit.V));
    ReferenceBsdf bsdf = surface.Bsdf;
    bsdf.BaseColor = math::Hadamard({texel.X, texel.Y, texel.Z}, bsdf.BaseColor);
    if (surface.HasSurfaceMap)
    {
        SurfaceParameters parameters;
        if (DecodeSurfaceParameters(SampleReferenceSurfaceMap(instance, Uv(triangle, hit.U, hit.V)),
                                    bsdf.Metallic,
                                    bsdf.Roughness,
                                    surface.NormalScale,
                                    parameters) != rhi::RasterStatus::Ready)
        {
            return Invalid(job, path);
        }
        bsdf.Metallic = parameters.Metallic;
        bsdf.Roughness = parameters.Roughness;
        normal = ApplyReferenceNormal(triangle, surface, normal, parameters.NormalTangent);
    }
    const auto proposal = SampleLight(job, path, point);
    if (!math::IsFinite(proposal.Pdf) || proposal.Pdf < 0 || !ValidContribution(proposal.Radiance))
    {
        return Invalid(job, path);
    }
    const auto cosine = math::Dot(normal, proposal.Direction);
    if (proposal.Pdf > 0 && Positive(proposal.Radiance) && cosine > 0 &&
        math::Dot(triangle.GeometricNormal, proposal.Direction) > 0)
    {
        ReferenceBsdfEvaluation evaluation;
        if (EvaluateReferenceBsdf(bsdf, normal, view, proposal.Direction, evaluation) != rhi::RasterStatus::Ready)
        {
            return Invalid(job, path);
        }
        const auto weight = proposal.Delta ? 1 : ReferencePowerWeight(proposal.Pdf, evaluation.Pdf);
        path.PendingDirect = math::Hadamard(path.Throughput, math::Hadamard(evaluation.Value, proposal.Radiance)) *
                             (cosine * weight / proposal.Pdf);
        if (!ValidContribution(path.PendingDirect))
        {
            return Invalid(job, path);
        }
        if (Positive(path.PendingDirect))
        {
            const auto start = Offset(point, triangle, proposal.Direction);
            auto direction = proposal.Direction;
            auto distance = proposal.Distance;
            if (proposal.FiniteEndpoint)
            {
                auto end = proposal.Point;
                if (!proposal.Delta &&
                    math::TryOffsetRayOrigin({proposal.Point, proposal.Error, proposal.Normal, -direction}, end) !=
                        math::MathStatus::Success)
                {
                    return Invalid(job, path);
                }
                const auto delta = end - start;
                distance = math::Length(delta);
                if (distance <= 0 || !math::IsFinite(distance))
                {
                    return Invalid(job, path);
                }
                direction = delta / distance;
                // The target endpoint is excluded by one representable step.
                // Light geometry must not occlude its own sampled emission.
                distance = math::NextFloatDown(distance);
            }
            path.Shadow = ReferenceRay(start, direction, distance);
            path.Shadow.VisibilityOnly = 1;
            path.DirectDepth = path.Depth;
            path.HasShadow = true;
        }
    }
    const auto dimension = static_cast<uint16>(2 + path.Depth * 8);
    ReferenceBsdfSample sample;
    if (SampleReferenceBsdf(bsdf,
                            normal,
                            view,
                            {ReferenceRandom(job, path, static_cast<uint16>(dimension + 3)),
                             ReferenceRandom(job, path, static_cast<uint16>(dimension + 4)),
                             ReferenceRandom(job, path, static_cast<uint16>(dimension + 5))},
                            sample) != rhi::RasterStatus::Ready)
    {
        return Invalid(job, path);
    }
    if (!sample.Accepted || math::Dot(triangle.GeometricNormal, sample.Direction) <= 0)
    {
        ++job.Info.NullBsdfSamples;
        return rhi::RasterStatus::Ready;
    }
    path.Throughput = math::Hadamard(path.Throughput, sample.Evaluation.Value) *
                      (math::Dot(normal, sample.Direction) / sample.Evaluation.Pdf);
    if (!ValidContribution(path.Throughput))
    {
        return Invalid(job, path);
    }
    if (!Positive(path.Throughput))
    {
        return rhi::RasterStatus::Ready;
    }
    path.PreviousPoint = point;
    path.PreviousPdf = sample.Evaluation.Pdf;
    ++path.Depth;
    if (path.Depth >= job.Description.RouletteDepth)
    {
        const auto survival =
            math::Clamp(math::Max(math::Max(path.Throughput.X, path.Throughput.Y), path.Throughput.Z), 0.05F, 0.95F);
        if (ReferenceRandom(job, path, static_cast<uint16>(dimension + 6)) >= survival)
        {
            ++job.Info.RouletteTerminations;
            return rhi::RasterStatus::Ready;
        }
        path.Throughput = path.Throughput / survival;
    }
    const auto origin = Offset(point, triangle, sample.Direction);
    if (!math::IsFinite(origin))
    {
        return Invalid(job, path);
    }
    path.Next = ReferenceRay(origin, sample.Direction, job.Description.SecondaryDistance);
    path.HasNext = path.Active = true;
    return ValidContribution(path.Emission) && ValidContribution(path.Direct) && ValidContribution(path.Indirect)
               ? rhi::RasterStatus::Ready
               : Invalid(job, path);
}
} // namespace ludus::graphics::renderer::internal
