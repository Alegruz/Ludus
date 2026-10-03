// Generic smoke-demo shader authored in Slang and compiled by the SDK helper to
// SPIR-V (Vulkan), WGSL (WebGPU) and GLSL ES 3.00 (WebGL 2). It draws a moving
// orange marker on a pulsing background over a fullscreen triangle, so the demo
// animates identically on every backend through the public rendering API. The
// marker position comes from the simulation; the QA harness detects the orange.
//
// std140 layout (verified independently per target, matching the diagnostic):
//   resolution float2  offset 0  size 8
//   pulse      float   offset 8  size 4
//   markerSize float   offset 12 size 4
//   marker     float3  offset 16 size 12   (x, y in [0,1]; z unused)
//   tint       float4  offset 32 size 16
// Total 48 bytes, alignment 16.
struct SmokeUniforms
{
    float2 resolution;
    float pulse;
    float markerSize;
    float3 marker;
    float4 tint;
};
[[vk::binding(0, 0)]] ConstantBuffer<SmokeUniforms> smoke;

[shader("vertex")]
// SV_VulkanVertexID maps to plain VertexIndex / vertex_index / gl_VertexID with
// no base-vertex draw parameters, so the SPIR-V translates to WebGL-safe GLSL ES.
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
    position.y = smoke.resolution.y - position.y;
#endif
    // Pulsing background, independent of marker, so the clear animates.
    float3 background = float3(0.04 + smoke.pulse * 0.06, 0.06, 0.14 + smoke.pulse * 0.08);
    // Marker in framebuffer space; keep it circular regardless of aspect ratio.
    float2 uv = position.xy / smoke.resolution;
    float2 d = (uv - smoke.marker.xy) * float2(smoke.resolution.x / smoke.resolution.y, 1.0);
    float inside = dot(d, d) < smoke.markerSize * smoke.markerSize ? 1.0 : 0.0;
    float3 color = background + smoke.tint.rgb * inside;
    return float4(color, 1.0);
}
