#pragma once
#include "Material.h"
#include "Mesh.h"
#include <Metal/Metal.hpp>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

// Mirrors struct Frame in mac/shaders/pbr.metal (16-byte aligned rows).
struct FrameConstants {
    simd::float4x4 world;
    simd::float4x4 viewProj;
    Float3 cameraPos;  float bumpStrength;
    Float3 lightPos;   float lightIntensity;
    Float3 lightColor; float dispScale;
    float dispMid, dispLod, metallic, ambient;
    int viewMode, useColor, useBump, useDisp;
    int useRough; float constRoughness; simd::float2 hoverUV;
    float hoverOn; Float3 pad;
};
static_assert(sizeof(FrameConstants) == 240 && offsetof(FrameConstants, hoverUV) == 216);

struct LineVertex {
    Float3 pos;
    Float4 color;
};

class Renderer {
public:
    static constexpr int kMeshSegments = 256;

    bool Init();
    void Shutdown();
    // Starts the command buffer that receives all of this frame's GPU work: the scene
    // first, then the user interface (see main.mm, which commits it).
    MTL::CommandBuffer* BeginFrame();

    bool ReloadShaders(const std::filesystem::path& dir, std::string& log);

    // Renders the 3D scene into an offscreen target of the given size and returns
    // a texture with the result that ImGui can display.
    MTL::Texture* RenderScene(uint32_t width, uint32_t height, const FrameConstants& fc,
                              const Material& material, Shape shape,
                              const std::vector<LineVertex>& lines);

    MTL::Device* Device() const { return device_.get(); }
    MTL::CommandQueue* Queue() const { return queue_.get(); }

private:
    struct GpuMesh {
        NS::SharedPtr<MTL::Buffer> vb, ib;
        NS::UInteger indexCount = 0;
    };
    void EnsureSceneTarget(uint32_t width, uint32_t height);
    GpuMesh& MeshFor(Shape shape);

    NS::SharedPtr<MTL::Device> device_;
    NS::SharedPtr<MTL::CommandQueue> queue_;
    MTL::CommandBuffer* frame_ = nullptr;  // owned by the autorelease pool of the frame

    // Offscreen scene target: 4x MSAA color + depth, resolved to a single-sample texture.
    uint32_t sceneW_ = 0, sceneH_ = 0;
    NS::SharedPtr<MTL::Texture> msaaColor_, resolved_, resolvedUnorm_, depth_;

    NS::SharedPtr<MTL::RenderPipelineState> meshPipeline_, linePipeline_;
    NS::SharedPtr<MTL::SamplerState> sampler_;
    NS::SharedPtr<MTL::DepthStencilState> depthOn_, depthOff_;
    NS::SharedPtr<MTL::Texture> fallbackGray_;
    GpuMesh meshes_[int(Shape::Count)];
    static constexpr size_t kMaxLineVertices = 4096;
};
