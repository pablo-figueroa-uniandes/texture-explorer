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
    simd::float2 uv{};
    simd::float3 color{};
    float bump = 0, displacement = 0, roughness = 0;
    simd::float2 slope{};           // dh/dx, dh/dy in texels (central differences)
    simd::float3 normalTangent{};
    // World-space quantities, available when the uv lands on the current shape.
    bool onSurface = false;
    simd::float3 position{}, normalGeom{}, normalBumped{}, tangent{}, bitangent{};
};

// Normal from a height (bump) map, in tangent space. This is the exact formula the
// fragment shader uses (see mac/shaders/pbr.metal, BumpNormalTS).
simd::float3 BumpNormalTangent(const MapImage& bump, float u, float v, float strength,
                               simd::float2* slopeOut = nullptr);

HoverInfo Inspect(const Material& m, float u, float v, Shape shape, const simd::float4x4& world,
                  const BumpSettings& s);
