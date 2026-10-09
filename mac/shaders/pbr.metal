// Texture Explorer (macOS) - main surface shader, a translation of shaders/pbr.hlsl.
// Edit and press F5 or Cmd+R (or "Reload shaders") to recompile while the program runs.
#include <metal_stdlib>
using namespace metal;

// Mirrors FrameConstants in mac/src/Renderer.h. packed_float3 + float fills one 16-byte
// row, exactly like float3 + float in an HLSL cbuffer.
struct Frame
{
    float4x4 world;               // object -> world (a rotation)
    float4x4 viewProj;            // world -> clip
    packed_float3 cameraPos;   float bumpStrength;
    packed_float3 lightPos;    float lightIntensity;
    packed_float3 lightColor;  float dispScale;
    float  dispMid;     float dispLod;  float metallic;  float ambient;
    int    viewMode;    int useColor;   int useBump;     int useDisp;
    int    useRough;    float constRoughness;  float2 hoverUV;
    float  hoverOn;     packed_float3 pad;
};

// Textures and the sampler are arguments of the shader functions, bound by index:
//   texture(0) colorMap  sRGB view -> linear albedo
//   texture(1) bumpMap   height used only for shading
//   texture(2) dispMap   height used to move vertices
//   texture(3) roughMap  specular roughness
//   sampler(0) linearWrap,  buffer(0) vertices,  buffer(1) Frame

struct VSIn  { float3 pos [[attribute(0)]]; float3 nrm [[attribute(1)]]; float4 tan [[attribute(2)]]; float2 uv [[attribute(3)]]; };
struct VSOut { float4 svpos [[position]]; float3 wpos; float3 nrm; float4 tan; float2 uv; };

vertex VSOut VSMain(VSIn i [[stage_in]], constant Frame& f [[buffer(1)]],
                    texture2d<float> dispMap [[texture(2)]], sampler linearWrap [[sampler(0)]])
{
    float3 p = i.pos;
    if (f.useDisp != 0)
    {
        // Displacement mapping: move the vertex along its normal by the height.
        float h = dispMap.sample(linearWrap, i.uv, level(f.dispLod)).r;
        p += i.nrm * f.dispScale * (h - f.dispMid);
    }
    VSOut o;
    float4 wp = f.world * float4(p, 1);
    o.svpos = f.viewProj * wp;
    o.wpos  = wp.xyz;
    o.nrm   = (f.world * float4(i.nrm, 0)).xyz;
    o.tan   = float4((f.world * float4(i.tan.xyz, 0)).xyz, i.tan.w);
    o.uv    = i.uv;
    return o;
}

// Normal of the height field z = k*h(x,y), in tangent space, by central differences
// (one texel each side). Same formula as BumpNormalTangent() in mac/src/Inspector.cpp.
float3 BumpNormalTS(texture2d<float> bumpMap, sampler linearWrap, float2 uv, float bumpStrength)
{
    float2 t = 1.0 / float2(bumpMap.get_width(), bumpMap.get_height());
    float dhdx = 0.5 * (bumpMap.sample(linearWrap, uv + float2(t.x, 0), level(0)).r -
                        bumpMap.sample(linearWrap, uv - float2(t.x, 0), level(0)).r);
    float dhdy = 0.5 * (bumpMap.sample(linearWrap, uv + float2(0, t.y), level(0)).r -
                        bumpMap.sample(linearWrap, uv - float2(0, t.y), level(0)).r);
    return normalize(float3(-bumpStrength * dhdx, -bumpStrength * dhdy, 1));
}

constant float PI = 3.14159265;

float3 SrgbToLinear(float3 c) { return pow(saturate(c), 2.2); }  // so debug colors display unchanged

fragment float4 PSMain(VSOut i [[stage_in]], constant Frame& f [[buffer(1)]],
                       texture2d<float> colorMap [[texture(0)]], texture2d<float> bumpMap [[texture(1)]],
                       texture2d<float> roughMap [[texture(3)]], sampler linearWrap [[sampler(0)]])
{
    // Tangent frame (TBN). B is rebuilt from N and T with the stored handedness.
    float3 N = normalize(i.nrm);
    float3 T = normalize(i.tan.xyz - N * dot(N, i.tan.xyz));
    float3 B = i.tan.w * cross(N, T);

    float3 nTS = f.useBump != 0 ? BumpNormalTS(bumpMap, linearWrap, i.uv, f.bumpStrength) : float3(0, 0, 1);
    float3 n = normalize(T * nTS.x + B * nTS.y + N * nTS.z);

    float3 albedo = f.useColor != 0 ? colorMap.sample(linearWrap, i.uv).rgb : float3(0.7, 0.7, 0.7);
    float rough = f.useRough != 0 ? roughMap.sample(linearWrap, i.uv).r : f.constRoughness;
    rough = clamp(rough, 0.04, 1.0);

    float3 dbg = float3(-1, -1, -1);
    if (f.viewMode == 1) dbg = pow(saturate(albedo), 1 / 2.2);
    else if (f.viewMode == 2) dbg = n * 0.5 + 0.5;
    else if (f.viewMode == 3) dbg = nTS * 0.5 + 0.5;
    else if (f.viewMode == 4) dbg = float3(rough);
    else if (f.viewMode == 5) dbg = float3(bumpMap.sample(linearWrap, i.uv).r);
    else if (f.viewMode == 6) dbg = float3(fract(i.uv), 0);

    float3 color;
    if (dbg.x >= 0)
    {
        color = SrgbToLinear(dbg);
    }
    else
    {
        // Cook-Torrance microfacet BRDF: GGX distribution, Smith-Schlick geometry, Schlick Fresnel.
        float3 V = normalize(float3(f.cameraPos) - i.wpos);
        float3 L = normalize(float3(f.lightPos) - i.wpos);
        float3 H = normalize(V + L);
        float NdotL = saturate(dot(n, L));
        float NdotV = max(dot(n, V), 1e-4);
        float NdotH = saturate(dot(n, H));
        float VdotH = saturate(dot(V, H));

        float a  = rough * rough;
        float a2 = a * a;
        float d  = NdotH * NdotH * (a2 - 1) + 1;
        float D  = a2 / (PI * d * d);
        float k  = (rough + 1) * (rough + 1) / 8;
        float G  = (NdotV / (NdotV * (1 - k) + k)) * (NdotL / (NdotL * (1 - k) + k));
        float3 F0 = mix(float3(0.04, 0.04, 0.04), albedo, f.metallic);
        float3 F  = F0 + (1 - F0) * pow(1 - VdotH, 5);

        float3 spec = D * G * F / (4 * NdotV * max(NdotL, 1e-4));
        float3 kd = (1 - F) * (1 - f.metallic);
        float3 radiance = float3(f.lightColor) * f.lightIntensity;
        // Geometric self-shadowing: no light where the unperturbed surface faces away.
        float horizon = saturate(dot(N, L) * 8);
        color = (kd * albedo / PI + spec) * radiance * NdotL * horizon + f.ambient * albedo;
    }

    // Highlight the texel under the mouse in the inspector.
    if (f.hoverOn > 0)
    {
        float2 duv = abs(i.uv - f.hoverUV);
        float r = length(duv);
        if (r > 0.010 && r < 0.016) color = float3(1, 0.1, 0.8);
    }
    return float4(color, 1);
}
