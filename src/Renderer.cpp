#include "Renderer.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <cstring>

using namespace DirectX;

bool Renderer::Init(HWND hwnd) {
    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 2,
                                   D3D11_SDK_VERSION, &device_, nullptr, &ctx_);
    if (FAILED(hr) && (flags & D3D11_CREATE_DEVICE_DEBUG)) {  // debug layer not installed
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
                               D3D11_SDK_VERSION, &device_, nullptr, &ctx_);
    }
    if (FAILED(hr)) return false;

    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory2> factory;
    device_.As(&dxgiDevice);
    dxgiDevice->GetAdapter(&adapter);
    adapter->GetParent(IID_PPV_ARGS(&factory));

    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if (FAILED(factory->CreateSwapChainForHwnd(device_.Get(), hwnd, &sd, nullptr, nullptr, &swapChain_)))
        return false;
    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    CreateBackbufferView();

    // Fixed pipeline state.
    D3D11_SAMPLER_DESC samp{};
    samp.Filter = D3D11_FILTER_ANISOTROPIC;
    samp.MaxAnisotropy = 8;
    samp.AddressU = samp.AddressV = samp.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    samp.MaxLOD = D3D11_FLOAT32_MAX;
    device_->CreateSamplerState(&samp, &sampler_);

    D3D11_RASTERIZER_DESC rs{};
    rs.FillMode = D3D11_FILL_SOLID;
    rs.CullMode = D3D11_CULL_NONE;
    rs.DepthClipEnable = TRUE;
    rs.MultisampleEnable = TRUE;
    rs.AntialiasedLineEnable = FALSE;
    device_->CreateRasterizerState(&rs, &rasterNoCull_);

    D3D11_DEPTH_STENCIL_DESC ds{};
    ds.DepthEnable = TRUE;
    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    ds.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    device_->CreateDepthStencilState(&ds, &depthOn_);
    ds.DepthEnable = FALSE;
    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    device_->CreateDepthStencilState(&ds, &depthOff_);

    D3D11_BLEND_DESC bd{};
    auto& rt = bd.RenderTarget[0];
    rt.BlendEnable = TRUE;
    rt.SrcBlend = D3D11_BLEND_SRC_ALPHA;
    rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    rt.BlendOp = D3D11_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D11_BLEND_ONE;
    rt.DestBlendAlpha = D3D11_BLEND_ZERO;
    rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    device_->CreateBlendState(&bd, &alphaBlend_);

    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth = sizeof(FrameConstants);
    cb.Usage = D3D11_USAGE_DYNAMIC;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    device_->CreateBuffer(&cb, nullptr, &frameCb_);

    D3D11_BUFFER_DESC lb{};
    lb.ByteWidth = sizeof(LineVertex) * kMaxLineVertices;
    lb.Usage = D3D11_USAGE_DYNAMIC;
    lb.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    lb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    device_->CreateBuffer(&lb, nullptr, &lineVb_);

    // 1x1 mid-gray texture bound when a material lacks a map.
    const uint32_t gray = 0xff808080;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = td.Height = 1;
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init{&gray, 4, 0};
    ComPtr<ID3D11Texture2D> tex;
    device_->CreateTexture2D(&td, &init, &tex);
    device_->CreateShaderResourceView(tex.Get(), nullptr, &fallbackGray_);
    return true;
}

void Renderer::Shutdown() {
    if (ctx_) ctx_->ClearState();
}

void Renderer::CreateBackbufferView() {
    ComPtr<ID3D11Texture2D> bb;
    swapChain_->GetBuffer(0, IID_PPV_ARGS(&bb));
    device_->CreateRenderTargetView(bb.Get(), nullptr, &backbufferRtv_);
}

void Renderer::Resize(UINT width, UINT height) {
    if (!swapChain_ || width == 0 || height == 0) return;
    backbufferRtv_.Reset();
    ctx_->OMSetRenderTargets(0, nullptr, nullptr);
    swapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    CreateBackbufferView();
}

void Renderer::BeginFrame(const float clear[4]) {
    ctx_->OMSetRenderTargets(1, backbufferRtv_.GetAddressOf(), nullptr);
    ctx_->ClearRenderTargetView(backbufferRtv_.Get(), clear);
}

void Renderer::Present(bool vsync) { swapChain_->Present(vsync ? 1 : 0, 0); }

