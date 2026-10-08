// Texture Explorer - colored lines (light marker, normal/tangent arrows).

cbuffer Frame : register(b0)
{
    row_major float4x4 world;
    row_major float4x4 viewProj;
};

struct VSIn  { float3 pos : POSITION; float4 color : COLOR; };
struct VSOut { float4 svpos : SV_Position; float4 color : COLOR; };

VSOut VSLine(VSIn i)
{
    VSOut o;
    o.svpos = mul(float4(i.pos, 1), viewProj);
    o.color = i.color;
    return o;
}

float4 PSLine(VSOut i) : SV_Target
{
    return float4(pow(saturate(i.color.rgb), 2.2), i.color.a);  // colors are given in display (sRGB) space
}
