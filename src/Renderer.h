#pragma once
#include "Material.h"
#include "Mesh.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <DirectXMath.h>
#include <filesystem>
#include <string>
#include <vector>

// Mirrors cbuffer Frame in shaders/pbr.hlsl (16-byte aligned rows).
struct FrameConstants {
    DirectX::XMFLOAT4X4 world;
    DirectX::XMFLOAT4X4 viewProj;
    DirectX::XMFLOAT3 cameraPos;  float bumpStrength;
    DirectX::XMFLOAT3 lightPos;   float lightIntensity;
    DirectX::XMFLOAT3 lightColor; float dispScale;
    float dispMid, dispLod, metallic, ambient;
    int viewMode, useColor, useBump, useDisp;
    int useRough; float constRoughness; DirectX::XMFLOAT2 hoverUV;
    float hoverOn; DirectX::XMFLOAT3 pad;
};
static_assert(sizeof(FrameConstants) % 16 == 0);

struct LineVertex {
    DirectX::XMFLOAT3 pos;
    DirectX::XMFLOAT4 color;
};

class Renderer {
public:
    static constexpr int kMeshSegments = 256;

    bool Init(HWND hwnd);
    void Shutdown();
    void Resize(UINT width, UINT height);
    void BeginFrame(const float clear[4]);
    void Present(bool vsync);

    bool ReloadShaders(const std::filesystem::path& dir, std::string& log);

    // Renders the 3D scene into an offscreen target of the given size and returns
    // a view of the result that ImGui can display.
    ID3D11ShaderResourceView* RenderScene(UINT width, UINT height, const FrameConstants& fc,
                                          const Material& material, Shape shape,
                                          const std::vector<LineVertex>& lines);

    ID3D11Device* Device() const { return device_.Get(); }
    ID3D11DeviceContext* Context() const { return ctx_.Get(); }

private:
    struct GpuMesh {
        ComPtr<ID3D11Buffer> vb, ib;
        UINT indexCount = 0;
    };
    void CreateBackbufferView();
    void EnsureSceneTarget(UINT width, UINT height);
    GpuMesh& MeshFor(Shape shape);

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> ctx_;
    ComPtr<IDXGISwapChain1> swapChain_;
    ComPtr<ID3D11RenderTargetView> backbufferRtv_;

    // Offscreen scene target: 4x MSAA color + depth, resolved to a single-sample texture.
    UINT sceneW_ = 0, sceneH_ = 0;
    ComPtr<ID3D11Texture2D> msaaColor_, resolved_, depth_;
    ComPtr<ID3D11RenderTargetView> msaaRtv_;
    ComPtr<ID3D11DepthStencilView> dsv_;
    ComPtr<ID3D11ShaderResourceView> resolvedSrv_;

    ComPtr<ID3D11VertexShader> meshVs_, lineVs_;
    ComPtr<ID3D11PixelShader> meshPs_, linePs_;
    ComPtr<ID3D11InputLayout> meshLayout_, lineLayout_;
    ComPtr<ID3D11Buffer> frameCb_, lineVb_;
    ComPtr<ID3D11SamplerState> sampler_;
    ComPtr<ID3D11RasterizerState> rasterNoCull_;
    ComPtr<ID3D11DepthStencilState> depthOn_, depthOff_;
    ComPtr<ID3D11BlendState> alphaBlend_;
    ComPtr<ID3D11ShaderResourceView> fallbackGray_;
    GpuMesh meshes_[int(Shape::Count)];
    static constexpr UINT kMaxLineVertices = 4096;
};
