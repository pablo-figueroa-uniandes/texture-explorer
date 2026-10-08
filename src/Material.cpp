#include "Material.h"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <nlohmann/json.hpp>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#include <stb_image.h>

using namespace DirectX;

const char* MapName(MapKind k) {
    static const char* names[] = {"Color (albedo)", "Bump", "Displacement", "Specular roughness", "Normal map (reference)"};
    return names[k];
}

static int Wrap(int i, int n) { return ((i % n) + n) % n; }

float MapImage::Texel(int x, int y) const {
    return rgba[(size_t(Wrap(y, height)) * width + Wrap(x, width)) * 4] / 255.0f;
}

XMFLOAT4 MapImage::Sample(float u, float v) const {
    // Same convention as the GPU: texel centers sit at (i + 0.5) / size.
    const float x = u * width - 0.5f, y = v * height - 0.5f;
    const int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    const float fx = x - x0, fy = y - y0;
    float out[4] = {};
    for (int c = 0; c < 4; ++c) {
        auto at = [&](int xi, int yi) {
            return rgba[(size_t(Wrap(yi, height)) * width + Wrap(xi, width)) * 4 + c] / 255.0f;
        };
        const float top = at(x0, y0) * (1 - fx) + at(x0 + 1, y0) * fx;
        const float bot = at(x0, y0 + 1) * (1 - fx) + at(x0 + 1, y0 + 1) * fx;
        out[c] = top * (1 - fy) + bot * fy;
    }
    return {out[0], out[1], out[2], out[3]};
}

static std::shared_ptr<MapImage> LoadImage(const std::filesystem::path& path, bool srgb,
                                           ID3D11Device* device, ID3D11DeviceContext* ctx) {
    FILE* f = nullptr;
    _wfopen_s(&f, path.c_str(), L"rb");
    if (!f) return nullptr;
    int w = 0, h = 0, n = 0;
    stbi_uc* pixels = stbi_load_from_file(f, &w, &h, &n, 4);
    fclose(f);
    if (!pixels) return nullptr;

    auto img = std::make_shared<MapImage>();
    img->width = w;
    img->height = h;
    img->rgba.assign(pixels, pixels + size_t(w) * h * 4);
    stbi_image_free(pixels);

    // Typeless storage lets the same texture be viewed as linear (UI) or sRGB (shading).
    D3D11_TEXTURE2D_DESC td{};
    td.Width = w;
    td.Height = h;
    td.MipLevels = 0;  // full chain
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    if (FAILED(device->CreateTexture2D(&td, nullptr, &img->texture))) return nullptr;

    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MipLevels = UINT(-1);
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    device->CreateShaderResourceView(img->texture.Get(), &sd, &img->srvLinear);
    if (srgb) {
        sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        device->CreateShaderResourceView(img->texture.Get(), &sd, &img->srvSrgb);
    } else {
        img->srvSrgb = img->srvLinear;
    }
    ctx->UpdateSubresource(img->texture.Get(), 0, nullptr, img->rgba.data(), w * 4, 0);
    ctx->GenerateMips(img->srvLinear.Get());
    return img;
}

bool MaterialLibrary::Load(const std::filesystem::path& materialsDir, std::string& error) {
    dir_ = materialsDir;
    std::ifstream in(materialsDir / "manifest.json");
    if (!in) {
        error = "Cannot open " + (materialsDir / "manifest.json").string() +
                "\nRun tools\\fetch_textures.ps1 first.";
        return false;
    }
    try {
        const nlohmann::json j = nlohmann::json::parse(in);
        static const char* keys[MapCount] = {"color", "bump", "displacement", "roughness", "normal"};
        for (const auto& e : j) {
            Material m;
            m.id = e.value("id", "");
            m.name = e.value("name", m.id);
            m.site = e.value("site", "");
            m.url = e.value("url", "");
            m.author = e.value("author", "");
            m.license = e.value("license", "");
            m.bumpFromHeight = e.value("bumpFromHeight", true);
            if (e.contains("files"))
                for (int k = 0; k < MapCount; ++k)
                    m.files[k] = e["files"].value(keys[k], "");
            items_.push_back(std::move(m));
        }
    } catch (const std::exception& ex) {
        error = std::string("manifest.json: ") + ex.what();
        return false;
    }
    if (items_.empty()) error = "manifest.json lists no materials";
    return !items_.empty();
}

void MaterialLibrary::EnsureLoaded(int index, ID3D11Device* device, ID3D11DeviceContext* ctx) {
    Material& m = items_[index];
    if (m.loaded) return;
    m.loaded = true;
    for (int k = 0; k < MapCount; ++k)
        if (!m.files[k].empty())
            m.maps[k] = LoadImage(dir_ / std::filesystem::path(reinterpret_cast<const char8_t*>(m.files[k].c_str())), k == MapColor, device, ctx);
    // A bump map is a height field used only for shading. When a pack has no separate
    // one, the height (displacement) map is the bump map.
    if (!m.maps[MapBump]) {
        m.maps[MapBump] = m.maps[MapDisplacement];
        m.bumpFromHeight = true;
    } else {
        m.bumpFromHeight = false;
    }
}
