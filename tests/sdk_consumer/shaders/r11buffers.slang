#include "raster_bindings.slang"

struct Settings
{
    float4 Color;
};
LUDUS_SHADER_BINDING(0) ConstantBuffer<Settings> Colors[4] : register(b0);
struct Input
{
    LUDUS_SHADER_LOCATION(0) float2 PositionClip : POSITION;
};
struct Varying
{
    float4 PositionClip : SV_Position;
    float2 Uv : TEXCOORD0;
};
[shader("vertex")] Varying VertexMain(Input input) {
    Varying output;
    output.PositionClip = float4(input.PositionClip, 0, 1);
    output.Uv = input.PositionClip * float2(0.5, -0.5) + 0.5;
    return output;
}[shader("fragment")] float4 FragmentMain(Varying input)
    : SV_Target
{
    return input.Uv.x < 0.25   ? Colors[0].Color
           : input.Uv.x < 0.5  ? Colors[1].Color
           : input.Uv.x < 0.75 ? Colors[2].Color
                               : Colors[3].Color;
}
