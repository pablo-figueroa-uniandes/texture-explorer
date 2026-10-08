// Texture Explorer - main surface shader.
// Edit and press F5 (or "Reload shaders") to recompile while the program runs.

cbuffer Frame : register(b0)
{
    row_major float4x4 world;     // object -> world (a rotation)
    row_major float4x4 viewProj;  // world -> clip
    float3 cameraPos;   float bumpStrength;
    float3 lightPos;    float lightIntensity;
    float3 lightColor;  float dispScale;
    float  dispMid;     float dispLod;  float metallic;  float ambient;
    int    viewMode;    int useColor;   int useBump;     int useDisp;
    int    useRough;    float constRoughness;  float2 hoverUV;
    float  hoverOn;     float3 pad;
};

Texture2D    colorMap : register(t0);  // sRGB view -> linear albedo
Texture2D    bumpMap  : register(t1);  // height used only for shading
Texture2D    dispMap  : register(t2);  // height used to move vertices
Texture2D    roughMap : register(t3);  // specular roughness
SamplerState linearWrap : register(s0);

struct VSIn  { float3 pos : POSITION; float3 nrm : NORMAL; float4 tan : TANGENT; float2 uv : TEXCOORD; };
struct VSOut { float4 svpos : SV_Position; float3 wpos : WORLDPOS; float3 nrm : NORMAL; float4 tan : TANGENT; float2 uv : TEXCOORD; };

VSOut VSMain(VSIn i)
{
    float3 p = i.pos;
    if (useDisp != 0)
    {
        // Displacement mapping: move the vertex along its normal by the height.
        float h = dispMap.SampleLevel(linearWrap, i.uv, dispLod).r;
        p += i.nrm * dispScale * (h - dispMid);
    }
    VSOut o;
    float4 wp = mul(float4(p, 1), world);
    o.svpos = mul(wp, viewProj);
    o.wpos  = wp.xyz;
    o.nrm   = mul(float4(i.nrm, 0), world).xyz;
    o.tan   = float4(mul(float4(i.tan.xyz, 0), world).xyz, i.tan.w);
    o.uv    = i.uv;
    return o;
}

// Normal of the height field z = k*h(x,y), in tangent space, by central differences
// (one texel each side). Same formula as BumpNormalTangent() in src/Inspector.cpp.
float3 BumpNormalTS(float2 uv)
{
    float w, h;
    bumpMap.GetDimensions(w, h);
    float2 t = 1.0 / float2(w, h);
    float dhdx = 0.5 * (bumpMap.SampleLevel(linearWrap, uv + float2(t.x, 0), 0).r -
                        bumpMap.SampleLevel(linearWrap, uv - float2(t.x, 0), 0).r);
    float dhdy = 0.5 * (bumpMap.SampleLevel(linearWrap, uv + float2(0, t.y), 0).r -
                        bumpMap.SampleLevel(linearWrap, uv - float2(0, t.y), 0).r);
    return normalize(float3(-bumpStrength * dhdx, -bumpStrength * dhdy, 1));
}

static const float PI = 3.14159265;

float3 SrgbToLinear(float3 c) { return pow(saturate(c), 2.2); }  // so debug colors display unchanged

float4 PSMain(VSOut i) : SV_Target
{
    // Tangent frame (TBN). B is rebuilt from N and T with the stored handedness.
    float3 N = normalize(i.nrm);
    float3 T = normalize(i.tan.xyz - N * dot(N, i.tan.xyz));
    float3 B = i.tan.w * cross(N, T);

    float3 nTS = useBump != 0 ? BumpNormalTS(i.uv) : float3(0, 0, 1);
    float3 n = normalize(T * nTS.x + B * nTS.y + N * nTS.z);

    float3 albedo = useColor != 0 ? colorMap.Sample(linearWrap, i.uv).rgb : float3(0.7, 0.7, 0.7);
    float rough = useRough != 0 ? roughMap.Sample(linearWrap, i.uv).r : constRoughness;
    rough = clamp(rough, 0.04, 1.0);

    float3 dbg = float3(-1, -1, -1);
    if (viewMode == 1) dbg = pow(saturate(albedo), 1 / 2.2);
    else if (viewMode == 2) dbg = n * 0.5 + 0.5;
    else if (viewMode == 3) dbg = nTS * 0.5 + 0.5;
    else if (viewMode == 4) dbg = rough.xxx;
    else if (viewMode == 5) dbg = bumpMap.Sample(linearWrap, i.uv).rrr;
    else if (viewMode == 6) dbg = float3(frac(i.uv), 0);

    float3 color;
    if (dbg.x >= 0)
    {
        color = SrgbToLinear(dbg);
    }
    else
    {
        // Cook-Torrance microfacet BRDF: GGX distribution, Smith-Schlick geometry, Schlick Fresnel.
        float3 V = normalize(cameraPos - i.wpos);
        float3 L = normalize(lightPos - i.wpos);
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
        float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
        float3 F  = F0 + (1 - F0) * pow(1 - VdotH, 5);

        float3 spec = D * G * F / (4 * NdotV * max(NdotL, 1e-4));
        float3 kd = (1 - F) * (1 - metallic);
        float3 radiance = lightColor * lightIntensity;
        // Geometric self-shadowing: no light where the unperturbed surface faces away.
        float horizon = saturate(dot(N, L) * 8);
        color = (kd * albedo / PI + spec) * radiance * NdotL * horizon + ambient * albedo;
    }

    // Highlight the texel under the mouse in the inspector.
    if (hoverOn > 0)
    {
        float2 duv = abs(i.uv - hoverUV);
        float r = length(duv);
        if (r > 0.010 && r < 0.016) color = float3(1, 0.1, 0.8);
    }
    return float4(color, 1);
}
