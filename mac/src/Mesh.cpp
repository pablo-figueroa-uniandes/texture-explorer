#include "Mesh.h"
#include <cmath>

using simd::float3;

namespace {
constexpr float kPi = 3.14159265358979f;

// Texture-space convention (Direct3D and Metal): u grows to the right, v grows DOWN the image.
// For a viewer outside the surface this means cross(dPdu, dPdv) == -N.

// Float3 parameters accept both simd vectors and braced lists such as {0, 1, 0}.
SurfacePoint Make(Float3 p, Float3 n, Float3 du, Float3 dv, float u, float v) {
    return SurfacePoint{p, n, du, dv, simd::float2{u, v}};
}

std::vector<Patch> SpherePatches() {
    Patch side;
    side.invertible = true;
    side.eval = [](float a, float b) {
        const float th = kPi * b, ph = 2 * kPi * a;
        const float st = std::sin(th), ct = std::cos(th), sp = std::sin(ph), cp = std::cos(ph);
        float3 p{st * cp, ct, -st * sp};
        return Make(p, p, {-sp, 0, -cp}, {ct * cp, -st, -ct * sp}, a, b);
    };
    return {side};
}

std::vector<Patch> CylinderPatches() {
    constexpr float r = 0.7f, H = 1.6f;
    Patch side;
    side.invertible = true;
    side.eval = [](float a, float b) {
        const float ph = 2 * kPi * a, sp = std::sin(ph), cp = std::cos(ph);
        return Make({r * cp, H / 2 - b * H, -r * sp}, {cp, 0, -sp}, {-sp, 0, -cp}, {0, -1, 0}, a, b);
    };
    // Caps use a planar projection, so their uv overlaps the side's chart.
    auto cap = [](float y, float ny) {
        Patch c;
        c.eval = [y, ny](float a, float b) {
            const float ph = 2 * kPi * a, x = r * b * std::cos(ph), z = -r * b * std::sin(ph);
            return Make({x, y, z}, {0, ny, 0}, {1, 0, 0}, {0, 0, -ny}, x / (2 * r) + 0.5f, -ny * z / (2 * r) + 0.5f);
        };
        return c;
    };
    return {side, cap(H / 2, 1), cap(-H / 2, -1)};
}

std::vector<Patch> CapsulePatches() {
    constexpr float r = 0.5f, L = 0.5f;           // radius, half length of the cylinder
    constexpr float q = kPi * r / 2;              // arc length of a quarter circle
    constexpr float total = 2 * q + 2 * L;        // v is proportional to profile arc length
    Patch side;
    side.invertible = true;
    side.eval = [](float a, float b) {
        const float s = b * total;
        float radial, y, nr, ny, dr, dy;          // profile point, normal, and d/ds
        if (s < q) {
            const float th = s / r;
            radial = r * std::sin(th); y = L + r * std::cos(th);
            nr = std::sin(th); ny = std::cos(th); dr = std::cos(th); dy = -std::sin(th);
        } else if (s < q + 2 * L) {
            radial = r; y = L - (s - q);
            nr = 1; ny = 0; dr = 0; dy = -1;
        } else {
            const float th = kPi / 2 + (s - q - 2 * L) / r;
            radial = r * std::sin(th); y = -L + r * std::cos(th);
            nr = std::sin(th); ny = std::cos(th); dr = std::cos(th); dy = -std::sin(th);
        }
        const float ph = 2 * kPi * a, sp = std::sin(ph), cp = std::cos(ph);
        return Make({radial * cp, y, -radial * sp}, {nr * cp, ny, -nr * sp}, {-sp, 0, -cp},
                    {dr * cp, dy, -dr * sp}, a, b);
    };
    return {side};
}

std::vector<Patch> PyramidPatches() {
    constexpr float a = 0.8f, h = 0.8f;
    static const float3 apex{0, h, 0};
    static const float3 c[4] = {{-a, -h, a}, {a, -h, a}, {a, -h, -a}, {-a, -h, -a}};
    std::vector<Patch> patches;
    // Each side face owns a quarter of the texture in u; v runs from apex (0) to base (1).
    for (int i = 0; i < 4; ++i) {
        Patch f;
        f.invertible = true;
        f.u0 = i / 4.0f; f.u1 = (i + 1) / 4.0f;
        f.eval = [i](float s, float t) {
            const float3 A = apex, C0 = c[i], C1 = c[(i + 1) % 4];
            const float3 edge = C0 + s * (C1 - C0);  // XMVectorLerp
            const float3 P = A + t * (edge - A);
            const float3 du = C1 - C0, dv = edge - A;
            const float3 N = simd::normalize(simd::cross(0.5f * (C0 + C1) - A, du));
            return Make(P, N, du, dv, (i + s) / 4.0f, t);
        };
        patches.push_back(f);
    }
    Patch base;
    base.eval = [](float s, float t) {
        const float x = -a + 2 * a * s, z = a - 2 * a * t;
        return Make({x, -h, z}, {0, -1, 0}, {1, 0, 0}, {0, 0, -1}, s, t);
    };
    patches.push_back(base);
    return patches;
}
} // namespace

