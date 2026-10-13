#include "math.slang"
#include "raster_bindings.slang"
// Thanks to Google/Filament authors, Physically Based Rendering in Filament,
// sections 4.4 and 5.2.2, https://google.github.io/filament/main/filament.html.
// GGX + correlated Smith + Schlick, Fresnel-weighted Lambert, single scattering.
// Constant environment is a declared Fresnel approximation, not a DFG/LUT integrator.
struct Parameters
{
    float4 Tint;
    float4 UvTransform;
    float4 Options;
    float4 Pbr;
    float4 Emission;
    float4 EnvironmentIrradiance;
    float4 EnvironmentRadiance;
    float4 BakeScale;
    float4 BakeUv;
#ifdef LUDUS_CLUSTERED
    float4 Lights[512];
#else
    float4 Lights[32];
#endif
    float4 ShadowData[24];
#ifdef LUDUS_CLUSTERED
    float4 GridRows[3];
    float4 GridMinimum;
    float4 GridScale;
#endif
#ifdef LUDUS_SURFACE_FIDELITY
    float4 SurfaceMap;
#endif
};
LUDUS_SHADER_BINDING(0) ConstantBuffer<Parameters> material : register(b0);
LUDUS_SHADER_BINDING(1) Texture2D<float4> colorTexture : register(t1);
LUDUS_SHADER_BINDING(2) SamplerState colorSampler : register(s2);
LUDUS_SHADER_BINDING(3) Texture2D<float4> bakeTexture : register(t3);
LUDUS_SHADER_BINDING(4) Texture2D<float4> shadowTexture : register(t4);
LUDUS_SHADER_BINDING(5) SamplerState shadowSampler : register(s5);
#ifdef LUDUS_CLUSTERED
LUDUS_SHADER_BINDING(6) Texture2D<float4> clusterTexture : register(t6);
#    ifdef LUDUS_SURFACE_FIDELITY
LUDUS_SHADER_BINDING(7) Texture2D<float4> surfaceTexture : register(t7);
#    else
LUDUS_SHADER_BINDING(7) SamplerState clusterSampler : register(s7);
#    endif
#endif
#if defined(LUDUS_SURFACE_FIDELITY) && !defined(LUDUS_CLUSTERED)
LUDUS_SHADER_BINDING(6) Texture2D<float4> surfaceTexture : register(t6);
#endif
#ifdef LUDUS_SURFACE_FIDELITY
#    include "renderer_surface_functions.slang"
#endif
struct Input
{
    LUDUS_SHADER_LOCATION(0) float3 PositionMesh : POSITION;
    LUDUS_SHADER_LOCATION(1) float4 MeshToClipRow0 : TEXCOORD0;
    LUDUS_SHADER_LOCATION(2) float4 MeshToClipRow1 : TEXCOORD1;
    LUDUS_SHADER_LOCATION(3) float4 MeshToClipRow2 : TEXCOORD2;
    LUDUS_SHADER_LOCATION(4) float4 MeshToClipRow3 : TEXCOORD3;
    LUDUS_SHADER_LOCATION(5) float4 Color : COLOR0;
    LUDUS_SHADER_LOCATION(6) float2 TexcoordUv : TEXCOORD4;
    LUDUS_SHADER_LOCATION(7) float3 NormalMesh : NORMAL;
    LUDUS_SHADER_LOCATION(8) float4 MeshToWorldRow0 : TEXCOORD5;
    LUDUS_SHADER_LOCATION(9) float4 MeshToWorldRow1 : TEXCOORD6;
    LUDUS_SHADER_LOCATION(10) float4 MeshToWorldRow2 : TEXCOORD7;
    LUDUS_SHADER_LOCATION(11) float4 NormalMeshToWorldRow0 : TEXCOORD8;
    LUDUS_SHADER_LOCATION(12) float4 NormalMeshToWorldRow1 : TEXCOORD9;
    LUDUS_SHADER_LOCATION(13) float4 NormalMeshToWorldRow2 : TEXCOORD10;
    LUDUS_SHADER_LOCATION(14) float4 EyeWorld : TEXCOORD11;
    LUDUS_SHADER_LOCATION(15) float2 BakeUv : TEXCOORD12;
};
struct Varying
{
    float4 PositionClip : SV_Position;
    float2 TexcoordUv : TEXCOORD0;
    float3 PositionWorld : TEXCOORD1;
    float3 NormalWorld : TEXCOORD2;
    float4 EyeWorld : TEXCOORD3;
    float2 BakeUv : TEXCOORD4;
    float IsBakeValid : TEXCOORD5;
#ifdef LUDUS_CLUSTERED
    float IsDirectReference : TEXCOORD6;
#endif
};

