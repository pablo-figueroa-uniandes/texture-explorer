#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

enum MapKind { MapColor, MapBump, MapDisplacement, MapRoughness, MapNormal, MapCount };
const char* MapName(MapKind k);

// One texture: kept both on the GPU (for rendering / UI) and on the CPU (for the inspector).
struct MapImage {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;                    // CPU copy, 8 bits per channel
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> srvLinear;   // raw values (data maps, UI display)
    ComPtr<ID3D11ShaderResourceView> srvSrgb;     // color decoded from sRGB (shading)

    // Bilinear sample with wrap addressing; channels in [0,1].
    DirectX::XMFLOAT4 Sample(float u, float v) const;
    // Nearest texel fetch with wrap addressing (red channel).
    float Texel(int x, int y) const;
};

struct Material {
    std::string id, name, site, url, author, license;
    std::string files[MapCount];
    bool bumpFromHeight = true;
    bool loaded = false;
    std::shared_ptr<MapImage> maps[MapCount];     // may be null when a map is not available
};

class MaterialLibrary {
public:
    bool Load(const std::filesystem::path& materialsDir, std::string& error);
    // Lazily uploads a material's textures the first time it is selected.
    void EnsureLoaded(int index, ID3D11Device* device, ID3D11DeviceContext* ctx);
    std::vector<Material>& Items() { return items_; }

private:
    std::filesystem::path dir_;
    std::vector<Material> items_;
};