const char* ShapeName(Shape s) {
    switch (s) {
    case Shape::Sphere: return "Sphere";
    case Shape::Pyramid: return "Pyramid";
    case Shape::Cylinder: return "Cylinder";
    case Shape::Capsule: return "Capsule";
    default: return "?";
    }
}

std::vector<Patch> MakePatches(Shape shape) {
    switch (shape) {
    case Shape::Sphere: return SpherePatches();
    case Shape::Pyramid: return PyramidPatches();
    case Shape::Cylinder: return CylinderPatches();
    case Shape::Capsule: return CapsulePatches();
    default: return {};
    }
}

void TangentFrame(const SurfacePoint& p, float3& T, float3& B, float3& N) {
    const float3 n = simd::normalize(p.normal);
    float3 t = p.dPdu - n * simd::dot(n, p.dPdu);
    if (simd::length_squared(t) < 1e-12f)  // degenerate: derive from dv instead
        t = simd::cross(p.dPdv, n);
    t = simd::normalize(t);
    float3 b = simd::cross(n, t);
    if (simd::dot(b, p.dPdv) < 0) b = -b;
    T = t; B = b; N = n;
}

MeshData BuildMesh(Shape shape, int segments) {
    MeshData mesh;
    for (const Patch& patch : MakePatches(shape)) {
        const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
        for (int j = 0; j <= segments; ++j) {
            for (int i = 0; i <= segments; ++i) {
                const SurfacePoint sp = patch.eval(float(i) / segments, float(j) / segments);
                float3 T, B, N;
                TangentFrame(sp, T, B, N);
                // Handedness: B = w * cross(N, T)
                const float w = simd::dot(simd::cross(N, T), B) < 0 ? -1.0f : 1.0f;
                mesh.vertices.push_back({sp.pos, N, {T.x, T.y, T.z, w}, sp.uv});
            }
        }
        const uint32_t row = segments + 1;
        for (int j = 0; j < segments; ++j) {
            for (int i = 0; i < segments; ++i) {
                const uint32_t i0 = base + j * row + i, i1 = i0 + 1, i2 = i0 + row, i3 = i2 + 1;
                mesh.indices.insert(mesh.indices.end(), {i0, i2, i1, i1, i2, i3});
            }
        }
    }
    return mesh;
}

bool FindSurfacePoint(Shape shape, float u, float v, SurfacePoint& out) {
    for (const Patch& patch : MakePatches(shape)) {
        if (!patch.invertible) continue;
        if (u < patch.u0 || u > patch.u1 || v < patch.v0 || v > patch.v1) continue;
        out = patch.eval((u - patch.u0) / (patch.u1 - patch.u0), (v - patch.v0) / (patch.v1 - patch.v0));
        return true;
    }
    return false;
}
