#include "math.slang"
#include "raster_bindings.slang"
#include "renderer_gpu_preview_layout.h"
#include "renderer_surface_functions.slang"

// GPU preview ports Ludus's frozen RGB transport, not the CPU transport loop.
// Thanks to Sven Woop, Carsten Benthin and Ingo Wald, "Watertight Ray/Triangle
// Intersection", JCGT 2(1) (2013), section 3, for permuted/sheared
// triangle tests (https://jcgt.org/published/0002/01/05/paper.pdf); this FP32 BVH
// variant does not claim arbitrary thin-geometry robustness or CPU-double parity.
// Thanks to John K. Salmon et al., "Parallel Random Numbers: As Easy as 1, 2, 3",
// SC11 (2011), section 4.3, for Philox4x32-10
// (https://www.thesalmons.org/john/random123/papers/random123sc11.pdf).
// Thanks to Eric Heitz, "Sampling the GGX Distribution of Visible Normals",
// JCGT 7(4) (2018), sections 3-4/Appendix B, visible-GGX
// (https://jcgt.org/published/0007/04/01/paper.pdf), Google, "Physically Based
// Rendering in Filament", sections 4.4/5.2.2
// (https://google.github.io/filament/main/filament.html), and Matt Pharr, Wenzel
// Jakob and Greg Humphreys, "Physically Based Rendering", 4e, section 13.4
// (https://pbr-book.org/4ed/Light_Transport_I_Surface_Reflection/A_Better_Path_Tracer)
// for the shared reflection, MIS and roulette conventions. Original ports from
// reference_bsdf.cpp/reference_path.cpp; renderer-systems.md owns quality limits.
LUDUS_SHADER_BINDING(0) cbuffer Parameters
{
    column_major float4x4 ClipToWorld;
    float4 CameraPositionWorld;
    uint4 Dimensions; // width, height, depth, roulette
    uint4 Offsets;    // nodes, order, emitters, surfaces
    uint4 Counts;     // triangles, nodes, emitters, analytic lights
    uint4 Sampling;   // key lo, key hi, flags (reverse/infinite/center), target
    float4 Environment;
    float4 Options; // secondary range, exposure, reserved
    float4 BoundsMinimumWorld;
    float4 BoundsMaximumWorld;
    uint4 SurfaceLayout; // surface stride, analytic offset, private kernel ABI, see renderer_gpu_preview_layout.h,
                         // reserved
};
LUDUS_SHADER_BINDING(1) StructuredBuffer<uint4> sceneWords;
float4 SceneValue(uint index)
{
    return asfloat(sceneWords[index]);
}
LUDUS_SHADER_BINDING(2) RWStructuredBuffer<float4> accumulation; // sum/count, compensation/error
LUDUS_SHADER_BINDING(3) RWStructuredBuffer<float4> colors;       // Encoded RGB and binary32-exact pixel index.
LUDUS_SHADER_BINDING(4) StructuredBuffer<uint4> textureWords;
float Random01(uint p, uint s, uint d)
{
    return float(RandomBits(p, s, d, Sampling.xy) >> 8) * (1.0 / 16777216.0);
}
uint Choose(uint p, uint s, uint d, uint count)
{
    uint threshold = (0u - count) % count;
    for (uint retry = 0; retry < 65536; ++retry)
    {
        uint lo;
        uint hi;
        MultiplyWide(RandomBits(p, s, d, Sampling.xy, retry), count, lo, hi);
        if (lo >= threshold)
            return hi;
    }
    return 0xffffffff;
}
float3 PositionWorld(uint t, float2 uv)
{
    uint b = t * 10;
    return SceneValue(b).xyz * (1 - uv.x - uv.y) + SceneValue(b + 1).xyz * uv.x + SceneValue(b + 2).xyz * uv.y;
}
float2 TexcoordUv(uint t, float2 uv)
{
    uint b = t * 10;
    return SceneValue(b + 6).xy * (1 - uv.x - uv.y) + SceneValue(b + 6).zw * uv.x + SceneValue(b + 7).xy * uv.y;
}
float4 Surface(uint t, uint field)
{
    uint i = asuint(SceneValue(t * 10).w);
    return SceneValue(Offsets.w + i * SurfaceLayout.x + field);
}
int Address(int i, int extent, bool repeat)
{
    if (!repeat)
        return clamp(i, 0, extent - 1);
    int v = i % extent;
    return v < 0 ? v + extent : v;
}
float4 Texel(uint t, int x, int y, bool isSurfaceMap)
{
    uint4 layout = asuint(Surface(t, isSurfaceMap ? 7 : 5));
    uint2 size = isSurfaceMap ? asuint(Surface(t, 6).yz) : asuint(Surface(t, 1).zw);
    uint flags = layout.y;
    x = Address(x, int(size.x), (flags & 2) != 0);
    y = Address(y, int(size.y), (flags & 4) != 0);
    uint index = uint(y) * size.x + uint(x);
    uint word = textureWords[layout.x + (index >> 2)][index & 3];
    float4 c = float4(word & 255, (word >> 8) & 255, (word >> 16) & 255, word >> 24) / 255.0;
    if ((flags & 8) != 0)
        c.xyz = select(c.xyz > .04045, pow((c.xyz + .055) / 1.055, 2.4), c.xyz / 12.92);
    return c;
}
float4 Texture(uint t, float2 uv, bool isSurfaceMap = false)
{
    if (asuint(Surface(t, isSurfaceMap ? 6 : 4).w) == 0)
        return float4(1);
    float4 transform = Surface(t, 3);
    uv = uv * transform.xy + transform.zw;
    uint flags = asuint(Surface(t, isSurfaceMap ? 7 : 5).y);
    uint2 size = isSurfaceMap ? asuint(Surface(t, 6).yz) : asuint(Surface(t, 1).zw);
    uv.x = (flags & 2) != 0 ? frac(uv.x) : saturate(uv.x);
    uv.y = (flags & 4) != 0 ? frac(uv.y) : saturate(uv.y);
    float2 p = uv * float2(size);
    if ((flags & 1) == 0)
        return Texel(t, int(floor(p.x)), int(floor(p.y)), isSurfaceMap);
    p -= .5;
    int2 cell = int2(floor(p));
    float2 f = frac(p);
    return lerp(lerp(Texel(t, cell.x, cell.y, isSurfaceMap), Texel(t, cell.x + 1, cell.y, isSurfaceMap), f.x),
                lerp(Texel(t, cell.x, cell.y + 1, isSurfaceMap), Texel(t, cell.x + 1, cell.y + 1, isSurfaceMap), f.x),
                f.y);
}
struct Hit
{
    uint Triangle;
    float DistanceWorld;
    float2 Barycentric;
};
bool Box(float3 originWorld, float3 directionWorld, float nearDistanceWorld, float farDistanceWorld, uint node)
{
    uint b = Offsets.x + node * 3;
    float3 boundsMinimumWorld = SceneValue(b).xyz;
    float3 boundsMaximumWorld = SceneValue(b + 1).xyz;
    for (uint axis = 0; axis < 3; ++axis)
    {
        if (directionWorld[axis] == 0)
        {
            if (originWorld[axis] < boundsMinimumWorld[axis] || originWorld[axis] > boundsMaximumWorld[axis])
                return false;
        }
        else
        {
            float a = (boundsMinimumWorld[axis] - originWorld[axis]) / directionWorld[axis];
            float c = (boundsMaximumWorld[axis] - originWorld[axis]) / directionWorld[axis];
            nearDistanceWorld = max(nearDistanceWorld, min(a, c));
            farDistanceWorld = min(farDistanceWorld, max(a, c));
            if (nearDistanceWorld > farDistanceWorld)
                return false;
        }
    }
    return true;
}
bool Intersect(uint t,
               float3 originWorld,
               float3 directionWorld,
               float nearDistanceWorld,
               float farDistanceWorld,
               out float outDistanceWorld,
               out float2 outBarycentric)
{
    uint z = 0;
    if (abs(directionWorld.y) > abs(directionWorld.x))
        z = 1;
    if (abs(directionWorld.z) > abs(directionWorld[z]))
        z = 2;
    uint x = (z + 1) % 3;
    uint y = (x + 1) % 3;
    if (directionWorld[z] < 0)
    {
        uint swap = x;
        x = y;
        y = swap;
    }
    float3 a = SceneValue(t * 10).xyz - originWorld;
    float3 b = SceneValue(t * 10 + 1).xyz - originWorld;
    float3 c = SceneValue(t * 10 + 2).xyz - originWorld;
    float sx = directionWorld[x] / directionWorld[z];
    float sy = directionWorld[y] / directionWorld[z];
    float sz = 1 / directionWorld[z];
    float ax = a[x] - sx * a[z];
    float ay = a[y] - sy * a[z];
    float bx = b[x] - sx * b[z];
    float by = b[y] - sy * b[z];
    float cx = c[x] - sx * c[z];
    float cy = c[y] - sy * c[z];
    float e0 = bx * cy - by * cx;
    float e1 = cx * ay - cy * ax;
    float e2 = ax * by - ay * bx;
    float determinant = e0 + e1 + e2;
    outDistanceWorld = 0;
    outBarycentric = 0;
    if ((min(e0, min(e1, e2)) < 0 && max(e0, max(e1, e2)) > 0) || determinant == 0)
        return false;
    outDistanceWorld = (e0 * a[z] + e1 * b[z] + e2 * c[z]) * sz / determinant;
    outBarycentric = float2(e1, e2) / determinant;
    if (outDistanceWorld < nearDistanceWorld || outDistanceWorld > farDistanceWorld)
        return false;
    if (asuint(Surface(t, 5).z) != 0 && Texture(t, TexcoordUv(t, outBarycentric)).w * Surface(t, 2).w < Surface(t, 1).y)
        return false;
    return true;
}
Hit Trace(float3 originWorld,
          float3 directionWorld,
          float nearDistanceWorld,
          float farDistanceWorld,
          bool visibility = false)
{
    Hit hit;
    hit.Triangle = 0xffffffff;
    hit.DistanceWorld = farDistanceWorld;
    hit.Barycentric = 0;
    if (Counts.y == 0)
        return hit;
    uint stack[64];
    uint pending = 1;
    stack[0] = 0;
    while (pending != 0)
    {
        uint node = stack[--pending];
        uint b = Offsets.x + node * 3;
        if (!Box(originWorld, directionWorld, nearDistanceWorld, hit.DistanceWorld, node))
            continue;
        uint4 range = asuint(SceneValue(b + 2));
        if (range.y == 0)
        {
            if (pending > 62)
            {
                hit.Triangle = 0xfffffffe;
                return hit;
            }
            stack[pending++] = asuint(SceneValue(b).w);
            stack[pending++] = asuint(SceneValue(b + 1).w);
            continue;
        }
        for (uint i = range.x; i < range.x + range.y; ++i)
        {
            uint t = asuint(SceneValue(Offsets.y + i).x);
            float distance;
            float2 uv;
            if (Intersect(t, originWorld, directionWorld, nearDistanceWorld, hit.DistanceWorld, distance, uv) &&
                (distance < hit.DistanceWorld || (distance == hit.DistanceWorld && t < hit.Triangle)))
            {
                hit.Triangle = t;
                hit.DistanceWorld = distance;
                hit.Barycentric = uv;
                if (visibility)
                    return hit;
            }
        }
    }
    return hit;
}
// Thanks to Dirk Gerrit van Antwerpen, "Solving Self-Intersection Artifacts in
// DirectX Raytracing", NVIDIA Technical Blog (2023), "Error bounds":
// https://developer.nvidia.com/blog/solving-self-intersection-artifacts-in-directx-raytracing/
// Use the reference scene's conservative world-space error allowance, then the
// representable-neighbor offset convention from FoundationMath, not RTX bounds.
float3 OffsetPositionWorld(float3 positionWorld, uint t, float3 directionWorld)
{
    float3 normalWorld = SceneValue(t * 10 + 8).xyz;
    float3 positionErrorWorld = SceneValue(t * 10 + 9).xyz;
    float3 offsetWorld =
        normalWorld * dot(abs(normalWorld), positionErrorWorld) * (dot(normalWorld, directionWorld) < 0 ? -1 : 1);
    float3 offsetPositionWorld = positionWorld + offsetWorld;
    for (uint i = 0; i < 3; ++i)
        if (offsetWorld[i] != 0)
        {
            uint b = asuint(offsetPositionWorld[i]);
            if (offsetPositionWorld[i] == 0)
                b = offsetWorld[i] > 0 ? 0x00800000 : 0x80800000;
            else
                b += (offsetPositionWorld[i] > 0) == (offsetWorld[i] > 0) ? 1u : 0xffffffffu;
            offsetPositionWorld[i] = asfloat(b);
        }
    return offsetPositionWorld;
}
float PowerWeight(float a, float b)
{
    if (a <= 0)
        return 0;
    if (b <= 0)
        return 1;
    float m = max(a, b);
    a /= m;
    b /= m;
    return a * a / (a * a + b * b);
}
float AreaPdf(uint t, float3 fromPositionWorld, float3 toPositionWorld, uint lightCount)
{
    if (lightCount == 0)
        return 0;
    float3 offsetWorld = toPositionWorld - fromPositionWorld;
    float cosine = abs(dot(SceneValue(t * 10 + 8).xyz, -NormalizeOrZero(offsetWorld)));
    return cosine > 0 ? dot(offsetWorld, offsetWorld) / (float(lightCount) * SceneValue(t * 10 + 1).w * cosine) : 0;
}
void Evaluate(float3 color,
              float metallic,
              float roughness,
              float3 normalWorld,
              float3 v,
              float3 l,
              out float3 outValue,
              out float outPdf)
{
    outValue = 0;
    outPdf = 0;
    float nv = dot(normalWorld, v);
    float nl = dot(normalWorld, l);
    if (nv <= 0 || nl <= 0)
        return;
    float3 h = NormalizeOrZero(v + l);
    float nh = saturate(dot(normalWorld, h));
    float vh = saturate(dot(v, h));
    float alpha = roughness * roughness;
    float a2 = alpha * alpha;
    float denominator = (1 - nh * nh) + nh * nh * a2;
    float distribution = a2 / (PI * denominator * denominator);
    float gv = sqrt(nv * nv * (1 - a2) + a2);
    float gl = sqrt(nl * nl * (1 - a2) + a2);
    float visibility = .5 / max(nl * gv + nv * gl, 1e-6);
    float3 f0 = .04 * (1 - metallic) + color * metallic;
    float term = 1 - vh;
    float3 fresnel = f0 + (1 - f0) * term * term * term * term * term;
    outValue = fresnel * (distribution * visibility) + color * (1 - fresnel) * ((1 - metallic) / PI);
    float probability = metallic == 1 ? 1 : .5;
    outPdf = probability * distribution / (2 * (nv + gv)) + (1 - probability) * nl / PI;
}
float3 SampleBsdf(float3 normalWorld, float3 viewDirectionWorld, float roughness, float probability, float3 u)
{
    if (dot(normalWorld, viewDirectionWorld) <= 0)
        return float3(0);
    float3 x = NormalizeOrZero(cross(abs(normalWorld.z) < .999 ? float3(0, 0, 1) : float3(0, 1, 0), normalWorld));
    float3 y = cross(normalWorld, x);
    float radius = sqrt(u.y);
    float angle = 2 * PI * u.z;
    float dx = radius * cos(angle);
    float dy = radius * sin(angle);
    float3 local;
    if (u.x >= probability)
        local = float3(dx, dy, sqrt(max(0, 1 - u.y)));
    else
    {
        float3 lv =
            float3(dot(viewDirectionWorld, x), dot(viewDirectionWorld, y), dot(viewDirectionWorld, normalWorld));
        float a = roughness * roughness;
        float3 stretch = NormalizeOrZero(float3(a * lv.xy, lv.z));
        float len = dot(stretch.xy, stretch.xy);
        float3 tangent = len > 0 ? float3(-stretch.y, stretch.x, 0) / sqrt(len) : float3(1, 0, 0);
        float3 bitangent = cross(stretch, tangent);
        float blend = .5 * (1 + stretch.z);
        float wy = (1 - blend) * sqrt(max(0, 1 - dx * dx)) + blend * dy;
        float3 hemisphere = tangent * dx + bitangent * wy + stretch * sqrt(max(0, 1 - dx * dx - wy * wy));
        float3 hl = NormalizeOrZero(float3(a * hemisphere.xy, max(0, hemisphere.z)));
        float3 h = x * hl.x + y * hl.y + normalWorld * hl.z;
        return NormalizeOrZero(h * (2 * dot(viewDirectionWorld, h)) - viewDirectionWorld);
    }
    return NormalizeOrZero(x * local.x + y * local.y + normalWorld * local.z);
}
float3 Integrate(uint pixel, uint sample)
{
    if (SurfaceLayout.x != LUDUS_GPU_PREVIEW_SURFACE_WORDS || SurfaceLayout.z != LUDUS_GPU_PREVIEW_KERNEL_VERSION)
        return float3(-1);
    float2 jitter = (Sampling.z & 4) != 0 ? float2(.5) : float2(Random01(pixel, sample, 0), Random01(pixel, sample, 1));
    float2 positionUv = (float2(pixel % Dimensions.x, pixel / Dimensions.x) + jitter) / float2(Dimensions.xy);
    float nearClipZ = (Sampling.z & 1) != 0 ? 1 : 0;
    float4 nearPositionWorld = mul(ClipToWorld, float4(positionUv.x * 2 - 1, 1 - positionUv.y * 2, nearClipZ, 1));
    nearPositionWorld /= nearPositionWorld.w;
    float4 farPositionWorld = mul(ClipToWorld, float4(positionUv.x * 2 - 1, 1 - positionUv.y * 2, 1 - nearClipZ, 1));
    // Match RT1's farPositionWorld-plane/homogeneous shadowDirectionWorld, including non-default pinholes.
    float3 originWorld = CameraPositionWorld.w != 0 ? nearPositionWorld.xyz : CameraPositionWorld.xyz;
    float3 directionWorld = NormalizeOrZero(
        farPositionWorld.w != 0 ? farPositionWorld.xyz / farPositionWorld.w - originWorld : farPositionWorld.xyz);
    float minimum = CameraPositionWorld.w != 0 ? 0 : dot(nearPositionWorld.xyz - originWorld, directionWorld);
    float maximum = Options.x;
    if (farPositionWorld.w != 0)
        maximum = dot(farPositionWorld.xyz / farPositionWorld.w - originWorld, directionWorld);
    else
    {
        maximum = 0;
        for (uint corner = 0; corner < 8; ++corner)
        {
            float3 boundsCornerWorld = float3((corner & 1) != 0 ? BoundsMaximumWorld.x : BoundsMinimumWorld.x,
                                              (corner & 2) != 0 ? BoundsMaximumWorld.y : BoundsMinimumWorld.y,
                                              (corner & 4) != 0 ? BoundsMaximumWorld.z : BoundsMinimumWorld.z);
            maximum = max(maximum, length(boundsCornerWorld - originWorld));
        }
        maximum = maximum * 1.000001 + 1;
    }
    float3 radiance = 0;
    float3 throughput = 1;
    float3 previousPositionWorld = 0;
    float previousPdf = 0;
    uint lights = Counts.w + Counts.z + (HasPositiveComponent(Environment.xyz) ? 1u : 0u);
    for (uint depth = 0; depth <= Dimensions.z; ++depth)
    {
        Hit hit = Trace(originWorld, directionWorld, minimum, maximum);
        if (hit.Triangle == 0xfffffffe)
            return float3(-1);
        if (hit.Triangle == 0xffffffff)
        {
            float w = depth == 0 ? 1 : PowerWeight(previousPdf, lights > 0 ? 1 / (float(lights) * 4 * PI) : 0);
            radiance += throughput * Environment.xyz * w;
            break;
        }
        uint t = hit.Triangle;
        float3 positionWorld = PositionWorld(t, hit.Barycentric);
        float3 geometricNormalWorld = SceneValue(t * 10 + 8).xyz;
        float3 viewDirectionWorld = -directionWorld;
        float3 emission = Surface(t, 4).xyz;
        radiance +=
            throughput * emission *
            (depth == 0 ? 1 : PowerWeight(previousPdf, AreaPdf(t, previousPositionWorld, positionWorld, lights)));
        if (depth >= Dimensions.z || dot(geometricNormalWorld, viewDirectionWorld) <= 0)
            break;
        float3 normalWorld = NormalizeOrZero(SceneValue(t * 10 + 3).xyz * (1 - hit.Barycentric.x - hit.Barycentric.y) +
                                             SceneValue(t * 10 + 4).xyz * hit.Barycentric.x +
                                             SceneValue(t * 10 + 5).xyz * hit.Barycentric.y);
        float3 color = saturate(Surface(t, 0).xyz * Texture(t, TexcoordUv(t, hit.Barycentric)).xyz);
        float metallic = Surface(t, 0).w;
        float roughness = Surface(t, 1).x;
        if (asuint(Surface(t, 6).w) != 0)
        {
            SurfaceParameters parameters = DecodeSurfaceParameters(Texture(t, TexcoordUv(t, hit.Barycentric), true),
                                                                   metallic,
                                                                   roughness,
                                                                   Surface(t, 6).x);
            metallic = parameters.Metallic;
            roughness = parameters.Roughness;
            float2 uvScale = Surface(t, 3).xy;
            normalWorld = ApplyNormalWorld(normalWorld,
                                           parameters.NormalTangent,
                                           SceneValue(t * 10 + 1).xyz - SceneValue(t * 10).xyz,
                                           SceneValue(t * 10 + 2).xyz - SceneValue(t * 10).xyz,
                                           (SceneValue(t * 10 + 6).zw - SceneValue(t * 10 + 6).xy) * uvScale,
                                           (SceneValue(t * 10 + 7).xy - SceneValue(t * 10 + 6).xy) * uvScale);
        }
        uint dimension = 2 + depth * 8;
        if (lights > 0)
        {
            uint selected = Choose(pixel, sample, dimension, lights);
            if (selected == 0xffffffff)
                return float3(-1);
            float3 lightDirectionWorld;
            float3 lightColor = 0;
            float3 lightPositionWorld = 0;
            float pdf = 0;
            float range = Options.x;
            uint emitter = 0xffffffff;
            bool isDelta = false;
            bool hasFiniteEndpoint = false;
            if (selected < Counts.w)
            {
                uint light = SurfaceLayout.y + selected * LUDUS_GPU_PREVIEW_LIGHT_WORDS;
                float4 colorAndKind = SceneValue(light);
                float4 positionAndRangeWorld = SceneValue(light + 1);
                float4 directionAndConeWorld = SceneValue(light + 2);
                lightColor = colorAndKind.xyz;
                lightDirectionWorld = -directionAndConeWorld.xyz;
                pdf = 1 / float(lights);
                isDelta = true;
                if (colorAndKind.w != 0)
                {
                    lightPositionWorld = positionAndRangeWorld.xyz;
                    float3 offsetWorld = lightPositionWorld - positionWorld;
                    float distanceSquared = dot(offsetWorld, offsetWorld);
                    if (distanceSquared <= 0)
                        return float3(-1);
                    range = sqrt(distanceSquared);
                    lightDirectionWorld = offsetWorld / range;
                    hasFiniteEndpoint = true;
                    float ratio = range / positionAndRangeWorld.w;
                    float window = max(1 - ratio * ratio * ratio * ratio, 0);
                    float angular = 1;
                    if (colorAndKind.w == 2)
                    {
                        float cone =
                            saturate((dot(-lightDirectionWorld, directionAndConeWorld.xyz) - directionAndConeWorld.w) /
                                     (SceneValue(light + 3).x - directionAndConeWorld.w));
                        angular = cone * cone;
                    }
                    lightColor *= range <= Options.x ? window * window * angular / max(distanceSquared, 1e-4) : 0;
                }
            }
            else if (selected - Counts.w < Counts.z)
            {
                emitter = asuint(SceneValue(Offsets.z + selected - Counts.w).x);
                float root = sqrt(Random01(pixel, sample, dimension + 1));
                float2 uv = float2(root * (1 - Random01(pixel, sample, dimension + 2)), 0);
                uv.y = root - uv.x;
                lightPositionWorld = PositionWorld(emitter, uv);
                float3 offsetWorld = lightPositionWorld - positionWorld;
                range = length(offsetWorld);
                lightDirectionWorld = NormalizeOrZero(offsetWorld);
                pdf = AreaPdf(emitter, positionWorld, lightPositionWorld, lights);
                if (range <= Options.x &&
                    (asuint(Surface(emitter, 5).z) == 0 ||
                     Texture(emitter, TexcoordUv(emitter, uv)).w * Surface(emitter, 2).w >= Surface(emitter, 1).y))
                    lightColor = Surface(emitter, 4).xyz;
            }
            else
            {
                float z = 1 - 2 * Random01(pixel, sample, dimension + 1);
                float r = sqrt(max(0, 1 - z * z));
                float a = 2 * PI * Random01(pixel, sample, dimension + 2);
                lightDirectionWorld = float3(r * cos(a), r * sin(a), z);
                pdf = 1 / (float(lights) * 4 * PI);
                lightColor = Environment.xyz;
            }
            float3 value;
            float bsdfPdf;
            Evaluate(color, metallic, roughness, normalWorld, viewDirectionWorld, lightDirectionWorld, value, bsdfPdf);
            if (pdf > 0 && HasPositiveComponent(lightColor) && dot(normalWorld, lightDirectionWorld) > 0 &&
                dot(geometricNormalWorld, lightDirectionWorld) > 0)
            {
                float3 shadowOriginWorld = OffsetPositionWorld(positionWorld, t, lightDirectionWorld);
                float3 shadowDirectionWorld = lightDirectionWorld;
                float distance = range;
                if (emitter != 0xffffffff)
                {
                    float3 shadowEndWorld = OffsetPositionWorld(lightPositionWorld, emitter, -lightDirectionWorld);
                    float3 offsetWorld = shadowEndWorld - shadowOriginWorld;
                    distance = length(offsetWorld);
                    shadowDirectionWorld = NormalizeOrZero(offsetWorld);
                    distance = asfloat(asuint(distance) - 1);
                }
                else if (hasFiniteEndpoint)
                {
                    float3 offsetWorld = lightPositionWorld - shadowOriginWorld;
                    distance = length(offsetWorld);
                    shadowDirectionWorld = NormalizeOrZero(offsetWorld);
                    distance = asfloat(asuint(distance) - 1);
                }
                Hit shadow = Trace(shadowOriginWorld, shadowDirectionWorld, 0, distance, true);
                if (shadow.Triangle == 0xfffffffe)
                    return float3(-1);
                if (shadow.Triangle == 0xffffffff)
                    radiance +=
                        throughput * value * lightColor *
                        (dot(normalWorld, lightDirectionWorld) * (isDelta ? 1 : PowerWeight(pdf, bsdfPdf)) / pdf);
            }
        }
        float3 outgoingDirectionWorld = SampleBsdf(normalWorld,
                                                   viewDirectionWorld,
                                                   roughness,
                                                   metallic == 1 ? 1 : .5,
                                                   float3(Random01(pixel, sample, dimension + 3),
                                                          Random01(pixel, sample, dimension + 4),
                                                          Random01(pixel, sample, dimension + 5)));
        if (dot(normalWorld, outgoingDirectionWorld) <= 0 || dot(geometricNormalWorld, outgoingDirectionWorld) <= 0)
            break;
        float3 value;
        float pdf;
        Evaluate(color, metallic, roughness, normalWorld, viewDirectionWorld, outgoingDirectionWorld, value, pdf);
        if (pdf <= 0)
            break;
        throughput *= value * (dot(normalWorld, outgoingDirectionWorld) / pdf);
        if (!HasPositiveComponent(throughput))
            break;
        if (depth + 1 >= Dimensions.w)
        {
            float keep = clamp(max(throughput.x, max(throughput.y, throughput.z)), .05, .95);
            if (Random01(pixel, sample, dimension + 6) >= keep)
                break;
            throughput /= keep;
        }
        previousPdf = pdf;
        previousPositionWorld = positionWorld;
        originWorld = OffsetPositionWorld(positionWorld, t, outgoingDirectionWorld);
        directionWorld = outgoingDirectionWorld;
        minimum = 0;
        maximum = Options.x;
    }
    return radiance;
}
[numthreads(64, 1, 1)] void ComputeMain(uint3 id
                                        : SV_DispatchThreadID) {
    if (id.x >= Dimensions.x || id.y >= Dimensions.y)
        return;
    uint pixel = id.y * Dimensions.x + id.x;
    float4 sum = accumulation[pixel * 2];
    float4 compensation = accumulation[pixel * 2 + 1];
    if (sum.w < float(Sampling.w) && compensation.w == 0)
    {
        float3 sample = Integrate(pixel, uint(sum.w));
        if (any(!isfinite(sample)) || any(sample < 0))
            compensation.w = 1;
        else
        {
            float3 adjusted = sample - compensation.xyz;
            float3 next = sum.xyz + adjusted;
            compensation.xyz = (next - sum.xyz) - adjusted;
            if (any(!isfinite(next)) || any(!isfinite(compensation.xyz)))
                compensation.w = 1;
            else
            {
                sum.xyz = next;
                sum.w += 1;
            }
        }
        accumulation[pixel * 2] = sum;
        accumulation[pixel * 2 + 1] = compensation;
    }
    float3 mean = sum.w > 0 ? sum.xyz / sum.w : float3(0);
    float3 linear = mean * Options.y;
    for (uint channel = 0; channel < 3; ++channel)
    {
        // Finite nonnegative radiance can overflow when exposure is applied.
        // Reinhard tends to one there; do not encode an infinity/infinity NaN.
        linear[channel] = isfinite(linear[channel]) ? linear[channel] / (1 + linear[channel]) : 1;
    }
    float3 encoded = select(linear > .0031308, 1.055 * pow(linear, 1.0 / 2.4) - .055, linear * 12.92);
    colors[pixel] = float4(compensation.w != 0 ? float3(1, 0, 1) : saturate(encoded), float(pixel));
}
