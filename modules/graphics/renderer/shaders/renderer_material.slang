// L3 schema: CPU material activation checks each selected target's member offsets.
struct Parameters
{
    float4 Tint;
    float4 UvTransform;
    float4 Options; // cutoff, explicit LOD (-1 implicit), alpha domain, reserved
};
[[vk::binding(0, 0)]] ConstantBuffer<Parameters> material : register(b0);
[[vk::binding(1, 0)]] Texture2D<float4> colorTexture : register(t1);
[[vk::binding(2, 0)]] SamplerState colorSampler : register(s2);
struct Input
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float4 row0 : TEXCOORD0;
    [[vk::location(2)]] float4 row1 : TEXCOORD1;
    [[vk::location(3)]] float4 row2 : TEXCOORD2;
    [[vk::location(4)]] float4 row3 : TEXCOORD3;
    [[vk::location(5)]] float4 color : COLOR0;
    [[vk::location(6)]] float2 uv : TEXCOORD4;
};
struct Varying
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};
[shader("vertex")]
Varying vertexMain(Input input)
{
    Varying output;
    float4 p = float4(input.position, 1);
    output.position = float4(dot(input.row0,p),dot(input.row1,p),dot(input.row2,p),dot(input.row3,p));
    output.uv = input.uv;
    output.color = input.color;
    return output;
}
[shader("fragment")]
float4 fragmentMain(Varying input) : SV_Target
{
    float2 uv = input.uv * material.UvTransform.xy + material.UvTransform.zw;
    float4 texel = material.Options.y < 0 ? colorTexture.Sample(colorSampler,uv) :
                                          colorTexture.SampleLevel(colorSampler,uv,material.Options.y);
    float4 color = texel * material.Tint * input.color;
    if (material.Options.z == 1 && color.a < material.Options.x) discard;
    float alpha = material.Options.z == 2 ? color.a : 1;
    return float4(color.rgb * alpha, alpha);
}
