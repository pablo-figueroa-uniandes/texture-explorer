#pragma once
// Parametric shapes. Every shape is a set of patches; each patch maps a parameter
// square (a,b) in [0,1]^2 to a surface point with position, normal, tangent frame
// and texture coordinates. The same functions build the GPU meshes and answer the
// inspector's question "where on the object is texel (u,v)?".
#include "VecMath.h"
#include <functional>
#include <vector>
#include <cstdint>

enum class Shape { Sphere, Pyramid, Cylinder, Capsule, Count };
const char* ShapeName(Shape s);

struct SurfacePoint {
    simd::float3 pos;     // object space
    simd::float3 normal;  // unit outward geometric normal
    simd::float3 dPdu;    // direction of increasing u (tangent T, not normalized)
    simd::float3 dPdv;    // direction of increasing v (bitangent B, not normalized)
    simd::float2 uv;
};

// Packed like the Direct3D vertex (48 bytes); described to Metal by a vertex descriptor.
struct Vertex {
    Float3 pos;
    Float3 normal;
    Float4 tangent;  // xyz = T orthogonalized against N, w = sign so B = w * cross(N, T)
    simd::float2 uv;
};
static_assert(sizeof(Vertex) == 48);

struct Patch {
    std::function<SurfacePoint(float a, float b)> eval;
    // Patches whose uv equals (lerp(u0,u1,a), lerp(v0,v1,b)) form a one-to-one chart
    // of the texture and can be inverted by the inspector.
    bool invertible = false;
    float u0 = 0, u1 = 1, v0 = 0, v1 = 1;
};

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

std::vector<Patch> MakePatches(Shape shape);
MeshData BuildMesh(Shape shape, int segments);
// Finds the surface point whose texture coordinate is (u,v). Returns false if no
// invertible patch covers that uv.
bool FindSurfacePoint(Shape shape, float u, float v, SurfacePoint& out);
// Builds the orthonormal tangent frame used by both shader and inspector.
void TangentFrame(const SurfacePoint& p, simd::float3& T, simd::float3& B, simd::float3& N);
