// GPU preview ports Ludus's frozen RGB transport, not the CPU transport loop.
// Thanks to Sven Woop, Carsten Benthin and Ingo Wald, "Watertight Ray/Triangle
// Intersection", JCGT 2(1) (2013), section 3, for permuted/sheared
// triangle tests (https://jcgt.org/published/0002/01/05/paper.pdf); this FP32 BVH
// variant does not claim arbitrary thin-geometry robustness or CPU-double parity.
// Thanks to John K. Salmon et al., "Parallel Random Numbers: As Easy as 1, 2, 3",
// SC11 (2011), section 4.3, for Philox4x32-10
// (https://www.thesalmons.org/john/random123/papers/random123sc11.pdf).
// Thanks to Eric Heitz, "Sampling the GGX Distribution of Visible Normals",
// JCGT 7(4) (2018), sections 3-4/Appendix B, visible-GGX
// (https://jcgt.org/published/0007/04/01/paper.pdf), Google, "Physically Based
// Rendering in Filament", sections 4.4/5.2.2
// (https://google.github.io/filament/main/filament.html), and Matt Pharr, Wenzel
// Jakob and Greg Humphreys, "Physically Based Rendering", 4e, section 13.4
// (https://pbr-book.org/4ed/Light_Transport_I_Surface_Reflection/A_Better_Path_Tracer)
// for the shared reflection, MIS and roulette conventions. Original ports from
// reference_bsdf.cpp/reference_path.cpp; renderer-systems.md owns quality limits.
[[vk::binding(0, 0)]] cbuffer Parameters
{
    column_major float4x4 clipToWorld;
    float4 eye;
    uint4 dimensions; // width, height, depth, roulette
    uint4 offsets0;   // nodes, order, emitters, surfaces
    uint4 counts;     // triangles, nodes, emitters, analytic lights
    uint4 sampling;   // key lo, key hi, flags (reverse/infinite/center), target
    float4 environment;
    float4 options; // secondary range, exposure, reserved
    float4 boundsMin;
    float4 boundsMax;
};
[[vk::binding(1, 0)]] StructuredBuffer<uint4> sceneWords;
float4 sceneValue(uint index)
{
    return asfloat(sceneWords[index]);
}
[[vk::binding(2, 0)]] RWStructuredBuffer<float4> accumulation; // sum/count, compensation/error
[[vk::binding(3, 0)]] RWStructuredBuffer<float4> colors;       // Encoded RGB and binary32-exact pixel index.
[[vk::binding(4, 0)]] StructuredBuffer<uint4> textureWords;
static const float PI = 3.14159265358979323846;
float3 unit(float3 v)
{
    float s = dot(v, v);
    return s > 0 ? v * rsqrt(s) : float3(0);
}
bool positive(float3 v)
{
    return any(v > 0);
}
void wide(uint a, uint b, out uint lo, out uint hi)
{
    uint al = a & 65535, ah = a >> 16, bl = b & 65535, bh = b >> 16;
    uint t = al * bl, low = t & 65535, carry = t >> 16;
    t = ah * bl + carry;
    uint middle = t & 65535;
    hi = t >> 16;
    t = al * bh + middle;
    hi += ah * bh + (t >> 16);
    lo = (t << 16) | low;
}
uint randomBits(uint pixel, uint sample, uint dimension, uint attempt = 0)
{
    uint4 c = uint4(pixel, 0, sample, (dimension >> 2) | (attempt << 16));
    uint2 k = sampling.xy;
    for (uint r = 0; r < 10; ++r)
    {
        uint a, b, x, y;
        wide(0xd2511f53, c.x, a, b);
        wide(0xcd9e8d57, c.z, x, y);
        c = uint4(y ^ c.y ^ k.x, x, b ^ c.w ^ k.y, a);
        k += uint2(0x9e3779b9, 0xbb67ae85);
    }
    return c[dimension & 3];
}
float random01(uint p, uint s, uint d)
{
    return float(randomBits(p, s, d) >> 8) * (1.0 / 16777216.0);
}
uint choose(uint p, uint s, uint d, uint count)
{
    uint threshold = (0u - count) % count;
    for (uint retry = 0; retry < 65536; ++retry)
    {
        uint lo, hi;
        wide(randomBits(p, s, d, retry), count, lo, hi);
        if (lo >= threshold)
            return hi;
    }
    return 0xffffffff;
}
float3 position(uint t, float2 uv)
{
    uint b = t * 10;
    return sceneValue(b).xyz * (1 - uv.x - uv.y) + sceneValue(b + 1).xyz * uv.x + sceneValue(b + 2).xyz * uv.y;
}
float2 texcoord(uint t, float2 uv)
{
    uint b = t * 10;
    return sceneValue(b + 6).xy * (1 - uv.x - uv.y) + sceneValue(b + 6).zw * uv.x + sceneValue(b + 7).xy * uv.y;
}
float4 surface(uint t, uint field)
{
    uint i = asuint(sceneValue(t * 10).w);
    return sceneValue(offsets0.w + i * 6 + field);
}
int address(int i, int extent, bool repeat)
{
    if (!repeat)
        return clamp(i, 0, extent - 1);
    int v = i % extent;
    return v < 0 ? v + extent : v;
}
float4 texel(uint t, int x, int y)
{
    uint4 layout = asuint(surface(t, 5));
    uint2 size = asuint(surface(t, 1).zw);
    uint flags = layout.y;
    x = address(x, int(size.x), (flags & 2) != 0);
    y = address(y, int(size.y), (flags & 4) != 0);
    uint index = uint(y) * size.x + uint(x);
    uint word = textureWords[layout.x + (index >> 2)][index & 3];
    float4 c = float4(word & 255, (word >> 8) & 255, (word >> 16) & 255, word >> 24) / 255.0;
    if ((flags & 8) != 0)
        c.xyz = select(c.xyz > .04045, pow((c.xyz + .055) / 1.055, 2.4), c.xyz / 12.92);
    return c;
}
float4 texture(uint t, float2 uv)
{
    if (asuint(surface(t, 4).w) == 0)
        return float4(1);
    float4 transform = surface(t, 3);
    uv = uv * transform.xy + transform.zw;
    uint flags = asuint(surface(t, 5).y);
    uint2 size = asuint(surface(t, 1).zw);
    uv.x = (flags & 2) != 0 ? frac(uv.x) : saturate(uv.x);
    uv.y = (flags & 4) != 0 ? frac(uv.y) : saturate(uv.y);
    float2 p = uv * float2(size);
    if ((flags & 1) == 0)
        return texel(t, int(floor(p.x)), int(floor(p.y)));
    p -= .5;
    int2 cell = int2(floor(p));
    float2 f = frac(p);
    return lerp(lerp(texel(t, cell.x, cell.y), texel(t, cell.x + 1, cell.y), f.x),
                lerp(texel(t, cell.x, cell.y + 1), texel(t, cell.x + 1, cell.y + 1), f.x),
                f.y);
}
struct Hit
{
    uint triangle;
    float distance;
    float2 uv;
};
bool box(float3 o, float3 d, float nearT, float farT, uint node)
{
    uint b = offsets0.x + node * 3;
    float3 lo = sceneValue(b).xyz, hi = sceneValue(b + 1).xyz;
    for (uint axis = 0; axis < 3; ++axis)
    {
        if (d[axis] == 0)
        {
            if (o[axis] < lo[axis] || o[axis] > hi[axis])
                return false;
        }
        else
        {
            float a = (lo[axis] - o[axis]) / d[axis], c = (hi[axis] - o[axis]) / d[axis];
            nearT = max(nearT, min(a, c));
            farT = min(farT, max(a, c));
            if (nearT > farT)
                return false;
        }
    }
    return true;
}
bool intersect(uint t, float3 o, float3 d, float nearT, float farT, out float distance, out float2 uv)
{
    uint z = 0;
    if (abs(d.y) > abs(d.x))
        z = 1;
    if (abs(d.z) > abs(d[z]))
        z = 2;
    uint x = (z + 1) % 3, y = (x + 1) % 3;
    if (d[z] < 0)
    {
        uint swap = x;
        x = y;
        y = swap;
    }
    float3 a = sceneValue(t * 10).xyz - o, b = sceneValue(t * 10 + 1).xyz - o, c = sceneValue(t * 10 + 2).xyz - o;
    float sx = d[x] / d[z], sy = d[y] / d[z], sz = 1 / d[z];
    float ax = a[x] - sx * a[z], ay = a[y] - sy * a[z], bx = b[x] - sx * b[z], by = b[y] - sy * b[z],
          cx = c[x] - sx * c[z], cy = c[y] - sy * c[z];
    float e0 = bx * cy - by * cx, e1 = cx * ay - cy * ax, e2 = ax * by - ay * bx;
    float determinant = e0 + e1 + e2;
    distance = 0;
    uv = 0;
    if ((min(e0, min(e1, e2)) < 0 && max(e0, max(e1, e2)) > 0) || determinant == 0)
        return false;
    distance = (e0 * a[z] + e1 * b[z] + e2 * c[z]) * sz / determinant;
    uv = float2(e1, e2) / determinant;
    if (distance < nearT || distance > farT)
        return false;
    if (asuint(surface(t, 5).z) != 0 && texture(t, texcoord(t, uv)).w * surface(t, 2).w < surface(t, 1).y)
        return false;
    return true;
}
Hit trace(float3 o, float3 d, float nearT, float farT, bool visibility = false)
{
    Hit hit;
    hit.triangle = 0xffffffff;
    hit.distance = farT;
    hit.uv = 0;
    if (counts.y == 0)
        return hit;
    uint stack[64];
    uint pending = 1;
    stack[0] = 0;
    while (pending != 0)
    {
        uint node = stack[--pending], b = offsets0.x + node * 3;
        if (!box(o, d, nearT, hit.distance, node))
            continue;
        uint4 range = asuint(sceneValue(b + 2));
        if (range.y == 0)
        {
            if (pending > 62)
            {
                hit.triangle = 0xfffffffe;
                return hit;
            }
            stack[pending++] = asuint(sceneValue(b).w);
            stack[pending++] = asuint(sceneValue(b + 1).w);
            continue;
        }
        for (uint i = range.x; i < range.x + range.y; ++i)
        {
            uint t = asuint(sceneValue(offsets0.y + i).x);
            float distance;
            float2 uv;
            if (intersect(t, o, d, nearT, hit.distance, distance, uv) && (distance < hit.distance || t < hit.triangle))
            {
                hit.triangle = t;
                hit.distance = distance;
                hit.uv = uv;
                if (visibility)
                    return hit;
            }
        }
    }
    return hit;
}
// Thanks to Dirk Gerrit van Antwerpen, "Solving Self-Intersection Artifacts in
// DirectX Raytracing", NVIDIA Technical Blog (2023), "Error bounds":
// https://developer.nvidia.com/blog/solving-self-intersection-artifacts-in-directx-raytracing/
// Use the reference scene's conservative world-space error allowance, then the
// representable-neighbor offset convention from FoundationMath, not RTX bounds.
float3 offsetPoint(float3 p, uint t, float3 d)
{
    float3 n = sceneValue(t * 10 + 8).xyz, error = sceneValue(t * 10 + 9).xyz;
    float3 shift = n * dot(abs(n), error) * (dot(n, d) < 0 ? -1 : 1);
    float3 outp = p + shift;
    for (uint i = 0; i < 3; ++i)
        if (shift[i] != 0)
        {
            uint b = asuint(outp[i]);
            if (outp[i] == 0)
                b = shift[i] > 0 ? 0x00800000 : 0x80800000;
            else
                b += (outp[i] > 0) == (shift[i] > 0) ? 1u : 0xffffffffu;
            outp[i] = asfloat(b);
        }
    return outp;
}
float powerWeight(float a, float b)
{
    if (a <= 0)
        return 0;
    if (b <= 0)
        return 1;
    float m = max(a, b);
    a /= m;
    b /= m;
    return a * a / (a * a + b * b);
}
float areaPdf(uint t, float3 from, float3 to, uint lightCount)
{
    if (lightCount == 0)
        return 0;
    float3 delta = to - from;
    float cosine = abs(dot(sceneValue(t * 10 + 8).xyz, -unit(delta)));
    return cosine > 0 ? dot(delta, delta) / (float(lightCount) * sceneValue(t * 10 + 1).w * cosine) : 0;
}
void evaluate(float3 color,
              float metallic,
              float roughness,
              float3 n,
              float3 v,
              float3 l,
              out float3 value,
              out float pdf)
{
    value = 0;
    pdf = 0;
    float nv = dot(n, v), nl = dot(n, l);
    if (nv <= 0 || nl <= 0)
        return;
    float3 h = unit(v + l);
    float nh = saturate(dot(n, h)), vh = saturate(dot(v, h)), alpha = roughness * roughness, a2 = alpha * alpha;
    float denominator = (1 - nh * nh) + nh * nh * a2, D = a2 / (PI * denominator * denominator);
    float gv = sqrt(nv * nv * (1 - a2) + a2), gl = sqrt(nl * nl * (1 - a2) + a2),
          visibility = .5 / max(nl * gv + nv * gl, 1e-6);
    float3 f0 = .04 * (1 - metallic) + color * metallic;
    float term = 1 - vh;
    float3 F = f0 + (1 - f0) * term * term * term * term * term;
    value = F * (D * visibility) + color * (1 - F) * ((1 - metallic) / PI);
    float probability = metallic == 1 ? 1 : .5;
    pdf = probability * D / (2 * (nv + gv)) + (1 - probability) * nl / PI;
}
float3 sampleBsdf(float3 n, float3 v, float roughness, float probability, float3 u)
{
    if (dot(n, v) <= 0)
        return float3(0);
    float3 x = unit(cross(abs(n.z) < .999 ? float3(0, 0, 1) : float3(0, 1, 0), n)), y = cross(n, x);
    float radius = sqrt(u.y), angle = 2 * PI * u.z, dx = radius * cos(angle), dy = radius * sin(angle);
    float3 local;
    if (u.x >= probability)
        local = float3(dx, dy, sqrt(max(0, 1 - u.y)));
    else
    {
        float3 lv = float3(dot(v, x), dot(v, y), dot(v, n));
        float a = roughness * roughness;
        float3 stretch = unit(float3(a * lv.xy, lv.z));
        float len = dot(stretch.xy, stretch.xy);
        float3 tangent = len > 0 ? float3(-stretch.y, stretch.x, 0) / sqrt(len) : float3(1, 0, 0),
               bitangent = cross(stretch, tangent);
        float blend = .5 * (1 + stretch.z), wy = (1 - blend) * sqrt(max(0, 1 - dx * dx)) + blend * dy;
        float3 hemisphere = tangent * dx + bitangent * wy + stretch * sqrt(max(0, 1 - dx * dx - wy * wy));
        float3 hl = unit(float3(a * hemisphere.xy, max(0, hemisphere.z))), h = x * hl.x + y * hl.y + n * hl.z;
        return unit(h * (2 * dot(v, h)) - v);
    }
    return unit(x * local.x + y * local.y + n * local.z);
}
float3 integrate(uint pixel, uint sample)
{
    float2 jitter = (sampling.z & 4) != 0 ? float2(.5) : float2(random01(pixel, sample, 0), random01(pixel, sample, 1));
    float2 xy = (float2(pixel % dimensions.x, pixel / dimensions.x) + jitter) / float2(dimensions.xy);
    float nearZ = (sampling.z & 1) != 0 ? 1 : 0;
    float4 near = mul(clipToWorld, float4(xy.x * 2 - 1, 1 - xy.y * 2, nearZ, 1));
    near /= near.w;
    float4 far = mul(clipToWorld, float4(xy.x * 2 - 1, 1 - xy.y * 2, 1 - nearZ, 1));
    // Match RT1's far-plane/homogeneous direction, including non-default pinholes.
    float3 origin = eye.w != 0 ? near.xyz : eye.xyz;
    float3 dir = unit(far.w != 0 ? far.xyz / far.w - origin : far.xyz);
    float minimum = eye.w != 0 ? 0 : dot(near.xyz - origin, dir), maximum = options.x;
    if (far.w != 0)
        maximum = dot(far.xyz / far.w - origin, dir);
    else
    {
        maximum = 0;
        for (uint corner = 0; corner < 8; ++corner)
        {
            float3 p = float3((corner & 1) != 0 ? boundsMax.x : boundsMin.x,
                              (corner & 2) != 0 ? boundsMax.y : boundsMin.y,
                              (corner & 4) != 0 ? boundsMax.z : boundsMin.z);
            maximum = max(maximum, length(p - origin));
        }
        maximum = maximum * 1.000001 + 1;
    }
    float3 radiance = 0, throughput = 1, previousPoint = 0;
    float previousPdf = 0;
    uint lights = counts.z + (positive(environment.xyz) ? 1u : 0u);
    for (uint depth = 0; depth <= dimensions.z; ++depth)
    {
        Hit hit = trace(origin, dir, minimum, maximum);
        if (hit.triangle == 0xfffffffe)
            return float3(-1);
        if (hit.triangle == 0xffffffff)
        {
            float w = depth == 0 ? 1 : powerWeight(previousPdf, lights > 0 ? 1 / (float(lights) * 4 * PI) : 0);
            radiance += throughput * environment.xyz * w;
            break;
        }
        uint t = hit.triangle;
        float3 point = position(t, hit.uv), gn = sceneValue(t * 10 + 8).xyz, view = -dir, emission = surface(t, 4).xyz;
        radiance += throughput * emission *
                    (depth == 0 ? 1 : powerWeight(previousPdf, areaPdf(t, previousPoint, point, lights)));
        if (depth >= dimensions.z || dot(gn, view) <= 0)
            break;
        float3 n = unit(sceneValue(t * 10 + 3).xyz * (1 - hit.uv.x - hit.uv.y) + sceneValue(t * 10 + 4).xyz * hit.uv.x +
                        sceneValue(t * 10 + 5).xyz * hit.uv.y);
        float3 color = saturate(surface(t, 0).xyz * texture(t, texcoord(t, hit.uv)).xyz);
        float metallic = surface(t, 0).w, roughness = surface(t, 1).x;
        uint dimension = 2 + depth * 8;
        if (lights > 0)
        {
            uint selected = choose(pixel, sample, dimension, lights);
            if (selected == 0xffffffff)
                return float3(-1);
            float3 l, lightColor = 0, lightPoint = 0;
            float pdf = 0, range = options.x;
            uint emitter = 0xffffffff;
            if (selected < counts.z)
            {
                emitter = asuint(sceneValue(offsets0.z + selected).x);
                float root = sqrt(random01(pixel, sample, dimension + 1));
                float2 uv = float2(root * (1 - random01(pixel, sample, dimension + 2)), 0);
                uv.y = root - uv.x;
                lightPoint = position(emitter, uv);
                float3 delta = lightPoint - point;
                range = length(delta);
                l = unit(delta);
                pdf = areaPdf(emitter, point, lightPoint, lights);
                if (range <= options.x &&
                    (asuint(surface(emitter, 5).z) == 0 ||
                     texture(emitter, texcoord(emitter, uv)).w * surface(emitter, 2).w >= surface(emitter, 1).y))
                    lightColor = surface(emitter, 4).xyz;
            }
            else
            {
                float z = 1 - 2 * random01(pixel, sample, dimension + 1), r = sqrt(max(0, 1 - z * z)),
                      a = 2 * PI * random01(pixel, sample, dimension + 2);
                l = float3(r * cos(a), r * sin(a), z);
                pdf = 1 / (float(lights) * 4 * PI);
                lightColor = environment.xyz;
            }
            float3 value;
            float bsdfPdf;
            evaluate(color, metallic, roughness, n, view, l, value, bsdfPdf);
            if (pdf > 0 && positive(lightColor) && dot(n, l) > 0 && dot(gn, l) > 0)
            {
                float3 start = offsetPoint(point, t, l), direction = l;
                float distance = range;
                if (emitter != 0xffffffff)
                {
                    float3 end = offsetPoint(lightPoint, emitter, -l), delta = end - start;
                    distance = length(delta);
                    direction = unit(delta);
                    distance = asfloat(asuint(distance) - 1);
                }
                Hit shadow = trace(start, direction, 0, distance, true);
                if (shadow.triangle == 0xfffffffe)
                    return float3(-1);
                if (shadow.triangle == 0xffffffff)
                    radiance += throughput * value * lightColor * (dot(n, l) * powerWeight(pdf, bsdfPdf) / pdf);
            }
        }
        float3 outgoing = sampleBsdf(n,
                                     view,
                                     roughness,
                                     metallic == 1 ? 1 : .5,
                                     float3(random01(pixel, sample, dimension + 3),
                                            random01(pixel, sample, dimension + 4),
                                            random01(pixel, sample, dimension + 5)));
        if (dot(n, outgoing) <= 0 || dot(gn, outgoing) <= 0)
            break;
        float3 value;
        float pdf;
        evaluate(color, metallic, roughness, n, view, outgoing, value, pdf);
        if (pdf <= 0)
            break;
        throughput *= value * (dot(n, outgoing) / pdf);
        if (!positive(throughput))
            break;
        if (depth + 1 >= dimensions.w)
        {
            float keep = clamp(max(throughput.x, max(throughput.y, throughput.z)), .05, .95);
            if (random01(pixel, sample, dimension + 6) >= keep)
                break;
            throughput /= keep;
        }
        previousPdf = pdf;
        previousPoint = point;
        origin = offsetPoint(point, t, outgoing);
        dir = outgoing;
        minimum = 0;
        maximum = options.x;
    }
    return radiance;
}
[numthreads(64, 1, 1)] void computeMain(uint3 id
                                        : SV_DispatchThreadID) {
    if (id.x >= dimensions.x || id.y >= dimensions.y)
        return;
    uint pixel = id.y * dimensions.x + id.x;
    float4 sum = accumulation[pixel * 2], compensation = accumulation[pixel * 2 + 1];
    if (sum.w < float(sampling.w) && compensation.w == 0)
    {
        float3 sample = integrate(pixel, uint(sum.w));
        if (any(!isfinite(sample)) || any(sample < 0))
            compensation.w = 1;
        else
        {
            float3 adjusted = sample - compensation.xyz, next = sum.xyz + adjusted;
            compensation.xyz = (next - sum.xyz) - adjusted;
            if (any(!isfinite(next)) || any(!isfinite(compensation.xyz)))
                compensation.w = 1;
            else
            {
                sum.xyz = next;
                sum.w += 1;
            }
        }
        accumulation[pixel * 2] = sum;
        accumulation[pixel * 2 + 1] = compensation;
    }
    float3 mean = sum.w > 0 ? sum.xyz / sum.w : float3(0), linear = mean * options.y;
    for (uint channel = 0; channel < 3; ++channel)
    {
        // Finite nonnegative radiance can overflow when exposure is applied.
        // Reinhard tends to one there; do not encode an infinity/infinity NaN.
        linear[channel] = isfinite(linear[channel]) ? linear[channel] / (1 + linear[channel]) : 1;
    }
    float3 encoded = select(linear > .0031308, 1.055 * pow(linear, 1.0 / 2.4) - .055, linear * 12.92);
    colors[pixel] = float4(compensation.w != 0 ? float3(1, 0, 1) : saturate(encoded), float(pixel));
}