static ComPtr<ID3DBlob> Compile(const std::filesystem::path& file, const char* entry, const char* target,
                                std::string& log) {
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    ComPtr<ID3DBlob> code, errors;
    const HRESULT hr = D3DCompileFromFile(file.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, entry,
                                          target, flags, 0, &code, &errors);
    if (errors) log += static_cast<const char*>(errors->GetBufferPointer());
    if (FAILED(hr)) {
        if (!errors) log += "Cannot compile " + file.string() + "\n";
        return nullptr;
    }
    return code;
}

bool Renderer::ReloadShaders(const std::filesystem::path& dir, std::string& log) {
    log.clear();
    auto vs = Compile(dir / "pbr.hlsl", "VSMain", "vs_5_0", log);
    auto ps = Compile(dir / "pbr.hlsl", "PSMain", "ps_5_0", log);
    auto lvs = Compile(dir / "gizmo.hlsl", "VSLine", "vs_5_0", log);
    auto lps = Compile(dir / "gizmo.hlsl", "PSLine", "ps_5_0", log);
    if (!vs || !ps || !lvs || !lps) return false;  // keep the previous working shaders

    const D3D11_INPUT_ELEMENT_DESC meshElems[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, pos), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, normal), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(Vertex, tangent), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Vertex, uv), D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    const D3D11_INPUT_ELEMENT_DESC lineElems[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(LineVertex, pos), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(LineVertex, color), D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    ComPtr<ID3D11VertexShader> newVs, newLineVs;
    ComPtr<ID3D11PixelShader> newPs, newLinePs;
    ComPtr<ID3D11InputLayout> newLayout, newLineLayout;
    if (FAILED(device_->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &newVs)) ||
        FAILED(device_->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &newPs)) ||
        FAILED(device_->CreateVertexShader(lvs->GetBufferPointer(), lvs->GetBufferSize(), nullptr, &newLineVs)) ||
        FAILED(device_->CreatePixelShader(lps->GetBufferPointer(), lps->GetBufferSize(), nullptr, &newLinePs)) ||
        FAILED(device_->CreateInputLayout(meshElems, 4, vs->GetBufferPointer(), vs->GetBufferSize(), &newLayout)) ||
        FAILED(device_->CreateInputLayout(lineElems, 2, lvs->GetBufferPointer(), lvs->GetBufferSize(), &newLineLayout))) {
        log += "Failed to create shader objects\n";
        return false;
    }
    meshVs_ = newVs; meshPs_ = newPs; lineVs_ = newLineVs; linePs_ = newLinePs;
    meshLayout_ = newLayout; lineLayout_ = newLineLayout;
    return true;
}

void Renderer::EnsureSceneTarget(UINT width, UINT height) {
    if (width == sceneW_ && height == sceneH_ && msaaColor_) return;
    sceneW_ = width;
    sceneH_ = height;
    // Typeless so it can be rendered as sRGB (gamma-correct lighting) but shown by
    // ImGui as UNORM, i.e. the already-encoded values reach the screen untouched.
    D3D11_TEXTURE2D_DESC td{};
    td.Width = width;
    td.Height = height;
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
    td.SampleDesc.Count = 4;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    device_->CreateTexture2D(&td, nullptr, &msaaColor_);
    D3D11_RENDER_TARGET_VIEW_DESC rd{};
    rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;
    device_->CreateRenderTargetView(msaaColor_.Get(), &rd, &msaaRtv_);

    td.SampleDesc.Count = 1;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    device_->CreateTexture2D(&td, nullptr, &resolved_);
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MipLevels = 1;
    device_->CreateShaderResourceView(resolved_.Get(), &sd, &resolvedSrv_);

    td.Format = DXGI_FORMAT_D32_FLOAT;
    td.SampleDesc.Count = 4;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    device_->CreateTexture2D(&td, nullptr, &depth_);
    device_->CreateDepthStencilView(depth_.Get(), nullptr, &dsv_);
}

Renderer::GpuMesh& Renderer::MeshFor(Shape shape) {
    GpuMesh& m = meshes_[int(shape)];
    if (m.vb) return m;
    const MeshData data = BuildMesh(shape, kMeshSegments);
    D3D11_BUFFER_DESC bd{};
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.ByteWidth = UINT(data.vertices.size() * sizeof(Vertex));
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA init{data.vertices.data(), 0, 0};
    device_->CreateBuffer(&bd, &init, &m.vb);
    bd.ByteWidth = UINT(data.indices.size() * sizeof(uint32_t));
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    init.pSysMem = data.indices.data();
    device_->CreateBuffer(&bd, &init, &m.ib);
    m.indexCount = UINT(data.indices.size());
    return m;
}

