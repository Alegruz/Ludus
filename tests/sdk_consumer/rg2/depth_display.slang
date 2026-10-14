#include "raster_bindings.slang"

LUDUS_SHADER_BINDING(0) Texture2D<float> DepthImage : register(t0);
struct Input
{
    LUDUS_SHADER_LOCATION(0) float4 PositionClip : POSITION;
};
struct Varying
{
    float4 PositionClip : SV_Position;
};
[shader("vertex")]
Varying VSMain(Input value)
{
    Varying result;
    result.PositionClip = value.PositionClip;
    return result;
}
[shader("fragment")]
float4 PSMain(in float4 positionPixel : SV_Position) : SV_Target
{
    float depthClip = DepthImage.Load(int3(int2(positionPixel.xy), 0));
    return float4(depthClip, depthClip, depthClip, 1);
}
