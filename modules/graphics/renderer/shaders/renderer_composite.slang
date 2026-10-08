// Linear target composition: top-left UVs, explicit clip-space crop geometry.
[[vk::binding(0, 0)]] Texture2D<float4> image : register(t0);
[[vk::binding(1, 0)]] SamplerState imageSampler : register(s1);
struct Input
{
    [[vk::location(0)]] float4 positionUv : POSITION;
};
struct Varying
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};
[shader("vertex")]
Varying vertexMain(Input input)
{
    Varying output;
    output.position = float4(input.positionUv.xy, 0, 1);
    output.uv = input.positionUv.zw;
    return output;
}
[shader("fragment")]
float4 fragmentMain(Varying input) : SV_Target
{
    return image.Sample(imageSampler, input.uv);
}
