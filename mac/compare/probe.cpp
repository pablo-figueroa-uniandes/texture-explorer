// probe - prints what the CPU half of Texture Explorer computes: a sample of every mesh,
// and the inspector's answer (values, slope, normals, surface point) on a grid of texels,
// for every material and shape. The same file is compiled twice: against the Windows
// sources in src/ (with TEXEX_CORE_WINDOWS, DirectXMath and the stand-ins in shims/) and
// against the macOS port in mac/src/. compare.py then compares the two outputs.
#ifdef TEXEX_CORE_WINDOWS
#include "Inspector.h"
static ID3D11Device g_device;
static ID3D11DeviceContext g_context;
#define TEXEX_CORE_NAME "windows (src/, DirectXMath)"
#else
#define NS_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#include "Inspector.h"
#define TEXEX_CORE_NAME "mac (mac/src/, simd)"
#endif
#include <cstdio>
#include <filesystem>
#include <string>

namespace {
// Both versions name their fields x, y, z, w, whatever the vector type.
template <class V> void P2(const V& v) { std::printf(" %.7g %.7g", v.x, v.y); }
template <class V> void P3(const V& v) { std::printf(" %.7g %.7g %.7g", v.x, v.y, v.z); }
template <class V> void P4(const V& v) { std::printf(" %.7g %.7g %.7g %.7g", v.x, v.y, v.z, v.w); }

void PrintMeshes() {
    for (int s = 0; s < int(Shape::Count); ++s) {
        const MeshData mesh = BuildMesh(Shape(s), 256);
        uint64_t indexSum = 0;
        for (uint32_t i : mesh.indices) indexSum += i;
        std::printf("mesh %s counts %zu %zu %llu\n", ShapeName(Shape(s)), mesh.vertices.size(), mesh.indices.size(),
                    (unsigned long long)indexSum);
        for (size_t i = 0; i < mesh.vertices.size(); i += 997) {
            const Vertex& v = mesh.vertices[i];
            std::printf("vertex %s %zu", ShapeName(Shape(s)), i);
            P3(v.pos); P3(v.normal); P4(v.tangent); P2(v.uv);
            std::printf("\n");
        }
    }
}

void PrintProbe(const Material& m, Shape shape, float u, float v, float angle) {
#ifdef TEXEX_CORE_WINDOWS
    const HoverInfo h = Inspect(m, u, v, shape, DirectX::XMMatrixRotationY(angle), BumpSettings{});
#else
    const HoverInfo h = Inspect(m, u, v, shape, MatrixRotationY(angle), BumpSettings{});
#endif
    std::printf("probe %s %s %.4f %.4f", m.id.c_str(), ShapeName(shape), u, v);
    P3(h.color);
    std::printf(" %.7g %.7g %.7g", h.bump, h.displacement, h.roughness);
    P2(h.slope); P3(h.normalTangent);
    std::printf(" %d", int(h.onSurface));
    if (h.onSurface) {
        P3(h.position); P3(h.normalGeom); P3(h.normalBumped); P3(h.tangent); P3(h.bitangent);
    }
    std::printf("\n");
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <repository root>\n", argv[0]);
        return 2;
    }
    MaterialLibrary library;
    std::string error;
    if (!library.Load(std::filesystem::path(argv[1]) / "assets" / "materials", error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
#ifndef TEXEX_CORE_WINDOWS
    NS::SharedPtr<MTL::Device> device = NS::TransferPtr(MTL::CreateSystemDefaultDevice());
    NS::SharedPtr<MTL::CommandQueue> queue = NS::TransferPtr(device->newCommandQueue());
#endif
    std::printf("core %s\n", TEXEX_CORE_NAME);
    PrintMeshes();
    for (int i = 0; i < int(library.Items().size()); ++i) {
#ifdef TEXEX_CORE_WINDOWS
        library.EnsureLoaded(i, &g_device, &g_context);
#else
        library.EnsureLoaded(i, device.get(), queue.get());
#endif
        const Material& m = library.Items()[i];
        for (int s = 0; s < int(Shape::Count); ++s)
            for (int y = 0; y <= 8; ++y)
                for (int x = 0; x <= 8; ++x) {
                    PrintProbe(m, Shape(s), x / 8.0f, y / 8.0f, 0.7f);  // includes the borders
                    PrintProbe(m, Shape(s), (x + 0.37f) / 9, (y + 0.61f) / 9, 0.7f);  // between texel centers
                }
    }
    return 0;
}
