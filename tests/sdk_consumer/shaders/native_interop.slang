// Native bridge fixture: producer colors and foreign graphics sampling are
// GPU-only. The fixture reads only the final validation buffer on the CPU.
struct Varying { float4 position : SV_Position; };
Varying vertexMain(float3 position : POSITION)
{
    Varying result; result.position=float4(position,1); return result;
}
float4 sourceMain(Varying input) : SV_Target0
{
    bool right=input.position.x>=2;
    bool bottom=input.position.y>=2;
    return bottom ? (right ? float4(1,1,1,1) : float4(0,0,1,1))
                  : (right ? float4(0,1,0,1) : float4(1,0,0,1));
}
[[vk::binding(0,0)]] Texture2D<float4> sourceImage : register(t0);
[[vk::binding(1,0)]] SamplerState sourceSampler : register(s1);
float4 sampleMain(Varying input) : SV_Target0
{
    return sourceImage.SampleLevel(sourceSampler,input.position.xy/float2(4,4),0);
}
