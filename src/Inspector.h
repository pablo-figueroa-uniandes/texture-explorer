#pragma once
#include "Material.h"
#include "Mesh.h"

struct BumpSettings {
    float strength = 4.0f;          // slope multiplier, per texel of the bump map
    float displacementScale = 0.05f;
    float displacementMid = 0.5f;   // height value that leaves the surface in place
    bool useDisplacement = true;
};

struct HoverInfo {
    bool valid = false;
    DirectX::XMFLOAT2 uv{};
    DirectX::XMFLOAT3 color{};
    float bump = 0, displacement = 0, roughness = 0;
    DirectX::XMFLOAT2 slope{};      // dh/dx, dh/dy in texels (central differences)
    DirectX::XMFLOAT3 normalTangent{};
    // World-space quantities, available when the uv lands on the current shape.
    bool onSurface = false;
    DirectX::XMFLOAT3 position{}, normalGeom{}, normalBumped{}, tangent{}, bitangent{};
};

// Normal from a height (bump) map, in tangent space. This is the exact formula the
// pixel shader uses (see shaders/pbr.hlsl, BumpNormalTS).
DirectX::XMFLOAT3 BumpNormalTangent(const MapImage& bump, float u, float v, float strength,
                                    DirectX::XMFLOAT2* slopeOut = nullptr);

HoverInfo Inspect(const Material& m, float u, float v, Shape shape, DirectX::FXMMATRIX world,
                  const BumpSettings& s);
