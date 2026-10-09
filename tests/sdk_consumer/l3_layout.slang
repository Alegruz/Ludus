// Per-target cook fixture: matrix and fixed arrays must retain independently
// verified occupied ranges and offsets; this shader is not a runtime material.
struct LayoutParameters
{
    float4x4 Transform;
    float4 Colors[2];
    float4x4 Bones[2];
    float4 Tint;
};
[[vk::binding(0, 0)]] ConstantBuffer<LayoutParameters> parameters : register(b0);
float4 vertexMain(float3 position : POSITION) : SV_Position
{
    return float4(position, 1);
}
float4 fragmentMain(float4 position : SV_Position) : SV_Target
{
    return mul(parameters.Transform, parameters.Colors[uint(position.x) % 2]) +
           mul(parameters.Bones[uint(position.y) % 2], parameters.Tint);
}
