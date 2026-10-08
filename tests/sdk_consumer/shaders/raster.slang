struct Tint { float4 color; };
[[vk::binding(0, 0)]] ConstantBuffer<Tint> tint : register(b0);
[[vk::binding(1, 0)]] Texture2D<float4> image : register(t1);
[[vk::binding(2, 0)]] SamplerState imageSampler : register(s2);
struct Input
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] float2 offset : TEXCOORD1;
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
    output.position = float4(input.position.xy + input.offset, input.position.z, 1);
    output.uv = input.uv;
    return output;
}
[shader("fragment")]
float4 fragmentMain(Varying input) : SV_Target
{
    return image.Sample(imageSampler, input.uv) * tint.color;
}
