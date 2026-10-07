// Thanks to Cornell University Program of Computer Graphics, "The Cornell Box":
// https://bowers.cornell.edu/cornell-box. Adopt the open room, colored walls,
// ceiling emitter and two blocks; use original normalized geometry and RGB colors,
// not measured Cornell data or their radiosity method. This is direct lighting
// only, with a small artistic ambient term. See docs/examples/cornell-box.md.
struct DisplayUniforms
{
    float4 viewport; // width, height, manual sRGB encoding (0/1), flat materials (0/1)
    float4 eye;
    float4 right; // world ray displacement at NDC X = 1
    float4 up; // world ray displacement at NDC Y = 1
    float4 forward;
};
[[vk::binding(0, 0)]] ConstantBuffer<DisplayUniforms> display;

struct Hit
{
    float distance;
    float3 normal;
    float3 albedo;
    bool emitter;
};

// Analytic rectangle intersection. axis is its normal axis, u/v span the plane.
void rectangle(float3 origin, float3 direction, int axis, float plane,
               float2 low, float2 high, float3 normal, float3 albedo, inout Hit hit)
{
    if (abs(direction[axis]) < 0.00001) return;
    float t = (plane - origin[axis]) / direction[axis];
    if (t <= 0.0001 || t >= hit.distance) return;
    float3 p = origin + direction * t;
    float2 uv = axis == 0 ? p.yz : axis == 1 ? p.xz : p.xy;
    if (any(uv < low) || any(uv > high)) return;
    hit.distance = t;
    hit.normal = normal;
    hit.albedo = albedo;
    hit.emitter = axis == 1 && plane > 1.9 && abs(p.x) < 0.32 && abs(p.z) < 0.28;
}

float3 rotateY(float3 p, float2 rotation)
{
    return float3(rotation.x * p.x + rotation.y * p.z, p.y,
                  -rotation.y * p.x + rotation.x * p.z);
}

// Slabs in block-local space. Parallel rays are tested before dividing, avoiding
// infinities/NaNs on face-aligned rays. Both entry and exit normals are retained.
void block(float3 origin, float3 direction, float3 center, float3 extent,
           float2 rotation, inout Hit hit)
{
    float3 o = rotateY(origin - center, float2(rotation.x, -rotation.y));
    float3 d = rotateY(direction, float2(rotation.x, -rotation.y));
    float nearT = -10000.0;
    float farT = 10000.0;
    float3 nearNormal = 0.0;
    float3 farNormal = 0.0;
    for (int axis = 0; axis < 3; ++axis)
    {
        if (abs(d[axis]) < 0.00001)
        {
            if (abs(o[axis]) > extent[axis]) return;
        }
        else
        {
            float a = (-extent[axis] - o[axis]) / d[axis];
            float b = (extent[axis] - o[axis]) / d[axis];
            float3 n = 0.0;
            n[axis] = d[axis] > 0.0 ? -1.0 : 1.0;
            if (min(a, b) > nearT) { nearT = min(a, b); nearNormal = n; }
            if (max(a, b) < farT) { farT = max(a, b); farNormal = -n; }
            if (nearT > farT) return;
        }
    }
    float t = nearT > 0.0001 ? nearT : farT;
    if (t <= 0.0001 || t >= hit.distance) return;
    hit.distance = t;
    hit.normal = rotateY(nearT > 0.0001 ? nearNormal : farNormal, rotation);
    hit.albedo = float3(0.73, 0.71, 0.67);
    hit.emitter = false;
}

void blocks(float3 origin, float3 direction, inout Hit hit)
{
    block(origin, direction, float3(-0.40, 0.36, 0.35), float3(0.30, 0.36, 0.30),
          float2(0.939693, 0.342020), hit);
    block(origin, direction, float3(0.38, 0.65, -0.32), float3(0.30, 0.65, 0.30),
          float2(0.965926, -0.258819), hit);
}

Hit scene(float3 origin, float3 direction)
{
    Hit hit = {10000.0, float3(0.0), float3(0.0), false};
    float3 white = float3(0.73, 0.71, 0.67);
    rectangle(origin, direction, 0, -1.0, float2(0.0, -1.0), float2(2.0, 1.0),
              float3(1.0, 0.0, 0.0), float3(0.63, 0.065, 0.05), hit);
    rectangle(origin, direction, 0, 1.0, float2(0.0, -1.0), float2(2.0, 1.0),
              float3(-1.0, 0.0, 0.0), float3(0.12, 0.45, 0.09), hit);
    rectangle(origin, direction, 1, 0.0, float2(-1.0), float2(1.0),
              float3(0.0, 1.0, 0.0), white, hit);
    rectangle(origin, direction, 1, 2.0, float2(-1.0), float2(1.0),
              float3(0.0, -1.0, 0.0), white, hit);
    rectangle(origin, direction, 2, -1.0, float2(-1.0, 0.0), float2(1.0, 2.0),
              float3(0.0, 0.0, 1.0), white, hit);
    blocks(origin, direction, hit);
    return hit;
}

float3 shade(float3 origin, float3 direction)
{
    Hit hit = scene(origin, direction);
    if (hit.distance > 9999.0) return float3(0.012, 0.018, 0.027);
    if (hit.emitter) return float3(4.0, 3.8, 3.4);
    if (display.viewport.w > 0.5) return hit.albedo;
    float3 point = origin + hit.distance * direction + hit.normal * 0.0005;
    float illumination = 0.0;
    // Fixed 4x4 midpoint quadrature of the ceiling rectangle: soft shadows without
    // random sampling, history textures, additional passes or indirect bounces.
    for (int y = 0; y < 4; ++y)
    {
        for (int x = 0; x < 4; ++x)
        {
            float3 light = float3(((float(x) + 0.5) / 4.0 - 0.5) * 0.64, 1.999,
                                 ((float(y) + 0.5) / 4.0 - 0.5) * 0.56);
            float3 delta = light - point;
            float distanceSquared = dot(delta, delta);
            float distance = sqrt(distanceSquared);
            float3 toLight = delta / distance;
            Hit shadow = {distance - 0.001, float3(0.0), float3(0.0), false};
            blocks(point, toLight, shadow);
            if (shadow.distance >= distance - 0.001)
                illumination += max(dot(hit.normal, toLight), 0.0) * max(toLight.y, 0.0)
                                / distanceSquared;
        }
    }
    return hit.albedo * (0.035 + illumination * (0.64 * 0.56 * 12.0 / (16.0 * 3.141593)));
}

float3 linearToSrgb(float3 color)
{
    return float3(color.x <= 0.0031308 ? 12.92 * color.x : 1.055 * pow(color.x, 1.0 / 2.4) - 0.055,
                  color.y <= 0.0031308 ? 12.92 * color.y : 1.055 * pow(color.y, 1.0 / 2.4) - 0.055,
                  color.z <= 0.0031308 ? 12.92 * color.z : 1.055 * pow(color.z, 1.0 / 2.4) - 0.055);
}

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
    position.y = display.viewport.y - position.y;
#endif
    float2 screen = (position.xy * 2.0 - display.viewport.xy) / display.viewport.xy;
    screen.y = -screen.y;
    float3 direction = normalize(display.forward.xyz + screen.x * display.right.xyz + screen.y * display.up.xyz);
    float3 color = shade(display.eye.xyz, direction);
    color = color / (1.0 + color); // simple exposure/tone mapping
    if (display.viewport.z > 0.5) color = linearToSrgb(color);
    return float4(color, 1.0);
}
