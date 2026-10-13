struct Input { [[vk::location(0)]] float2 position : POSITION; [[vk::location(1)]] float2 uv : TEXCOORD0; };
struct Varying { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Varying vertexMain(Input input)
{
    Varying output; output.position=float4(input.position,0.25,1); output.uv=input.uv;return output;
}
struct Mrt { float4 first : SV_Target0; float4 second : SV_Target1; };
Mrt writeMain(Varying input)
{
    Mrt output;
    output.first=float4(input.uv.x>=0.5?1:0,0,input.uv.y>=0.5?1:0,1);
    output.second=float4(0,input.uv.y>=0.5?1:0,input.uv.x>=0.5?1:0,1);
    return output;
}
Mrt maskMain(Varying input)
{
    Mrt output;output.first=float4(1,1,0,1);output.second=float4(0,1,1,1);return output;
}
[[vk::binding(0,0)]] Texture2D<float4> colorImage : register(t0);
[[vk::binding(1,0)]] SamplerState colorSampler : register(s1);
float4 sampleMain(Varying input) : SV_Target0 { return colorImage.SampleLevel(colorSampler,input.uv,0); }
[[vk::binding(2,0)]] Texture2D<float> depthImage : register(t2);
float4 depthMain(Varying input) : SV_Target0 { float d=depthImage.Load(int3(int2(input.uv*8),0));return float4(d,d,d,1); }
[[vk::binding(3,0)]] Texture2D<uint> stencilImage : register(t3);
float4 stencilMain(Varying input) : SV_Target0 { float s=float(stencilImage.Load(int3(int2(input.uv*8),0)))/255;return float4(s,s,s,1); }