[shader("vertex")] Varying VertexMain(Input input) {
    Varying output;
    float4 p = float4(input.PositionMesh, 1);
    output.PositionClip = float4(dot(input.MeshToClipRow0, p),
                                 dot(input.MeshToClipRow1, p),
                                 dot(input.MeshToClipRow2, p),
                                 dot(input.MeshToClipRow3, p));
    output.PositionWorld =
        float3(dot(input.MeshToWorldRow0, p), dot(input.MeshToWorldRow1, p), dot(input.MeshToWorldRow2, p));
    output.NormalWorld = NormalizeOrZero(float3(dot(input.NormalMeshToWorldRow0.xyz, input.NormalMesh),
                                                dot(input.NormalMeshToWorldRow1.xyz, input.NormalMesh),
                                                dot(input.NormalMeshToWorldRow2.xyz, input.NormalMesh)));
    output.TexcoordUv = input.TexcoordUv;
    output.EyeWorld = input.EyeWorld;
    output.BakeUv = input.BakeUv;
    output.IsBakeValid = input.Color.x;
#ifdef LUDUS_CLUSTERED
    output.IsDirectReference = input.Color.y;
#endif
    return output;
} float ComputeFifth(float x)
{
    float square = x * x;
    return square * square * x;
}
float3 EvaluateBrdf(float3 baseColor,
                    float3 normalWorld,
                    float3 viewDirectionWorld,
                    float3 lightDirectionWorld,
                    float metallic,
                    float roughness,
                    bool isBakedDiffuse)
{
    float nv = max(dot(normalWorld, viewDirectionWorld), 0), nl = max(dot(normalWorld, lightDirectionWorld), 0);
    float3 halfSumWorld = viewDirectionWorld + lightDirectionWorld;
    if (nv <= 0 || nl <= 0 || dot(halfSumWorld, halfSumWorld) < 1e-12)
        return float3(0);
    float3 halfDirectionWorld = NormalizeOrZero(halfSumWorld);
    float nh = saturate(dot(normalWorld, halfDirectionWorld)),
          lh = saturate(dot(lightDirectionWorld, halfDirectionWorld));
    float a = roughness * roughness, a2 = a * a;
    float d = (1 - nh * nh) + nh * nh * a2;
    float distribution = a2 / (3.141592653589793 * d * d);
    float visibility = 0.5 / max(nl * sqrt(nv * nv * (1 - a2) + a2) + nv * sqrt(nl * nl * (1 - a2) + a2), 1e-6);
    float3 f0 = lerp(float3(0.04), baseColor, metallic);
    float3 fresnel = f0 + (1 - f0) * ComputeFifth(1 - lh);
    return nl * (distribution * visibility * fresnel +
                 baseColor * (1 - metallic) * (1 - fresnel) * (isBakedDiffuse ? 0 : 1) / 3.141592653589793);
}
float ComputeShadowVisibility(int light, Varying input)
{
    if ((int(input.EyeWorld.w) & 1) == 0)
        return 1;
    for (int region = 0; region < 4; ++region)
    {
        if (region >= int(material.Pbr.w))
            break;
        float4 settings = material.ShadowData[region * 6 + 5];
        if (int(settings.y) != light)
            continue;
        float4 p = float4(input.PositionWorld, 1);
        float4 clip = float4(dot(material.ShadowData[region * 6], p),
                             dot(material.ShadowData[region * 6 + 1], p),
                             dot(material.ShadowData[region * 6 + 2], p),
                             dot(material.ShadowData[region * 6 + 3], p));
        if (clip.w <= 1e-6)
            continue;
        float3 ndc = clip.xyz / clip.w;
        if (abs(ndc.x) > 1 || abs(ndc.y) > 1 || ndc.z < 0 || ndc.z > 1)
            continue;
        float4 rect = material.ShadowData[region * 6 + 4];
        float2 uv = lerp(rect.xy, rect.zw, float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5));
        float visible = 0;
        for (int tap = 0; tap < 4; ++tap)
        {
            float2 delta = float2((tap & 1) != 0 ? 0.5 : -0.5, (tap & 2) != 0 ? 0.5 : -0.5) * settings.zw;
            float2 sampleUv = clamp(uv + delta, rect.xy, rect.zw);
            float2 depth = shadowTexture.SampleLevel(shadowSampler, sampleUv, 0).rg;
            float stored = dot(depth, float2(65280, 255)) / 65535;
            visible += ndc.z - settings.x <= stored ? 0.25 : 0;
        }
        return visible;
    }
    return 1; // Outside the declared cascade/local coverage remains unshadowed.
}
float4 SampleBaseTexel(Varying input)
{
    float2 uv = input.TexcoordUv * material.UvTransform.xy + material.UvTransform.zw;
    float4 texel = material.Options.y < 0 ? colorTexture.Sample(colorSampler, uv)
                                          : colorTexture.SampleLevel(colorSampler, uv, material.Options.y);
    float4 baseColor = texel * material.Tint;
    if (material.Options.z == 1 && baseColor.a < material.Options.x)
        discard;
    return baseColor;
}
[shader("fragment")] float4 FragmentMain(Varying input) : SV_Target
{
    float4 baseColor = SampleBaseTexel(input);
    float3 normalWorld = NormalizeOrZero(input.NormalWorld),
           viewDirectionWorld = input.EyeWorld.w >= 2 ? NormalizeOrZero(input.EyeWorld.xyz)
                                                      : NormalizeOrZero(input.EyeWorld.xyz - input.PositionWorld);
    float metallic = material.Pbr.x, roughness = material.Pbr.y;
#ifdef LUDUS_SURFACE_FIDELITY
    if (material.SurfaceMap.y > .5)
    {
        float2 uv = input.TexcoordUv * material.UvTransform.xy + material.UvTransform.zw;
        float4 texel = material.Options.y < 0 ? surfaceTexture.Sample(colorSampler, uv)
                                              : surfaceTexture.SampleLevel(colorSampler, uv, material.Options.y);
        SurfaceParameters parameters = DecodeSurfaceParameters(texel, metallic, roughness, material.SurfaceMap.x);
        metallic = parameters.Metallic;
        roughness = parameters.Roughness;
        normalWorld = ApplyNormalWorld(normalWorld,
                                       parameters.NormalTangent,
                                       ddx(input.PositionWorld),
                                       ddy(input.PositionWorld),
                                       ddx(uv),
                                       ddy(uv));
    }
#endif
    float nv = max(dot(normalWorld, viewDirectionWorld), 0);
    float3 f0 = lerp(float3(0.04), baseColor.rgb, metallic);
    float3 f = f0 + (1 - f0) * ComputeFifth(1 - nv);
    float3 irradiance = material.EnvironmentIrradiance.rgb;
    if (input.IsBakeValid > 0.5)
        irradiance =
            bakeTexture.SampleLevel(colorSampler, saturate(input.BakeUv) * material.BakeUv.xy + material.BakeUv.zw, 0)
                .rgb *
            material.BakeScale.rgb;
    float3 result = material.Emission.rgb;
    if (nv > 0)
        result += irradiance * baseColor.rgb * (1 - metallic) * (1 - f) / 3.141592653589793 +
                  material.EnvironmentRadiance.rgb * f;
#ifdef LUDUS_CLUSTERED
    float4 world = float4(input.PositionWorld, 1);
    float3 grid =
        (float3(dot(material.GridRows[0], world), dot(material.GridRows[1], world), dot(material.GridRows[2], world)) -
         material.GridMinimum.xyz) *
        material.GridScale.xyz;
    bool direct = input.IsDirectReference > 0.5 || any(grid < 0) || any(grid >= 8);
    int3 cell = int3(clamp(grid, float3(0), float3(7)));
    int start = (cell.x + cell.y * 8 + cell.z * 64) * 32;
    float4 first = clusterTexture.SampleLevel(
#    ifdef LUDUS_SURFACE_FIDELITY
        shadowSampler,
#    else
        clusterSampler,
#    endif
        (float2((start / 4) % 64, (start / 4) / 64) + 0.5) / 64,
        0);
    direct = direct || int(first.x * 255 + 0.5) == 255;
    for (int slot = 0; slot < 128; ++slot)
    {
        int i = slot;
        if (direct)
        {
            if (slot >= int(material.Pbr.z))
                break;
        }
        else
        {
            if (slot >= 32)
                break;
            int at = start + slot;
            float4 packed = clusterTexture.SampleLevel(
#    ifdef LUDUS_SURFACE_FIDELITY
                shadowSampler,
#    else
                clusterSampler,
#    endif
                (float2((at / 4) % 64, (at / 4) / 64) + 0.5) / 64,
                0);
            i = int(packed[at % 4] * 255 + 0.5) - 1;
            if (i < 0)
                break;
        }
#else
    for (int i = 0; i < 8; ++i)
    {
        if (i >= int(material.Pbr.z))
            break;
#endif
        float4 color = material.Lights[i * 4], position = material.Lights[i * 4 + 1],
               direction = material.Lights[i * 4 + 2], extra = material.Lights[i * 4 + 3];
        float3 lightDirectionWorld = -direction.xyz;
        float attenuation = 1;
        if (color.w != 0)
        {
            float3 delta = position.xyz - input.PositionWorld;
            float distance2 = dot(delta, delta);
            lightDirectionWorld = NormalizeOrZero(delta);
            float ratio = distance2 / (position.w * position.w);
            float edge = max(1 - ratio * ratio, 0);
            attenuation = edge * edge / max(distance2, 1e-4);
            if (color.w == 2)
            {
                float cone = saturate((dot(-lightDirectionWorld, direction.xyz) - direction.w) /
                                      max(extra.x - direction.w, 1e-6));
                attenuation *= cone * cone;
            }
        }
        result += EvaluateBrdf(baseColor.rgb,
                               normalWorld,
                               viewDirectionWorld,
                               lightDirectionWorld,
                               metallic,
                               roughness,
                               input.IsBakeValid > 0.5 && extra.y > 0.5) *
                  color.rgb * attenuation * ComputeShadowVisibility(i, input);
    }
    float alpha = material.Options.z == 2 ? baseColor.a : 1;
    return float4(min(max(result, float3(0)), float3(65504)) * alpha, alpha);
}
[shader("fragment")] float4 ShadowMain(Varying input) : SV_Target
{
    SampleBaseTexel(input);
    float encoded = floor(saturate(input.PositionClip.z) * 65535 + 0.5);
    float high = floor(encoded / 256);
    return float4(high / 255, (encoded - high * 256) / 255, 0, 1);
}
