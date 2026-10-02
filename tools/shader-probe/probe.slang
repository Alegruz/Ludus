struct ProbeUniforms
{
    float2 resolution;
    float elapsedTime;
    float3 direction;
    float4 tint;
};
[[vk::binding(0, 0)]] ConstantBuffer<ProbeUniforms> probe;
[shader("vertex")]
float4 vertexMain(uint id : SV_VertexID) : SV_Position
{
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p * 2.0 - 1.0, 0.0, 1.0);
}
[shader("fragment")]
float4 fragmentMain(float4 position : SV_Position) : SV_Target0
{
    float2 uv = position.xy / probe.resolution;
    float2 centered = (position.xy - probe.resolution * 0.5) / probe.resolution.y;
    float circle = dot(centered, centered) < 0.04 ? 1.0 : 0.0;
    float pulse = 0.05 * probe.elapsedTime;
    return float4(probe.tint.rgb * float3(uv.x, uv.y, circle) + probe.direction * pulse, probe.tint.a);
}
