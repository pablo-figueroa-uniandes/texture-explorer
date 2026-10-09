#include "Inspector.h"

using simd::float3;

float3 BumpNormalTangent(const MapImage& bump, float u, float v, float strength, simd::float2* slopeOut) {
    const float du = 1.0f / bump.width, dv = 1.0f / bump.height;
    // Height change per texel, by central differences.
    const float dhdx = 0.5f * (bump.Sample(u + du, v).x - bump.Sample(u - du, v).x);
    const float dhdy = 0.5f * (bump.Sample(u, v + dv).x - bump.Sample(u, v - dv).x);
    if (slopeOut) *slopeOut = {dhdx, dhdy};
    // The surface z = k*h(x,y) has normal (-k*dh/dx, -k*dh/dy, 1).
    return simd::normalize(float3{-strength * dhdx, -strength * dhdy, 1});
}

HoverInfo Inspect(const Material& m, float u, float v, Shape shape, const simd::float4x4& world, const BumpSettings& s) {
    HoverInfo h;
    h.valid = true;
    h.uv = {u, v};
    if (auto& c = m.maps[MapColor]) h.color = c->Sample(u, v).xyz;
    if (auto& b = m.maps[MapBump]) h.bump = b->Sample(u, v).x;
    if (auto& d = m.maps[MapDisplacement]) h.displacement = d->Sample(u, v).x;
    if (auto& r = m.maps[MapRoughness]) h.roughness = r->Sample(u, v).x;
    h.normalTangent = m.maps[MapBump] ? BumpNormalTangent(*m.maps[MapBump], u, v, s.strength, &h.slope)
                                      : float3{0, 0, 1};

    SurfacePoint sp;
    if (!FindSurfacePoint(shape, u, v, sp)) return h;
    h.onSurface = true;
    float3 t, b, n;
    TangentFrame(sp, t, b, n);
    // Tangent space -> object space: columns of the TBN matrix are T, B, N.
    const float3& nt = h.normalTangent;
    const float3 nObj = t * nt.x + b * nt.y + n * nt.z;
    float3 p = sp.pos;
    if (s.useDisplacement && m.maps[MapDisplacement])
        p += n * (s.displacementScale * (h.displacement - s.displacementMid));
    // Object -> world. The world matrix is a pure rotation, so it also transforms normals.
    h.position = TransformPoint(world, p);
    h.normalGeom = simd::normalize(TransformNormal(world, n));
    h.normalBumped = simd::normalize(TransformNormal(world, nObj));
    h.tangent = simd::normalize(TransformNormal(world, t));
    h.bitangent = simd::normalize(TransformNormal(world, b));
    return h;
}
