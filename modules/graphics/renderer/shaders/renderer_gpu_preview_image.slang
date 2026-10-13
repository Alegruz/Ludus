// GPU-produced RGB/index records are instance attributes, using R4 StorageVertex.
// No fragment-stage storage binding or host image transfer is required.
[[vk::binding(0, 0)]] cbuffer ImageParameters
{
    float4 previewExtent;
};
struct VertexInput
{
    float2 corner : POSITION0;
    float4 display : COLOR0; // Encoded RGB and exact integer-valued pixel index.
};
struct VertexOutput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
};
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    uint width = uint(previewExtent.x);
    float2 pixel = float2(uint(input.display.w) % width, uint(input.display.w) / width) + .5;
    float2 center = pixel * float2(2 / previewExtent.x, -2 / previewExtent.y) + float2(-1, 1);
    output.position = float4(center + input.corner / previewExtent.xy, 0, 1);
    output.color = float4(input.display.xyz, 1);
    return output;
}
float4 fragmentMain(VertexOutput input) : SV_Target0
{
    return input.color;
}
