// Explicit scene-linear -> display-linear Reinhard -> optional sRGB encoding.
// UI blends into a linear display intermediate before the final encode-only pass.
struct Parameters { float4 Output; };
[[vk::binding(0,0)]] ConstantBuffer<Parameters> parameters : register(b0);
[[vk::binding(1,0)]] Texture2D<float4> image : register(t1);
[[vk::binding(2,0)]] SamplerState imageSampler : register(s2);
struct Input { [[vk::location(0)]] float4 positionUv : POSITION; };
struct Varying { float4 position : SV_Position; float2 uv : TEXCOORD0; };
[shader("vertex")]
Varying vertexMain(Input input)
{
    Varying output; output.position = float4(input.positionUv.xy,0,1); output.uv = input.positionUv.zw; return output;
}
[shader("fragment")]
float4 fragmentMain(Varying input) : SV_Target
{
    float3 color = max(image.SampleLevel(imageSampler,input.uv,0).rgb,float3(0))*parameters.Output.x;
    if (parameters.Output.y > 0.5) color = color/(1+color);
    if (parameters.Output.z > 0.5)
    {
        color = saturate(color);
        color = float3(color.x <= 0.0031308 ? 12.92*color.x : 1.055*pow(color.x,1.0/2.4)-0.055,
                       color.y <= 0.0031308 ? 12.92*color.y : 1.055*pow(color.y,1.0/2.4)-0.055,
                       color.z <= 0.0031308 ? 12.92*color.z : 1.055*pow(color.z,1.0/2.4)-0.055);
    }
    return float4(color,1);
}
