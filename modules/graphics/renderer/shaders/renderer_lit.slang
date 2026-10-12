// Thanks to Google/Filament authors, Physically Based Rendering in Filament,
// sections 4.4 and 5.2.2, https://google.github.io/filament/main/filament.html.
// GGX + correlated Smith + Schlick, Fresnel-weighted Lambert, single scattering.
// Constant environment is a declared Fresnel approximation, not a DFG/LUT integrator.
struct Parameters
{
    float4 Tint;
    float4 UvTransform;
    float4 Options;
    float4 Pbr;
    float4 Emission;
    float4 EnvironmentIrradiance;
    float4 EnvironmentRadiance;
    float4 BakeScale;
    float4 BakeUv;
#ifdef LUDUS_CLUSTERED
    float4 Lights[512];
#else
    float4 Lights[32];
#endif
    float4 ShadowData[24];
#ifdef LUDUS_CLUSTERED
    float4 GridRows[3];
    float4 GridMinimum;
    float4 GridScale;
#endif
};
[[vk::binding(0,0)]] ConstantBuffer<Parameters> material : register(b0);
[[vk::binding(1,0)]] Texture2D<float4> colorTexture : register(t1);
[[vk::binding(2,0)]] SamplerState colorSampler : register(s2);
[[vk::binding(3,0)]] Texture2D<float4> bakeTexture : register(t3);
[[vk::binding(4,0)]] Texture2D<float4> shadowTexture : register(t4);
[[vk::binding(5,0)]] SamplerState shadowSampler : register(s5);
#ifdef LUDUS_CLUSTERED
[[vk::binding(6,0)]] Texture2D<float4> clusterTexture : register(t6);
[[vk::binding(7,0)]] SamplerState clusterSampler : register(s7);
#endif
struct Input
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float4 row0 : TEXCOORD0;
    [[vk::location(2)]] float4 row1 : TEXCOORD1;
    [[vk::location(3)]] float4 row2 : TEXCOORD2;
    [[vk::location(4)]] float4 row3 : TEXCOORD3;
    [[vk::location(5)]] float4 color : COLOR0;
    [[vk::location(6)]] float2 uv : TEXCOORD4;
    [[vk::location(7)]] float3 normal : NORMAL;
    [[vk::location(8)]] float4 world0 : TEXCOORD5;
    [[vk::location(9)]] float4 world1 : TEXCOORD6;
    [[vk::location(10)]] float4 world2 : TEXCOORD7;
    [[vk::location(11)]] float4 normal0 : TEXCOORD8;
    [[vk::location(12)]] float4 normal1 : TEXCOORD9;
    [[vk::location(13)]] float4 normal2 : TEXCOORD10;
    [[vk::location(14)]] float4 eye : TEXCOORD11;
    [[vk::location(15)]] float2 bakeUv : TEXCOORD12;
};
struct Varying
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float3 world : TEXCOORD1;
    float3 normal : TEXCOORD2;
    float4 eye : TEXCOORD3;
    float2 bakeUv : TEXCOORD4;
    float bakeValid : TEXCOORD5;
#ifdef LUDUS_CLUSTERED
    float directReference : TEXCOORD6;
