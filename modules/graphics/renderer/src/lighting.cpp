#include <ludus/graphics/renderer/lighting.hpp>

#include <ludus/foundation/math/scalar.hpp>
#include <ludus/foundation/math/status.hpp>

namespace ludus::graphics::renderer
{
using namespace foundation;
namespace math = foundation::math;
namespace
{
bool Bounded(math::Vector3 value, float32 minimum, float32 maximum) noexcept
{
    return math::IsFinite(value) && value.X >= minimum && value.X <= maximum && value.Y >= minimum &&
           value.Y <= maximum && value.Z >= minimum && value.Z <= maximum;
}
float32 Fifth(float32 value) noexcept
{
    const auto square = value * value;
    return square * square * value;
}
bool NormalizeLight(DirectLight& light) noexcept
{
    return light.SourceId != 0 && static_cast<uint8>(light.Kind) <= 2 && Bounded(light.Color, 0, 1) &&
           math::IsFinite(light.Intensity) && light.Intensity >= 0 && light.Intensity <= 1000000 &&
           Bounded(light.Position, -1000000, 1000000) && math::IsFinite(light.Direction) &&
           math::IsFinite(light.Range) && light.Range >= 0.01F && light.Range <= 1000000 &&
           math::IsFinite(light.InnerCos) && math::IsFinite(light.OuterCos) && light.OuterCos > -1 &&
           light.OuterCos < light.InnerCos && light.InnerCos <= 1 &&
           (light.Kind == LightKind::Point ||
            math::TryNormalize(light.Direction, light.Direction) == math::MathStatus::Success);
}
} // namespace
rhi::RasterStatus NormalizeDirectLights(const DirectLight* lights, usize count, LightSelection& output) noexcept
{
    if (count > 128)
    {
        return rhi::RasterStatus::CapacityExceeded;
    }
    if (count != 0 && lights == nullptr)
    {
        return rhi::RasterStatus::InvalidDescription;
    }
    LightSelection result;
    for (usize i = 0; i < count; ++i)
    {
        auto light = lights[i];
        if (!NormalizeLight(light))
        {
            return rhi::RasterStatus::InvalidDescription;
        }
        usize at = 0;
        while (at < result.Count && result.Lights[at].SourceId < light.SourceId)
        {
            ++at;
        }
        if (at < result.Count && result.Lights[at].SourceId == light.SourceId)
        {
            return rhi::RasterStatus::InvalidDescription;
        }
        for (usize j = result.Count; j > at; --j)
        {
            result.Lights[j] = result.Lights[j - 1];
        }
        result.Lights[at] = light;
        ++result.Count;
    }
    output = result;
    return rhi::RasterStatus::Ready;
}
rhi::RasterStatus SelectDirectLights(const DirectLight* lights, usize count, LightSelection& output) noexcept
{
    if (count > 128)
    {
        return rhi::RasterStatus::CapacityExceeded;
    }
    if (count != 0 && lights == nullptr)
    {
        return rhi::RasterStatus::InvalidDescription;
    }
    LightSelection result;
    for (usize i = 0; i < count; ++i)
    {
        auto light = lights[i];
        if (!NormalizeLight(light))
        {
            return rhi::RasterStatus::InvalidDescription;
        }
        for (usize j = 0; j < i; ++j)
        {
            if (lights[j].SourceId == light.SourceId)
            {
                return rhi::RasterStatus::InvalidDescription;
            }
        }
        usize at = 0;
        while (at < result.Count && result.Lights[at].SourceId < light.SourceId)
        {
            ++at;
        }
        if (at < 8)
        {
            if (result.Count < 8)
            {
                ++result.Count;
            }
            for (usize j = result.Count - 1; j > at; --j)
            {
                result.Lights[j] = result.Lights[j - 1];
            }
            result.Lights[at] = light;
        }
    }
    result.Dropped = static_cast<uint32>(count) - result.Count;
    output = result;
    return rhi::RasterStatus::Ready;
}
rhi::RasterStatus BuildLightClusters(const LightClusterDescription& description,
                                     const DirectLight* lights,
                                     usize count,
                                     LightClusters& output) noexcept
{
    if (count > 128)
    {
        return rhi::RasterStatus::CapacityExceeded;
    }
    if ((count != 0 && lights == nullptr) || !math::IsFinite(description.WorldToGrid) ||
        !math::IsFinite(description.Bounds) || math::IsEmpty(description.Bounds))
    {
        return rhi::RasterStatus::InvalidDescription;
    }
    const auto extent = description.Bounds.Max - description.Bounds.Min;
    if (!Bounded(extent, 0.01F, 2000000) || !Bounded(description.Bounds.Min, -1000000, 1000000) ||
        !Bounded(description.Bounds.Max, -1000000, 1000000))
    {
        return rhi::RasterStatus::InvalidDescription;
    }
    const auto& columns = description.WorldToGrid.Columns;
    for (usize i = 0; i < 3; ++i)
    {
        for (usize j = 0; j < 3; ++j)
        {
            if (math::Abs(math::Dot(columns[i], columns[j]) - (i == j ? 1.0F : 0.0F)) > 1e-5F)
            {
                return rhi::RasterStatus::InvalidDescription;
            }
        }
    }
    LightClusters result;
    for (usize i = 0; i < count; ++i)
    {
        auto validated = lights[i];
        if (!NormalizeLight(validated))
        {
            return rhi::RasterStatus::InvalidDescription;
        }
        usize at = 0;
        while (at < result.Count && result.Lights[at].SourceId < lights[i].SourceId)
        {
            ++at;
        }
        if (at < result.Count && result.Lights[at].SourceId == lights[i].SourceId)
        {
            return rhi::RasterStatus::InvalidDescription;
        }
        for (usize j = result.Count; j > at; --j)
        {
            result.Lights[j] = result.Lights[j - 1];
        }
        result.Lights[at] = validated;
        ++result.Count;
    }
    // Thanks to Olsson, Billeter and Assarsson, "Clustered Deferred and Forward Shading",
    // High Performance Graphics 2012, pp. 87-96, doi:10.2312/EGGH/HPG12/087-096.
    // Adopt spatial grouping from the authors' abstract; this portable CPU grid uses
    // linear cells and conservative range spheres, without depth/normal pruning.
    // See docs/architecture/renderer-systems.md for capacity and overflow departures.
    for (usize cell = 0; cell < 512; ++cell)
    {
        const math::Vector3 coordinate{static_cast<float32>(cell % 8),
                                       static_cast<float32>((cell / 8) % 8),
                                       static_cast<float32>((cell / 64) % 8)};
        const auto minimum =
            description.Bounds.Min +
            math::Vector3{extent.X * coordinate.X / 8, extent.Y * coordinate.Y / 8, extent.Z * coordinate.Z / 8};
        const auto maximum = minimum + extent / 8.0F;
        for (usize i = 0; i < count; ++i)
        {
            const auto& light = result.Lights[i];
            bool intersects = light.Kind == LightKind::Directional;
            if (!intersects)
            {
                const auto center = math::TransformPoint(description.WorldToGrid, light.Position);
                if (!math::IsFinite(center))
                {
                    return rhi::RasterStatus::InvalidDescription;
                }
                const auto dx = math::Max(math::Max(minimum.X - center.X, center.X - maximum.X), 0.0F);
                const auto dy = math::Max(math::Max(minimum.Y - center.Y, center.Y - maximum.Y), 0.0F);
                const auto dz = math::Max(math::Max(minimum.Z - center.Z, center.Z - maximum.Z), 0.0F);
                // A conservative rounding margin protects tangencies and CPU/GPU cell boundaries.
                const auto magnitude =
                    math::Max(math::Max(math::Abs(center.X), math::Abs(center.Y)), math::Abs(center.Z));
                const auto translation = description.WorldToGrid.Translation;
                const auto shift =
                    math::Max(math::Max(math::Abs(translation.X), math::Abs(translation.Y)), math::Abs(translation.Z));
                const auto gridMagnitude = math::Max(
                    math::Max(math::Max(math::Abs(description.Bounds.Min.X), math::Abs(description.Bounds.Max.X)),
                              math::Max(math::Abs(description.Bounds.Min.Y), math::Abs(description.Bounds.Max.Y))),
                    math::Max(math::Abs(description.Bounds.Min.Z), math::Abs(description.Bounds.Max.Z)));
                const float64 radius = static_cast<float64>(light.Range) * 1.00002 + 0.001 +
                                       (static_cast<float64>(magnitude) + static_cast<float64>(shift) +
                                        static_cast<float64>(gridMagnitude)) *
                                           0.000004;
                intersects = static_cast<float64>(dx) * static_cast<float64>(dx) +
                                 static_cast<float64>(dy) * static_cast<float64>(dy) +
                                 static_cast<float64>(dz) * static_cast<float64>(dz) <=
                             radius * radius;
            }
            if (!intersects)
            {
                continue;
            }
            auto& written = result.Counts[cell];
            if (written < 32)
            {
                result.Texels[cell * 32 + written] = static_cast<uint8>(i + 1);
                ++written;
                ++result.Assignments;
            }
            else if (!result.Overflow[cell])
            {
                result.Overflow[cell] = true;
                ++result.OverflowCells;
            }
        }
        if (result.Overflow[cell])
        {
            result.Texels[cell * 32] = 255;
        }
    }
    output = result;
    return rhi::RasterStatus::Ready;
}
rhi::RasterStatus EvaluateLitBrdf(math::Vector3 baseColor,
                                  float32 metallic,
                                  float32 roughness,
                                  math::Vector3 normal,
                                  math::Vector3 view,
                                  math::Vector3 light,
                                  math::Vector3& output) noexcept
{
    if (!Bounded(baseColor, 0, 1) || !math::IsFinite(metallic) || metallic < 0 || metallic > 1 ||
        !math::IsFinite(roughness) || roughness < 0.045F || roughness > 1 ||
        math::TryNormalize(normal, normal) != math::MathStatus::Success ||
        math::TryNormalize(view, view) != math::MathStatus::Success ||
        math::TryNormalize(light, light) != math::MathStatus::Success)
    {
        return rhi::RasterStatus::InvalidDescription;
    }
    math::Vector3 result;
    const auto nv = math::Max(math::Dot(normal, view), 0.0F);
    const auto nl = math::Max(math::Dot(normal, light), 0.0F);
    math::Vector3 half;
    if (nv > 0 && nl > 0 && math::TryNormalize(view + light, half) == math::MathStatus::Success)
    {
        // Thanks to Google/Filament authors, "Physically Based Rendering in Filament",
        // sections 4.4.1-4.4.4 and 5.2.2, https://google.github.io/filament/main/filament.html.
        // Adopt GGX, correlated Smith visibility and Schlick; use Fresnel-weighted Lambert
        // diffuse and single scattering (no compensation/DFG LUT). CPU reference, not copied code.
        const auto nh = math::Clamp(math::Dot(normal, half), 0.0F, 1.0F);
        const auto lh = math::Clamp(math::Dot(light, half), 0.0F, 1.0F);
        const auto alpha = roughness * roughness, a2 = alpha * alpha;
        const auto denominator = nh * nh * (a2 - 1) + 1;
        const auto distribution = a2 / (math::kPiF * denominator * denominator);
        const auto visibility =
            0.5F /
            math::Max(nl * math::Sqrt(nv * nv * (1 - a2) + a2) + nv * math::Sqrt(nl * nl * (1 - a2) + a2), 1e-6F);
        const auto fresnelWeight = Fifth(1 - lh);
        const float32 colors[]{baseColor.X, baseColor.Y, baseColor.Z};
        float32 channels[3]{};
        for (usize i = 0; i < 3; ++i)
        {
            const auto f0 = 0.04F * (1 - metallic) + colors[i] * metallic;
            const auto fresnel = f0 + (1 - f0) * fresnelWeight;
            channels[i] =
                nl * (distribution * visibility * fresnel + colors[i] * (1 - metallic) * (1 - fresnel) / math::kPiF);
        }
        result = {channels[0], channels[1], channels[2]};
    }
    output = result;
    return rhi::RasterStatus::Ready;
}
rhi::RasterStatus BuildDirectionalShadowMatrix(const math::Aabb3& bounds,
                                               math::Vector3 direction,
                                               uint32 resolution,
                                               math::Matrix4& output) noexcept
{
    if (!math::IsFinite(bounds) || math::IsEmpty(bounds) || resolution < 4 || resolution > 2048 ||
        math::TryNormalize(direction, direction) != math::MathStatus::Success)
    {
        return rhi::RasterStatus::InvalidDescription;
    }
    const auto up = math::Abs(direction.Y) > 0.9F ? math::Vector3{1, 0, 0} : math::Vector3{0, 1, 0};
    math::Vector3 right;
    if (math::TryNormalize(math::Cross(up, direction), right) != math::MathStatus::Success)
    {
        return rhi::RasterStatus::InvalidDescription;
    }
    const auto vertical = math::Cross(direction, right);
    auto minimum = math::Vector3{1e30F, 1e30F, 1e30F}, maximum = -minimum;
    for (usize corner = 0; corner < 8; ++corner)
    {
        const math::Vector3 p{(corner & 1) != 0 ? bounds.Max.X : bounds.Min.X,
                              (corner & 2) != 0 ? bounds.Max.Y : bounds.Min.Y,
                              (corner & 4) != 0 ? bounds.Max.Z : bounds.Min.Z};
        const math::Vector3 projected{math::Dot(p, right), math::Dot(p, vertical), math::Dot(p, direction)};
        minimum = {math::Min(minimum.X, projected.X),
                   math::Min(minimum.Y, projected.Y),
                   math::Min(minimum.Z, projected.Z)};
        maximum = {math::Max(maximum.X, projected.X),
                   math::Max(maximum.Y, projected.Y),
                   math::Max(maximum.Z, projected.Z)};
    }
    auto width = maximum.X - minimum.X, height = maximum.Y - minimum.Y;
    if (!math::IsFinite(width) || !math::IsFinite(height) || width <= 1e-5F || height <= 1e-5F)
    {
        return rhi::RasterStatus::InvalidDescription;
    }
    const auto step = math::Max(width, height) / static_cast<float32>(resolution - 2);
    const auto cx =
        static_cast<float32>(math::Floor(static_cast<float64>((minimum.X + maximum.X) * 0.5F / step) + 0.5)) * step;
    const auto cy =
        static_cast<float32>(math::Floor(static_cast<float64>((minimum.Y + maximum.Y) * 0.5F / step) + 0.5)) * step;
    width += 2 * step;
    height += 2 * step;
    const auto near = minimum.Z - step, depth = maximum.Z - minimum.Z + 2 * step;
    if (!math::IsFinite(near) || !math::IsFinite(depth) || depth <= 0)
    {
        return rhi::RasterStatus::InvalidDescription;
    }
    math::Matrix4 matrix{};
    for (usize column = 0; column < 3; ++column)
    {
        const float32 x[]{right.X, right.Y, right.Z}, y[]{vertical.X, vertical.Y, vertical.Z},
            z[]{direction.X, direction.Y, direction.Z};
        matrix.Columns[column] = {2 * x[column] / width, 2 * y[column] / height, z[column] / depth, 0};
    }
    matrix.Columns[3] = {-2 * cx / width, -2 * cy / height, -near / depth, 1};
    if (!math::IsFinite(matrix))
    {
        return rhi::RasterStatus::InvalidDescription;
    }
    output = matrix;
    return rhi::RasterStatus::Ready;
}
} // namespace ludus::graphics::renderer
