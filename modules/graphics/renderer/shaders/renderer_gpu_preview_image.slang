// GPU-produced per-pixel colors are instance attributes, using R4 StorageVertex.
// No fragment-stage storage binding or host image transfer is required.
[[vk::binding(0, 0)]] cbuffer ImageParameters
{
    float4 imageSize;
};
struct VertexInput
{
    float2 corner : POSITION0;
    float4 color : COLOR0;
    uint instance : SV_InstanceID;
};
struct VertexOutput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
};
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    uint width = uint(imageSize.x);
    float2 pixel = float2(input.instance % width, input.instance / width) + .5;
    float2 center = pixel * float2(2 / imageSize.x, -2 / imageSize.y) + float2(-1, 1);
    output.position = float4(center + input.corner / imageSize.xy, 0, 1);
    output.color = input.color;
    return output;
}
float4 fragmentMain(VertexOutput input) : SV_Target0
{
    return input.color;
}
