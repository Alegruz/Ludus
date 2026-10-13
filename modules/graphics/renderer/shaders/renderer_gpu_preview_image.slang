#include "raster_bindings.slang"

// GPU-produced RGB/index records are instance attributes, using R4 StorageVertex.
// No fragment-stage storage binding or host image transfer is required.
LUDUS_SHADER_BINDING(0) cbuffer ImageParameters
{
    float4 ExtentPixel;
};
struct VertexInput
{
    float2 CornerPixel : POSITION0;
    float4 DisplayRgbAndIndex : COLOR0; // Encoded RGB and exact integer-valued positionPixel index.
};
struct VertexOutput
{
    float4 PositionClip : SV_Position;
    float4 Color : COLOR0;
};
VertexOutput VertexMain(VertexInput input)
{
    VertexOutput output;
    uint width = uint(ExtentPixel.x);
    float2 positionPixel =
        float2(uint(input.DisplayRgbAndIndex.w) % width, uint(input.DisplayRgbAndIndex.w) / width) + .5;
    float2 positionClip = positionPixel * float2(2 / ExtentPixel.x, -2 / ExtentPixel.y) + float2(-1, 1);
    output.PositionClip = float4(positionClip + input.CornerPixel / ExtentPixel.xy, 0, 1);
    output.Color = float4(input.DisplayRgbAndIndex.xyz, 1);
    return output;
}
float4 FragmentMain(VertexOutput input) : SV_Target0
{
    return input.Color;
}