ID3D11ShaderResourceView* Renderer::RenderScene(UINT width, UINT height, const FrameConstants& fc,
                                                const Material& material, Shape shape,
                                                const std::vector<LineVertex>& lines) {
    if (width == 0 || height == 0 || !meshVs_) return nullptr;
    EnsureSceneTarget(width, height);

    const float clear[4] = {0.035f, 0.04f, 0.05f, 1};  // linear (sRGB target)
    ctx_->ClearRenderTargetView(msaaRtv_.Get(), clear);
    ctx_->ClearDepthStencilView(dsv_.Get(), D3D11_CLEAR_DEPTH, 1, 0);
    ctx_->OMSetRenderTargets(1, msaaRtv_.GetAddressOf(), dsv_.Get());
    const D3D11_VIEWPORT vp{0, 0, float(width), float(height), 0, 1};
    ctx_->RSSetViewports(1, &vp);
    ctx_->RSSetState(rasterNoCull_.Get());
    ctx_->OMSetDepthStencilState(depthOn_.Get(), 0);
    ctx_->OMSetBlendState(nullptr, nullptr, 0xffffffff);

    D3D11_MAPPED_SUBRESOURCE ms;
    ctx_->Map(frameCb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);
    memcpy(ms.pData, &fc, sizeof(fc));
    ctx_->Unmap(frameCb_.Get(), 0);

    // Surface
    GpuMesh& mesh = MeshFor(shape);
    const UINT stride = sizeof(Vertex), offset = 0;
    ctx_->IASetInputLayout(meshLayout_.Get());
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->IASetVertexBuffers(0, 1, mesh.vb.GetAddressOf(), &stride, &offset);
    ctx_->IASetIndexBuffer(mesh.ib.Get(), DXGI_FORMAT_R32_UINT, 0);
    ctx_->VSSetShader(meshVs_.Get(), nullptr, 0);
    ctx_->PSSetShader(meshPs_.Get(), nullptr, 0);
    ctx_->VSSetConstantBuffers(0, 1, frameCb_.GetAddressOf());
    ctx_->PSSetConstantBuffers(0, 1, frameCb_.GetAddressOf());
    auto srv = [&](MapKind k, bool srgb) -> ID3D11ShaderResourceView* {
        const auto& map = material.maps[k];
        if (!map) return fallbackGray_.Get();
        return srgb ? map->srvSrgb.Get() : map->srvLinear.Get();
    };
    ID3D11ShaderResourceView* srvs[4] = {srv(MapColor, true), srv(MapBump, false), srv(MapDisplacement, false),
                                         srv(MapRoughness, false)};
    ctx_->VSSetShaderResources(0, 4, srvs);
    ctx_->PSSetShaderResources(0, 4, srvs);
    ctx_->VSSetSamplers(0, 1, sampler_.GetAddressOf());
    ctx_->PSSetSamplers(0, 1, sampler_.GetAddressOf());
    ctx_->DrawIndexed(mesh.indexCount, 0, 0);

    // Lines: a faint "x-ray" pass ignoring depth, then a depth-tested pass on top.
    if (!lines.empty()) {
        const UINT n = UINT(std::min<size_t>(lines.size(), kMaxLineVertices / 2));
        ctx_->Map(lineVb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);
        auto* dst = static_cast<LineVertex*>(ms.pData);
        for (UINT i = 0; i < n; ++i) {
            dst[i] = lines[i];
            dst[i].color.w *= 0.3f;
            dst[n + i] = lines[i];
        }
        ctx_->Unmap(lineVb_.Get(), 0);
        const UINT lstride = sizeof(LineVertex);
        ctx_->IASetInputLayout(lineLayout_.Get());
        ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
        ctx_->IASetVertexBuffers(0, 1, lineVb_.GetAddressOf(), &lstride, &offset);
        ctx_->VSSetShader(lineVs_.Get(), nullptr, 0);
        ctx_->PSSetShader(linePs_.Get(), nullptr, 0);
        ctx_->OMSetBlendState(alphaBlend_.Get(), nullptr, 0xffffffff);
        ctx_->OMSetDepthStencilState(depthOff_.Get(), 0);
        ctx_->Draw(n, 0);
        ctx_->OMSetDepthStencilState(depthOn_.Get(), 0);
        ctx_->Draw(n, n);
    }

    ID3D11ShaderResourceView* nullSrvs[4] = {};
    ctx_->VSSetShaderResources(0, 4, nullSrvs);
    ctx_->PSSetShaderResources(0, 4, nullSrvs);
    ctx_->ResolveSubresource(resolved_.Get(), 0, msaaColor_.Get(), 0, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
    return resolvedSrv_.Get();
}
