struct ProbeUniforms
{
    float2 resolution;
    float elapsedTime;
    float3 direction;
    float4 tint;
};
[[vk::binding(0, 0)]] ConstantBuffer<ProbeUniforms> probe;
[shader("vertex")]
// SV_VulkanVertexID maps to the plain Vulkan VertexIndex / WGSL vertex_index /
// GLSL ES gl_VertexID. SV_VertexID would add D3D base-vertex semantics
// (SPV_KHR_shader_draw_parameters), which SPIRV-Cross rejects for the WebGL 2
// (GLSL ES) backend. The non-indexed, zero-base-vertex fullscreen draw makes
// these identical in value for every backend.
float4 vertexMain(uint id : SV_VulkanVertexID) : SV_Position
{
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p * 2.0 - 1.0, 0.0, 1.0);
}
[shader("fragment")]
float4 fragmentMain(float4 position : SV_Position) : SV_Target0
{
#if LUDUS_GLSL_ES
    // WebGL fragment coordinates start at the bottom; our authored scene uses top-left pixels.
    position.y = probe.resolution.y - position.y;
#endif
    float2 uv = position.xy / probe.resolution;
    float2 centered = (position.xy - probe.resolution * 0.5) / probe.resolution.y;
    float circle = dot(centered, centered) < 0.04 ? 1.0 : 0.0;
    float pulse = 0.05 * probe.elapsedTime;
    return float4(probe.tint.rgb * float3(uv.x, uv.y, circle) + probe.direction * pulse, probe.tint.a);
}
