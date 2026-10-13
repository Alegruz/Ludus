#pragma once
// Thanks to Google/Filament, Physically Based Rendering in Filament, section 5.7,
// https://google.github.io/filament/main/filament.html: tangent-space surface detail.
// Ludus reconstructs positive Z from RG and uses a UV-derived frame rather than
// baked mesh tangents. This is a declared RGB preview profile, not MikkTSpace.
struct SurfaceParameters
{
    float3 NormalTangent;
    float Metallic;
    float Roughness;
};
SurfaceParameters DecodeSurfaceParameters(float4 texel, float metallic, float roughness, float normalScale)
{
    float2 xy = texel.xy * 2 - 1;
    float square = dot(xy, xy);
    if (square > 1)
        xy /= sqrt(square);
    SurfaceParameters result;
    result.NormalTangent =
        normalScale == 0 ? float3(0, 0, 1) : NormalizeOrZero(float3(xy * normalScale, sqrt(max(0, 1 - square))));
    result.Metallic = metallic * texel.w;
    result.Roughness = max(.045, roughness * texel.z);
    return result;
}
