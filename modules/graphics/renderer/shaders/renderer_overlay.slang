// One ordered triangle path for debug solids and single-channel glyph coverage.
[[vk::binding(0, 0)]] Texture2D<float4> coverageAtlas : register(t0);
[[vk::binding(1, 0)]] SamplerState coverageSampler : register(s1);
struct Input
{
    [[vk::location(0)]] float4 clip : POSITION;
    [[vk::location(1)]] float4 coverage : TEXCOORD0;
    [[vk::location(2)]] float4 color : COLOR0;
};
struct Varying
{
    float4 position : SV_Position;
    float3 coverage : TEXCOORD0;
    float4 color : COLOR0;
};
[shader("vertex")]
Varying vertexMain(Input input)
{
    Varying output;
    output.position = input.clip;
    output.coverage = input.coverage.xyz;
    output.color = input.color;
    return output;
}
[shader("fragment")]
float4 fragmentMain(Varying input) : SV_Target
{
    float mask = lerp(1.0, coverageAtlas.Sample(coverageSampler, input.coverage.xy).r, input.coverage.z);
    float alpha = input.color.a * mask;
    return float4(input.color.rgb * alpha, alpha);
}
