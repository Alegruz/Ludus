// Independent std140 layout: view/settings at 0/16, then three arrays of
// 64 float4s at 32/1056/2080. Array stride 16; total occupied size 3104.
struct WorldUniforms
{
    float4 view;
    float4 settings;
    float4 geometry[64];
    float4 color[64];
    float4 rotation[64];
};
[[vk::binding(0, 0)]] ConstantBuffer<WorldUniforms> world;
[shader("vertex")]
float4 vertexMain(uint id : SV_VulkanVertexID) : SV_Position
{
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p * 2.0 - 1.0, 0.0, 1.0);
}
[shader("fragment")]
float4 fragmentMain(float4 position : SV_Position) : SV_Target0
{
#if LUDUS_GLSL_ES
    position.y = world.view.y - position.y;
#endif
    float2 uv = position.xy / world.view.xy;
    float2 point = world.view.zw + (uv - 0.5) * float2(world.settings.x * world.view.x / world.view.y, -world.settings.x);
    float3 color = float3(0.025, 0.04, 0.08);
    for (uint index = 0; index < min(uint(world.settings.y), 64u); ++index)
    {
        float4 geometry = world.geometry[index]; float2 delta = point - geometry.xy;
        float angle = world.rotation[index].x; float c = cos(angle); float s = sin(angle);
        float2 local = float2(c * delta.x + s * delta.y, -s * delta.x + c * delta.y);
        if (all(abs(local) < geometry.zw)) { color = lerp(color, world.color[index].rgb, world.color[index].a); }
    }
    return float4(color, 1.0);
}
