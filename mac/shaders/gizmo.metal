// Texture Explorer (macOS) - colored lines (light marker, normal/tangent arrows).
#include <metal_stdlib>
using namespace metal;

struct Frame
{
    float4x4 world;
    float4x4 viewProj;
};

struct VSIn  { float3 pos [[attribute(0)]]; float4 color [[attribute(1)]]; };
struct VSOut { float4 svpos [[position]]; float4 color; };

vertex VSOut VSLine(VSIn i [[stage_in]], constant Frame& f [[buffer(1)]])
{
    VSOut o;
    o.svpos = f.viewProj * float4(i.pos, 1);
    o.color = i.color;
    return o;
}

fragment float4 PSLine(VSOut i [[stage_in]])
{
    return float4(pow(saturate(i.color.rgb), 2.2), i.color.a);  // colors are given in display (sRGB) space
}
