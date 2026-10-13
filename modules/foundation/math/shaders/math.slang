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
    uint al = a & 65535, ah = a >> 16, bl = b & 65535, bh = b >> 16;
    uint t = al * bl, low = t & 65535, carry = t >> 16;
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
        uint a, b, x, y;
        MultiplyWide(0xd2511f53, c.x, a, b);
        MultiplyWide(0xcd9e8d57, c.z, x, y);
        c = uint4(y ^ c.y ^ k.x, x, b ^ c.w ^ k.y, a);
        k += uint2(0x9e3779b9, 0xbb67ae85);
    }
    return c[dimension & 3];
}
