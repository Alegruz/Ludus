#include "raster_bindings.slang"

struct Settings
{
    float4 Selection;
};
LUDUS_SHADER_BINDING(0) ConstantBuffer<Settings> SettingsBuffer : register(b0);
#if defined(LUDUS_R11_ARRAYS)
#    if defined(LUDUS_R11C_RUNTIME)
#        include "runtime_bindings.slang"
LUDUS_SHADER_BINDING(LUDUS_RUNTIME_BINDING_ARRAY_SLOT)
Texture2D<float4> Images[] : register(LUDUS_RUNTIME_REGISTER(t, LUDUS_RUNTIME_BINDING_ARRAY_SLOT));
#    elif defined(LUDUS_R11C_CUBE)
LUDUS_SHADER_BINDING(1) TextureCube<float4> Images[4] : register(t1);
#    elif defined(LUDUS_R11C_VOLUME)
LUDUS_SHADER_BINDING(1) Texture3D<float4> Images[4] : register(t1);
#    elif defined(LUDUS_R11C_WIDER)
LUDUS_SHADER_BINDING(1) Texture2DArray<float4> Images[4] : register(t1);
#    else
LUDUS_SHADER_BINDING(1) Texture2D<float4> Images[4] : register(t1);
#    endif
#    if defined(LUDUS_R11C_WIDER)
LUDUS_SHADER_BINDING(6) SamplerState ImageSamplers[2] : register(s6);
#    else
LUDUS_SHADER_BINDING(2) SamplerState ImageSampler : register(s2);
#    endif
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
#    if defined(LUDUS_R11C_WIDER)
    return Images[0].SampleLevel(ImageSamplers[0], float3(input.Uv, 0), 0) * SettingsBuffer.Selection;
#    else
    uint index = min(uint(input.Uv.x * 4), 3U);
#        if defined(LUDUS_R11C_RUNTIME)
    index = BoundRuntimeBindingIndex(index);
#        endif
    return Images[NonUniformResourceIndex(index)].SampleLevel(ImageSampler, input.Uv, 0) * SettingsBuffer.Selection;
#    endif
#else
    return SettingsBuffer.Selection;
#endif
}
#if defined(LUDUS_R11_ARRAYS)
[shader("fragment")] float4 ConstantArrayMain(Varying input) : SV_Target
{
#    if defined(LUDUS_R11C_WIDER)
#        if defined(LUDUS_R11C_CUBE)
    float3 coordinateTexture = float3(1, 0, 0);
#        else
    float3 coordinateTexture = float3(input.Uv, 0);
#        endif
    float4 color = input.Uv.x < 0.25   ? Images[0].SampleLevel(ImageSamplers[0], coordinateTexture, 0)
                   : input.Uv.x < 0.5  ? Images[1].SampleLevel(ImageSamplers[1], coordinateTexture, 0)
                   : input.Uv.x < 0.75 ? Images[2].SampleLevel(ImageSamplers[0], coordinateTexture, 0)
                                       : Images[3].SampleLevel(ImageSamplers[1], coordinateTexture, 0);
#    elif defined(LUDUS_R11C_RUNTIME)
    float4 color = input.Uv.x < 0.25   ? Images[BoundRuntimeBindingIndex(0)].SampleLevel(ImageSampler, input.Uv, 0)
                   : input.Uv.x < 0.5  ? Images[BoundRuntimeBindingIndex(1)].SampleLevel(ImageSampler, input.Uv, 0)
                   : input.Uv.x < 0.75 ? Images[BoundRuntimeBindingIndex(2)].SampleLevel(ImageSampler, input.Uv, 0)
                                       : Images[BoundRuntimeBindingIndex(3)].SampleLevel(ImageSampler, input.Uv, 0);
#    else
    float4 color = input.Uv.x < 0.25   ? Images[0].SampleLevel(ImageSampler, input.Uv, 0)
                   : input.Uv.x < 0.5  ? Images[1].SampleLevel(ImageSampler, input.Uv, 0)
                   : input.Uv.x < 0.75 ? Images[2].SampleLevel(ImageSampler, input.Uv, 0)
                                       : Images[3].SampleLevel(ImageSampler, input.Uv, 0);
#    endif
    return color * SettingsBuffer.Selection;
}
#endif
