#include "Material.h"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <nlohmann/json.hpp>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#include <stb_image.h>

const char* MapName(MapKind k) {
    static const char* names[] = {"Color (albedo)", "Bump", "Displacement", "Specular roughness", "Normal map (reference)"};
    return names[k];
}

static int Wrap(int i, int n) { return ((i % n) + n) % n; }

float MapImage::Texel(int x, int y) const {
    return rgba[(size_t(Wrap(y, height)) * width + Wrap(x, width)) * 4] / 255.0f;
}

simd::float4 MapImage::Sample(float u, float v) const {
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
                                           MTL::Device* device, MTL::CommandQueue* queue) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return nullptr;
    int w = 0, h = 0, n = 0;
    stbi_uc* pixels = stbi_load_from_file(f, &w, &h, &n, 4);
    std::fclose(f);
    if (!pixels) return nullptr;

    auto img = std::make_shared<MapImage>();
    img->width = w;
    img->height = h;
    img->rgba.assign(pixels, pixels + size_t(w) * h * 4);
    stbi_image_free(pixels);

    // Metal's equivalent of a typeless texture: a texture that allows views with a
    // different pixel format, so it can be read as linear (UI) or sRGB (shading).
    MTL::TextureDescriptor* td = MTL::TextureDescriptor::texture2DDescriptor(
        MTL::PixelFormatRGBA8Unorm, NS::UInteger(w), NS::UInteger(h), true);  // full mip chain
    td->setUsage(MTL::TextureUsageShaderRead | MTL::TextureUsagePixelFormatView);
    td->setStorageMode(MTL::StorageModeManaged);
    img->texLinear = NS::TransferPtr(device->newTexture(td));
    if (!img->texLinear) return nullptr;
    img->texLinear->replaceRegion(MTL::Region::Make2D(0, 0, NS::UInteger(w), NS::UInteger(h)), 0,
                                  img->rgba.data(), NS::UInteger(w) * 4);
    img->texSrgb = srgb ? NS::TransferPtr(img->texLinear->newTextureView(MTL::PixelFormatRGBA8Unorm_sRGB))
                        : img->texLinear;

    // The GPU computes the smaller mip levels from level 0.
    MTL::CommandBuffer* cmd = queue->commandBuffer();
    MTL::BlitCommandEncoder* blit = cmd->blitCommandEncoder();
    blit->generateMipmaps(img->texLinear.get());
    blit->endEncoding();
    cmd->commit();
    return img;
}

bool MaterialLibrary::Load(const std::filesystem::path& materialsDir, std::string& error) {
    dir_ = materialsDir;
    std::ifstream in(materialsDir / "manifest.json");
    if (!in) {
        error = "Cannot open " + (materialsDir / "manifest.json").string() +
                "\nRun tools/fetch_textures.ps1 first.";
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

void MaterialLibrary::EnsureLoaded(int index, MTL::Device* device, MTL::CommandQueue* queue) {
    Material& m = items_[index];
    if (m.loaded) return;
    m.loaded = true;
    for (int k = 0; k < MapCount; ++k)
        if (!m.files[k].empty())
            m.maps[k] = LoadImage(dir_ / std::filesystem::path(reinterpret_cast<const char8_t*>(m.files[k].c_str())), k == MapColor, device, queue);
    // A bump map is a height field used only for shading. When a pack has no separate
    // one, the height (displacement) map is the bump map.
    if (!m.maps[MapBump]) {
        m.maps[MapBump] = m.maps[MapDisplacement];
        m.bumpFromHeight = true;
    } else {
        m.bumpFromHeight = false;
    }
}
