#include "raster_bindings.slang"

struct Settings
{
    float4 Selection;
};
LUDUS_SHADER_BINDING(0) ConstantBuffer<Settings> SettingsBuffer : register(b0);
#if defined(LUDUS_R11_ARRAYS)
LUDUS_SHADER_BINDING(1) Texture2D<float4> Images[4] : register(t1);
LUDUS_SHADER_BINDING(2) SamplerState ImageSampler : register(s2);
#endif
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
#if defined(LUDUS_R11_ARRAYS)
    uint index = min(uint(input.Uv.x * 4), 3U);
    return Images[NonUniformResourceIndex(index)].SampleLevel(ImageSampler, input.Uv, 0) * SettingsBuffer.Selection;
#else
    return SettingsBuffer.Selection;
#endif
}

#if defined(LUDUS_R11_ARRAYS)
[shader("fragment")] float4 ConstantArrayMain(Varying input) : SV_Target
{
    float4 color = input.Uv.x < 0.25   ? Images[0].SampleLevel(ImageSampler, input.Uv, 0)
                   : input.Uv.x < 0.5  ? Images[1].SampleLevel(ImageSampler, input.Uv, 0)
                   : input.Uv.x < 0.75 ? Images[2].SampleLevel(ImageSampler, input.Uv, 0)
                                       : Images[3].SampleLevel(ImageSampler, input.Uv, 0);
    return color * SettingsBuffer.Selection;
}
#endif
