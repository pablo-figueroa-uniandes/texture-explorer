#include "Inspector.h"

using namespace DirectX;

XMFLOAT3 BumpNormalTangent(const MapImage& bump, float u, float v, float strength, XMFLOAT2* slopeOut) {
    const float du = 1.0f / bump.width, dv = 1.0f / bump.height;
    // Height change per texel, by central differences.
    const float dhdx = 0.5f * (bump.Sample(u + du, v).x - bump.Sample(u - du, v).x);
    const float dhdy = 0.5f * (bump.Sample(u, v + dv).x - bump.Sample(u, v - dv).x);
    if (slopeOut) *slopeOut = {dhdx, dhdy};
    // The surface z = k*h(x,y) has normal (-k*dh/dx, -k*dh/dy, 1).
    XMFLOAT3 n;
    XMStoreFloat3(&n, XMVector3Normalize(XMVectorSet(-strength * dhdx, -strength * dhdy, 1, 0)));
    return n;
}

HoverInfo Inspect(const Material& m, float u, float v, Shape shape, FXMMATRIX world, const BumpSettings& s) {
    HoverInfo h;
    h.valid = true;
    h.uv = {u, v};
    if (auto& c = m.maps[MapColor]) { const XMFLOAT4 x = c->Sample(u, v); h.color = {x.x, x.y, x.z}; }
    if (auto& b = m.maps[MapBump]) h.bump = b->Sample(u, v).x;
    if (auto& d = m.maps[MapDisplacement]) h.displacement = d->Sample(u, v).x;
    if (auto& r = m.maps[MapRoughness]) h.roughness = r->Sample(u, v).x;
    h.normalTangent = m.maps[MapBump] ? BumpNormalTangent(*m.maps[MapBump], u, v, s.strength, &h.slope)
                                      : XMFLOAT3{0, 0, 1};

    SurfacePoint sp;
    if (!FindSurfacePoint(shape, u, v, sp)) return h;
    h.onSurface = true;
    XMFLOAT3 T, B, N;
    TangentFrame(sp, T, B, N);
    const XMVECTOR t = XMLoadFloat3(&T), b = XMLoadFloat3(&B), n = XMLoadFloat3(&N);
    // Tangent space -> object space: columns of the TBN matrix are T, B, N.
    const XMFLOAT3& nt = h.normalTangent;
    const XMVECTOR nObj = t * nt.x + b * nt.y + n * nt.z;
    XMVECTOR p = XMLoadFloat3(&sp.pos);
    if (s.useDisplacement && m.maps[MapDisplacement])
        p += n * (s.displacementScale * (h.displacement - s.displacementMid));
    // Object -> world. The world matrix is a pure rotation, so it also transforms normals.
    XMStoreFloat3(&h.position, XMVector3TransformCoord(p, world));
    XMStoreFloat3(&h.normalGeom, XMVector3Normalize(XMVector3TransformNormal(n, world)));
    XMStoreFloat3(&h.normalBumped, XMVector3Normalize(XMVector3TransformNormal(nObj, world)));
    XMStoreFloat3(&h.tangent, XMVector3Normalize(XMVector3TransformNormal(t, world)));
    XMStoreFloat3(&h.bitangent, XMVector3Normalize(XMVector3TransformNormal(b, world)));
    return h;
}
