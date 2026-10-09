// Just enough of the Direct3D 11 API for src/Material.cpp to compile and run on macOS, so
// that the Windows version's CPU code (shapes, image sampling, inspector) can be compared
// with the macOS port. Device calls succeed and do nothing: the probe never draws.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cwchar>

typedef unsigned int UINT;
typedef long HRESULT;
#define S_OK 0
#define FAILED(hr) ((hr) < 0)

enum DXGI_FORMAT { DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB };
enum D3D11_USAGE { D3D11_USAGE_DEFAULT };
enum { D3D11_BIND_SHADER_RESOURCE = 0x8, D3D11_BIND_RENDER_TARGET = 0x20 };
enum { D3D11_RESOURCE_MISC_GENERATE_MIPS = 0x1 };
enum D3D11_SRV_DIMENSION { D3D11_SRV_DIMENSION_TEXTURE2D = 4 };

struct DXGI_SAMPLE_DESC { UINT Count, Quality; };
struct D3D11_TEXTURE2D_DESC {
    UINT Width, Height, MipLevels, ArraySize;
    DXGI_FORMAT Format;
    DXGI_SAMPLE_DESC SampleDesc;
    D3D11_USAGE Usage;
    UINT BindFlags, CPUAccessFlags, MiscFlags;
};
struct D3D11_TEX2D_SRV { UINT MostDetailedMip, MipLevels; };
struct D3D11_SHADER_RESOURCE_VIEW_DESC {
    DXGI_FORMAT Format;
    D3D11_SRV_DIMENSION ViewDimension;
    D3D11_TEX2D_SRV Texture2D;
};
struct D3D11_SUBRESOURCE_DATA;
struct D3D11_BOX;

struct ID3D11Resource {};
struct ID3D11Texture2D : ID3D11Resource {};
struct ID3D11ShaderResourceView {};

struct ID3D11Device {
    HRESULT CreateTexture2D(const D3D11_TEXTURE2D_DESC*, const D3D11_SUBRESOURCE_DATA*, ID3D11Texture2D**) { return S_OK; }
    HRESULT CreateShaderResourceView(ID3D11Resource*, const D3D11_SHADER_RESOURCE_VIEW_DESC*, ID3D11ShaderResourceView**) {
        return S_OK;
    }
};
struct ID3D11DeviceContext {
    void UpdateSubresource(ID3D11Resource*, UINT, const D3D11_BOX*, const void*, UINT, UINT) {}
    void GenerateMips(ID3D11ShaderResourceView*) {}
};

// The Windows code opens files with wide-character paths; on macOS paths are UTF-8.
inline int _wfopen_s(FILE** f, const char* path, const wchar_t*) {
    *f = std::fopen(path, "rb");
    return *f ? 0 : 1;
}
