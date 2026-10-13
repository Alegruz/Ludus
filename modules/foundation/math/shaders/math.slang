#pragma once

// GPU counterparts of FoundationMath operations; Slang owns shader scalar spellings.
static const float PI = 3.14159265358979323846;
float3 NormalizeOrZero(float3 v)
{
    float s = dot(v, v);
    return s > 0 ? v * rsqrt(s) : float3(0);
}
bool HasPositiveComponent(float3 v)
{
    return any(v > 0);
}
void MultiplyWide(uint a, uint b, out uint outLo, out uint outHi)
{
    uint al = a & 65535;
    uint ah = a >> 16;
    uint bl = b & 65535;
    uint bh = b >> 16;
    uint t = al * bl;
    uint low = t & 65535;
    uint carry = t >> 16;
    t = ah * bl + carry;
    uint middle = t & 65535;
    outHi = t >> 16;
    t = al * bh + middle;
    outHi += ah * bh + (t >> 16);
    outLo = (t << 16) | low;
}
// Thanks to John K. Salmon et al., "Parallel Random Numbers: As Easy as 1, 2, 3",
// SC11 (2011), section 4.3, Philox4x32-10; this portable integer implementation
// shares the reference renderer's addressed dimensions, not a mutable generator.
// https://www.thesalmons.org/john/random123/papers/random123sc11.pdf
uint RandomBits(uint pixel, uint sample, uint dimension, uint2 key, uint attempt = 0)
{
    uint4 c = uint4(pixel, 0, sample, (dimension >> 2) | (attempt << 16));
    uint2 k = key;
    for (uint r = 0; r < 10; ++r)
    {
        uint a;
        uint b;
        uint x;
        uint y;
        MultiplyWide(0xd2511f53, c.x, a, b);
        MultiplyWide(0xcd9e8d57, c.z, x, y);
        c = uint4(y ^ c.y ^ k.x, x, b ^ c.w ^ k.y, a);
        k += uint2(0x9e3779b9, 0xbb67ae85);
    }
    return c[dimension & 3];
}

// UV-derived frame used by renderer raster and ray profiles; see
// docs/architecture/renderer-systems.md#material-and-lighting-fidelity.
// Orthonormalizes against the supplied unit normal, preserving mirrored UV sign.
// Numerically degenerate frames return the original normal without perturbation.
// Input vectors use Slang copy-in semantics; local edge normalization does not
// modify the caller. Do not use inout to express read-only input or avoid copies.
// Thanks to the Slang project, Language Reference, "Declarations", Parameters:
// https://docs.shader-slang.org/en/stable/external/slang/docs/language-reference/declarations.html#parameters
float3 ApplyNormalWorld(in float3 normalWorld,
                        in float3 normalTangent,
                        in float3 edgeUWorld,
                        in float3 edgeVWorld,
                        in float2 edgeUUv,
                        in float2 edgeVUv)
{
    // Normalize edge magnitudes before solving; this makes admission independent
    // of pixel footprint, triangle size and uniform UV tiling scale.
    float uvMagnitude = max(max(abs(edgeUUv.x), abs(edgeUUv.y)), max(abs(edgeVUv.x), abs(edgeVUv.y)));
    float worldMagnitude = max(max(max(abs(edgeUWorld.x), abs(edgeUWorld.y)), abs(edgeUWorld.z)),
                               max(max(abs(edgeVWorld.x), abs(edgeVWorld.y)), abs(edgeVWorld.z)));
    if (uvMagnitude <= 1e-30 || worldMagnitude <= 1e-30)
        return normalWorld;
    edgeUUv /= uvMagnitude;
    edgeVUv /= uvMagnitude;
    edgeUWorld /= worldMagnitude;
    edgeVWorld /= worldMagnitude;
    float determinant = edgeUUv.x * edgeVUv.y - edgeUUv.y * edgeVUv.x;
    if (abs(determinant) <= 1e-6 * max(abs(edgeUUv.x * edgeVUv.y), abs(edgeUUv.y * edgeVUv.x)))
        return normalWorld;
    float3 tangentWorld = (edgeUWorld * edgeVUv.y - edgeVWorld * edgeUUv.y) / determinant;
    float3 bitangentWorld = (edgeVWorld * edgeUUv.x - edgeUWorld * edgeVUv.x) / determinant;
    tangentWorld -= normalWorld * dot(normalWorld, tangentWorld);
    if (dot(tangentWorld, tangentWorld) <= 1e-20)
        return normalWorld;
    tangentWorld = NormalizeOrZero(tangentWorld);
    float handedness = dot(cross(normalWorld, tangentWorld), bitangentWorld) < 0 ? -1 : 1;
    bitangentWorld = cross(normalWorld, tangentWorld) * handedness;
    return NormalizeOrZero(tangentWorld * normalTangent.x + bitangentWorld * normalTangent.y +
                           normalWorld * normalTangent.z);
}
