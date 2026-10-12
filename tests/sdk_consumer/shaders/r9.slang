struct Settings { float4 selection; float4 coordinate; };
[[vk::binding(0,0)]] ConstantBuffer<Settings> settings : register(b0);
[[vk::binding(1,0)]] Texture2D<float4> image : register(t1);
[[vk::binding(2,0)]] SamplerState imageSampler : register(s2);
[[vk::binding(3,0)]] Texture2DArray<float4> layers : register(t3);
[[vk::binding(4,0)]] TextureCube<float4> faces : register(t4);
[[vk::binding(5,0)]] Texture3D<float4> volume : register(t5);
[[vk::binding(6,0)]] Texture2D<float4> hdr : register(t6);
[[vk::binding(7,0)]] Texture2D<float4> blocks : register(t7);
struct Input { [[vk::location(0)]] float2 position : POSITION; [[vk::location(1)]] float2 uv : TEXCOORD0; };
struct Varying { float4 position : SV_Position; float2 uv : TEXCOORD0; };
[shader("vertex")] Varying vertexMain(Input input)
{
    Varying output; output.position = float4(input.position,0,1); output.uv=input.uv; return output;
}
[shader("fragment")] float4 fragmentMain(Varying input) : SV_Target
{
    float lod = settings.selection.y;
    if (settings.selection.x < 0.5) return image.SampleLevel(imageSampler,input.uv,lod);
    if (settings.selection.x < 1.5) return layers.SampleLevel(imageSampler,float3(input.uv,settings.coordinate.z),lod);
    if (settings.selection.x < 2.5)
    {
        float2 p = input.uv * 2 - 1;
        float3 direction = settings.coordinate.xyz;
        if (direction.x > 0.5) direction = float3(1,-p.y,-p.x);
        else if (direction.x < -0.5) direction = float3(-1,-p.y,p.x);
        else if (direction.y > 0.5) direction = float3(p.x,1,p.y);
        else if (direction.y < -0.5) direction = float3(p.x,-1,-p.y);
        else if (direction.z > 0.5) direction = float3(p.x,-p.y,1);
        else direction = float3(-p.x,-p.y,-1);
        return faces.SampleLevel(imageSampler,direction,lod);
    }
    if (settings.selection.x < 3.5) return volume.SampleLevel(imageSampler,float3(input.uv,settings.coordinate.z),lod);
    if (settings.selection.x < 4.5) return hdr.SampleLevel(imageSampler,input.uv,lod);
    return blocks.SampleLevel(imageSampler,input.uv,lod);
}