#endif
};
float3 safeNormal(float3 value)
{
    float scale = max(max(abs(value.x),abs(value.y)),abs(value.z));
    float3 scaled = value / max(scale,1e-30);
    return scaled / sqrt(max(dot(scaled,scaled),1e-30));
}
[shader("vertex")]
Varying vertexMain(Input input)
{
    Varying output;
    float4 p = float4(input.position,1);
    output.position = float4(dot(input.row0,p),dot(input.row1,p),dot(input.row2,p),dot(input.row3,p));
    output.world = float3(dot(input.world0,p),dot(input.world1,p),dot(input.world2,p));
    output.normal = safeNormal(float3(dot(input.normal0.xyz,input.normal),dot(input.normal1.xyz,input.normal),dot(input.normal2.xyz,input.normal)));
    output.uv = input.uv;
    output.eye = input.eye;
    output.bakeUv = input.bakeUv;
    output.bakeValid = input.color.x;
#ifdef LUDUS_CLUSTERED
    output.directReference = input.color.y;
#endif
    return output;
}
float fifth(float x) { float square = x*x; return square*square*x; }
float3 brdf(float3 base, float3 n, float3 v, float3 l, bool bakedDiffuse)
{
    float nv = max(dot(n,v),0), nl = max(dot(n,l),0);
    float3 sum = v+l;
    if (nv <= 0 || nl <= 0 || dot(sum,sum) < 1e-12) return float3(0);
    float3 h = safeNormal(sum);
    float nh = saturate(dot(n,h)), lh = saturate(dot(l,h));
    float a = material.Pbr.y * material.Pbr.y, a2 = a*a;
    float d = nh*nh*(a2-1)+1;
    float distribution = a2 / (3.141592653589793 * d*d);
    float visibility = 0.5 / max(nl*sqrt(nv*nv*(1-a2)+a2)+nv*sqrt(nl*nl*(1-a2)+a2),1e-6);
    float3 f0 = lerp(float3(0.04),base,material.Pbr.x);
    float3 fresnel = f0+(1-f0)*fifth(1-lh);
    return nl*(distribution*visibility*fresnel+base*(1-material.Pbr.x)*(1-fresnel)*(bakedDiffuse ? 0 : 1)/3.141592653589793);
}
float shadowVisibility(int light, Varying input)
{
    if ((int(input.eye.w) & 1) == 0) return 1;
    for (int region = 0; region < 4; ++region)
    {
        if (region >= int(material.Pbr.w)) break;
        float4 settings = material.ShadowData[region*6+5];
        if (int(settings.y) != light) continue;
        float4 p = float4(input.world,1);
        float4 clip = float4(dot(material.ShadowData[region*6],p),dot(material.ShadowData[region*6+1],p),dot(material.ShadowData[region*6+2],p),dot(material.ShadowData[region*6+3],p));
        if (clip.w <= 1e-6) continue;
        float3 ndc = clip.xyz / clip.w;
        if (abs(ndc.x) > 1 || abs(ndc.y) > 1 || ndc.z < 0 || ndc.z > 1) continue;
        float4 rect = material.ShadowData[region*6+4];
        float2 uv = lerp(rect.xy,rect.zw,float2(ndc.x*0.5+0.5,0.5-ndc.y*0.5));
        float visible = 0;
        for (int tap = 0; tap < 4; ++tap)
        {
            float2 delta = float2((tap & 1) != 0 ? 0.5 : -0.5,(tap & 2) != 0 ? 0.5 : -0.5)*settings.zw;
            float2 sampleUv = clamp(uv+delta,rect.xy,rect.zw);
            float2 depth = shadowTexture.SampleLevel(shadowSampler,sampleUv,0).rg;
            float stored = dot(depth,float2(65280,255))/65535;
            visible += ndc.z-settings.x <= stored ? 0.25 : 0;
        }
        return visible;
    }
    return 1; // Outside the declared cascade/local coverage remains unshadowed.
}
float4 baseTexel(Varying input)
{
    float2 uv = input.uv*material.UvTransform.xy+material.UvTransform.zw;
    float4 texel = material.Options.y < 0 ? colorTexture.Sample(colorSampler,uv) : colorTexture.SampleLevel(colorSampler,uv,material.Options.y);
    float4 base = texel*material.Tint;
    if (material.Options.z == 1 && base.a < material.Options.x) discard;
    return base;
}
[shader("fragment")]
float4 fragmentMain(Varying input) : SV_Target
{
    float4 base = baseTexel(input);
    float3 n = safeNormal(input.normal), v = input.eye.w >= 2 ? safeNormal(input.eye.xyz) : safeNormal(input.eye.xyz-input.world);
    float nv = max(dot(n,v),0);
    float3 f0 = lerp(float3(0.04),base.rgb,material.Pbr.x);
    float3 f = f0+(1-f0)*fifth(1-nv);
    float3 irradiance = material.EnvironmentIrradiance.rgb;
    if (input.bakeValid > 0.5)
        irradiance = bakeTexture.SampleLevel(colorSampler,saturate(input.bakeUv)*material.BakeUv.xy+material.BakeUv.zw,0).rgb*material.BakeScale.rgb;
    float3 result = material.Emission.rgb;
    if (nv > 0)
        result += irradiance*base.rgb*(1-material.Pbr.x)*(1-f)/3.141592653589793 + material.EnvironmentRadiance.rgb*f;
#ifdef LUDUS_CLUSTERED
    float4 world = float4(input.world,1);
    float3 grid = (float3(dot(material.GridRows[0],world),dot(material.GridRows[1],world),dot(material.GridRows[2],world))-material.GridMinimum.xyz)*material.GridScale.xyz;
    bool direct = input.directReference > 0.5 || any(grid < 0) || any(grid >= 8);
    int3 cell = int3(clamp(grid,float3(0),float3(7)));
    int start = (cell.x+cell.y*8+cell.z*64)*32;
    float4 first = clusterTexture.SampleLevel(clusterSampler,(float2((start/4)%64,(start/4)/64)+0.5)/64,0);
    direct = direct || int(first.x*255+0.5) == 255;
    for (int slot = 0; slot < 128; ++slot)
    {
        int i = slot;
        if (direct)
        {
            if (slot >= int(material.Pbr.z)) break;
        }
        else
        {
            if (slot >= 32) break;
            int at = start+slot;
            float4 packed = clusterTexture.SampleLevel(clusterSampler,(float2((at/4)%64,(at/4)/64)+0.5)/64,0);
            i = int(packed[at%4]*255+0.5)-1;
            if (i < 0) break;
        }
#else
    for (int i = 0; i < 8; ++i)
    {
        if (i >= int(material.Pbr.z)) break;
#endif
        float4 color = material.Lights[i*4], position = material.Lights[i*4+1], direction = material.Lights[i*4+2], extra = material.Lights[i*4+3];
        float3 l = -direction.xyz;
        float attenuation = 1;
        if (color.w != 0)
        {
            float3 delta = position.xyz-input.world;
            float distance2 = dot(delta,delta);
            l = safeNormal(delta);
            float ratio = distance2/(position.w*position.w);
            float edge = max(1-ratio*ratio,0);
            attenuation = edge*edge/max(distance2,1e-4);
            if (color.w == 2)
            {
                float cone = saturate((dot(-l,direction.xyz)-direction.w)/max(extra.x-direction.w,1e-6));
                attenuation *= cone*cone;
            }
        }
        result += brdf(base.rgb,n,v,l,input.bakeValid > 0.5 && extra.y > 0.5)*color.rgb*attenuation*shadowVisibility(i,input);
    }
    float alpha = material.Options.z == 2 ? base.a : 1;
    return float4(min(max(result,float3(0)),float3(65504))*alpha,alpha);
}
[shader("fragment")]
float4 shadowMain(Varying input) : SV_Target
{
    baseTexel(input);
    float encoded = floor(saturate(input.position.z)*65535+0.5);
    float high = floor(encoded/256);
    return float4(high/255,(encoded-high*256)/255,0,1);
}
